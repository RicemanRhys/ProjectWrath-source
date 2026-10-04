/**
 * XAudio2 Audio Output Backend
 *
 * Provides low-latency audio output via XAudio2 (Win7+).
 * Called from the APU monitor frame to submit mixed samples.
 * Falls back gracefully if XAudio2 is unavailable.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apu_xaudio2.h"

/* The XAudio2 backend is Windows-only. On Linux all xa2_* functions are
 * stubbed to report inactive; real audio output via SDL2 comes later. */
#if defined(_WIN32)

#define COBJMACROS
#include <windows.h>
#include <xaudio2.h>

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "ole32.lib")

#define XA2_SAMPLE_RATE   48000
#define XA2_CHANNELS      2
#define XA2_MAX_CHANNELS  6      /* 5.1, when asked for and the device takes it */
#define XA2_BUF_SAMPLES   1024   /* ~21ms per submission */
/* The monitor submits each 256-sample APU frame as it is produced (5.3 ms),
 * so the ring is sized in those: 16 of them queue at most ~85 ms. */
#define XA2_NUM_BUFS      16

static IXAudio2               *g_xa2 = NULL;
static IXAudio2MasteringVoice *g_xa2_master = NULL;
static IXAudio2SourceVoice    *g_xa2_source = NULL;
static int16_t                 g_xa2_bufs[XA2_NUM_BUFS][XA2_BUF_SAMPLES][XA2_MAX_CHANNELS];
static int                     g_xa2_next_buf = 0;
static int                     g_xa2_initialized = 0;
static int                     g_xa2_frames_written = 0;
static int                     g_xa2_channels = XA2_CHANNELS;   /* the source voice's: 2, or 6 */
static int                     g_xa2_device_channels = 0;       /* probed when 5.1 is asked for */

/* 5.1 OUTPUT (notes 18.58). RECOMP_SPEAKERS=5.1 asks for six channels, in
 * the order FL FR C LFE BL BR (the MCPX's speaker bins 0-5). The default
 * device's mastering voice is opened at its own channel count; when its
 * speakers include front left/right, center, LFE and a surround pair (back,
 * or side), the source voice takes six channels and an explicit matrix puts
 * each on its speaker (the surrounds on the side pair when there is one, as
 * 5.1 is laid out in 7.1). Otherwise the output is the stereo below, exactly
 * as without the switch, and the [XA2] line says why.
 * RECOMP_APU_FORCE_51=1 (a check) opens 5.1 on any device: Windows then
 * downmixes it to the device's channels. */
#define SPK_FL  0x001u
#define SPK_FR  0x002u
#define SPK_FC  0x004u
#define SPK_LFE 0x008u
#define SPK_BL  0x010u
#define SPK_BR  0x020u
#define SPK_SL  0x200u
#define SPK_SR  0x400u

static int want_51(int *force)
{
    const char *e = getenv("RECOMP_SPEAKERS"), *f = getenv("RECOMP_APU_FORCE_51");

    *force = f && *f && strcmp(f, "0") != 0;
    return e && strcmp(e, "5.1") == 0;
}

/* The device channel that carries `bit` of `mask`. */
static int spk_index(DWORD mask, DWORD bit)
{
    int n = 0;
    DWORD b;

    for (b = 1; b < bit; b <<= 1)
        if (mask & b)
            n++;
    return n;
}

/* The mastering and source voices at 5.1, or 0 with neither made. */
static int open_51(int force)
{
    static const DWORD front[4] = { SPK_FL, SPK_FR, SPK_FC, SPK_LFE };
    XAUDIO2_VOICE_DETAILS det;
    WAVEFORMATEXTENSIBLE wfx;
    DWORD mask = 0, bl, br;
    float matrix[XA2_MAX_CHANNELS * 32];
    HRESULT hr;
    int i, devch;

    hr = IXAudio2_CreateMasteringVoice(g_xa2, &g_xa2_master,
        force ? XA2_MAX_CHANNELS : XAUDIO2_DEFAULT_CHANNELS, XA2_SAMPLE_RATE, 0, NULL, NULL, 0);
    if (FAILED(hr)) {
        fprintf(stderr, "[XA2] 5.1: CreateMasteringVoice failed: 0x%08lX; stereo\n", hr);
        g_xa2_master = NULL;
        return 0;
    }
    g_xa2_master->lpVtbl->GetVoiceDetails(g_xa2_master, &det);
    g_xa2_master->lpVtbl->GetChannelMask(g_xa2_master, &mask);
    devch = (int)det.InputChannels;
    g_xa2_device_channels = devch;
    bl = (mask & SPK_SL) && (mask & SPK_SR) ? SPK_SL : SPK_BL;
    br = bl == SPK_SL ? SPK_SR : SPK_BR;
    for (i = 0; i < 4 && (mask & front[i]); i++)
        ;
    if (i < 4 || !(mask & bl) || !(mask & br) || devch > 32) {
        fprintf(stderr, "[XA2] 5.1 asked for, but the default device takes %d channels (mask 0x%lX): stereo\n",
                devch, (unsigned long)mask);
        g_xa2_master->lpVtbl->DestroyVoice(g_xa2_master);
        g_xa2_master = NULL;
        return 0;
    }

    memset(&wfx, 0, sizeof(wfx));
    wfx.Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
    wfx.Format.nChannels       = XA2_MAX_CHANNELS;
    wfx.Format.nSamplesPerSec  = XA2_SAMPLE_RATE;
    wfx.Format.wBitsPerSample  = 16;
    wfx.Format.nBlockAlign     = XA2_MAX_CHANNELS * 2;
    wfx.Format.nAvgBytesPerSec = XA2_SAMPLE_RATE * wfx.Format.nBlockAlign;
    wfx.Format.cbSize          = sizeof(wfx) - sizeof(WAVEFORMATEX);
    wfx.Samples.wValidBitsPerSample = 16;
    wfx.dwChannelMask = SPK_FL | SPK_FR | SPK_FC | SPK_LFE | SPK_BL | SPK_BR;
    {   /* KSDATAFORMAT_SUBTYPE_PCM, without ksguid.lib */
        static const GUID pcm = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
        wfx.SubFormat = pcm;
    }
    hr = IXAudio2_CreateSourceVoice(g_xa2, &g_xa2_source,
        &wfx.Format, 0, XAUDIO2_DEFAULT_FREQ_RATIO, NULL, NULL, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "[XA2] 5.1: CreateSourceVoice failed: 0x%08lX; stereo\n", hr);
        g_xa2_source = NULL;
        g_xa2_master->lpVtbl->DestroyVoice(g_xa2_master);
        g_xa2_master = NULL;
        return 0;
    }

    /* matrix[d * 6 + s]: source channel s to device channel d */
    memset(matrix, 0, sizeof(matrix));
    {
        const DWORD to[XA2_MAX_CHANNELS] = { SPK_FL, SPK_FR, SPK_FC, SPK_LFE, bl, br };
        for (i = 0; i < XA2_MAX_CHANNELS; i++)
            matrix[spk_index(mask, to[i]) * XA2_MAX_CHANNELS + i] = 1.0f;
    }
    hr = g_xa2_source->lpVtbl->SetOutputMatrix(g_xa2_source, NULL, XA2_MAX_CHANNELS, devch, matrix,
                                               XAUDIO2_COMMIT_NOW);
    if (FAILED(hr))
        fprintf(stderr, "[XA2] 5.1: SetOutputMatrix failed: 0x%08lX (XAudio2's own mapping)\n", hr);
    g_xa2_channels = XA2_MAX_CHANNELS;
    fprintf(stderr, "[XA2] 5.1 on a device of %d channels (mask 0x%lX), surrounds to its %s pair%s\n",
            devch, (unsigned long)mask, bl == SPK_SL ? "side" : "back",
            force ? " (RECOMP_APU_FORCE_51)" : "");
    return 1;
}

int xa2_init(void)
{
    HRESULT hr;
    int com_initialized, force;
    WAVEFORMATEX wfx = { 0 };

    if (g_xa2_initialized) return 1;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        fprintf(stderr, "[XA2] CoInitializeEx failed: 0x%08lX\n", hr);
        return 0;
    }
    com_initialized = SUCCEEDED(hr);

    hr = XAudio2Create(&g_xa2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr) || !g_xa2) {
        fprintf(stderr, "[XA2] XAudio2Create failed: 0x%08lX\n", hr);
        goto fail;
    }

    g_xa2_channels = XA2_CHANNELS;
    if (want_51(&force) && open_51(force))
        goto start;

    hr = IXAudio2_CreateMasteringVoice(g_xa2, &g_xa2_master,
        XA2_CHANNELS, XA2_SAMPLE_RATE, 0, NULL, NULL, 0);
    if (FAILED(hr)) {
        fprintf(stderr, "[XA2] CreateMasteringVoice failed: 0x%08lX\n", hr);
        goto fail;
    }

    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = XA2_CHANNELS;
    wfx.nSamplesPerSec  = XA2_SAMPLE_RATE;
    wfx.wBitsPerSample  = 16;
    wfx.nBlockAlign     = XA2_CHANNELS * 2;
    wfx.nAvgBytesPerSec = XA2_SAMPLE_RATE * wfx.nBlockAlign;

    hr = IXAudio2_CreateSourceVoice(g_xa2, &g_xa2_source,
        &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, NULL, NULL, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "[XA2] CreateSourceVoice failed: 0x%08lX\n", hr);
        goto fail;
    }

start:
    hr = IXAudio2SourceVoice_Start(g_xa2_source, 0, XAUDIO2_COMMIT_NOW);
    if (FAILED(hr)) {
        fprintf(stderr, "[XA2] Start failed: 0x%08lX\n", hr);
        goto fail;
    }

    g_xa2_next_buf = 0;
    g_xa2_initialized = 1;
    g_xa2_frames_written = 0;

    fprintf(stderr, "[XA2] XAudio2 initialized (%d Hz %s 16-bit, %d x %d-sample buffers)\n",
            XA2_SAMPLE_RATE, g_xa2_channels == XA2_MAX_CHANNELS ? "5.1" : "stereo", XA2_NUM_BUFS,
            XA2_BUF_SAMPLES);
    return 1;

fail:
    xa2_shutdown();
    /* Failed initialization still runs on the COM-initializing thread. */
    if (com_initialized) CoUninitialize();
    return 0;
}

void xa2_shutdown(void)
{
    if (g_xa2_source) {
        IXAudio2SourceVoice_Stop(g_xa2_source, 0, XAUDIO2_COMMIT_NOW);
        IXAudio2SourceVoice_FlushSourceBuffers(g_xa2_source);
        g_xa2_source->lpVtbl->DestroyVoice(g_xa2_source);
        g_xa2_source = NULL;
    }
    if (g_xa2_master) {
        g_xa2_master->lpVtbl->DestroyVoice(g_xa2_master);
        g_xa2_master = NULL;
    }
    if (g_xa2) {
        IXAudio2_Release(g_xa2);
        g_xa2 = NULL;
    }

    if (g_xa2_initialized)
        fprintf(stderr, "[XA2] Shut down (%d frames written)\n", g_xa2_frames_written);
    g_xa2_initialized = 0;
}

int xa2_is_active(void)
{
    return g_xa2_initialized;
}

/* Submit a buffer of mixed samples to XAudio2.
 * Called from APU frame thread. Returns 1 if buffer was submitted. */
int xa2_submit_samples(const int16_t *samples, int num_samples)
{
    XAUDIO2_VOICE_STATE state;
    XAUDIO2_BUFFER xbuf;
    int idx;
    int copy_samples;
    HRESULT hr;

    if (!g_xa2_initialized || !g_xa2_source) return 0;

    IXAudio2SourceVoice_GetState(g_xa2_source, &state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    if ((int)state.BuffersQueued >= XA2_NUM_BUFS) return 0;

    idx = g_xa2_next_buf;
    copy_samples = (num_samples > XA2_BUF_SAMPLES) ? XA2_BUF_SAMPLES : num_samples;
    memcpy(g_xa2_bufs[idx], samples, copy_samples * g_xa2_channels * sizeof(int16_t));

    memset(&xbuf, 0, sizeof(xbuf));
    xbuf.AudioBytes = copy_samples * g_xa2_channels * sizeof(int16_t);
    xbuf.pAudioData = (const BYTE *)g_xa2_bufs[idx];

    hr = IXAudio2SourceVoice_SubmitSourceBuffer(g_xa2_source, &xbuf, NULL);
    if (FAILED(hr)) return 0;

    g_xa2_next_buf = (idx + 1) % XA2_NUM_BUFS;
    g_xa2_frames_written++;
    return 1;
}

int xa2_get_buffer_size(void)
{
    return XA2_BUF_SAMPLES;
}

int xa2_buffers_queued(void)
{
    XAUDIO2_VOICE_STATE state;

    if (!g_xa2_initialized || !g_xa2_source) return 0;
    IXAudio2SourceVoice_GetState(g_xa2_source, &state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return (int)state.BuffersQueued;
}

int xa2_max_buffers(void)
{
    return XA2_NUM_BUFS;
}

int xa2_channels(void)
{
    return g_xa2_initialized ? g_xa2_channels : 0;
}

int xa2_device_channels(void)
{
    return g_xa2_device_channels;
}

#else /* !_WIN32 -- POSIX stubs (no audio output yet) */

int  xa2_init(void)                                   { return 0; }
void xa2_shutdown(void)                               {}
int  xa2_is_active(void)                              { return 0; }
int  xa2_submit_samples(const int16_t *s, int n)      { (void)s; (void)n; return 0; }
int  xa2_get_buffer_size(void)                        { return 0; }
int  xa2_buffers_queued(void)                         { return 0; }
int  xa2_max_buffers(void)                            { return 0; }
int  xa2_channels(void)                               { return 0; }
int  xa2_device_channels(void)                        { return 0; }

#endif /* _WIN32 */
