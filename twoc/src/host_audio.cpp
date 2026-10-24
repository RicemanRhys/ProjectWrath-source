/**
 * The game's volume (step 54, notes 18.57): the level of this process's audio
 * session, the one Windows' volume mixer shows for the game. XAudio2 (the
 * APU monitor's output, xboxrecomp) plays into that session, so this scales
 * everything the game plays without touching the emulated APU. Music and
 * effects can't be told apart here (the APU mixes them into one stream), so
 * their volumes are gains inside the APU (twoc_audio_set_kind, below).
 *
 * Windows remembers a session's level for the next start, as it does when
 * the player moves the mixer's slider. So the level is set only when the
 * player asked (RECOMP_VOLUME, the volume key of crash_twoc.ini, or the
 * overlay), and a run without either leaves Windows' level alone.
 *
 * The overlay also mutes the session while it holds the game: the APU keeps
 * playing looped voices (H2 Oh No's hum droned on). In case the process ends
 * while muted, crash_twoc.muted next to the exe marks the mute as ours, and
 * the next start (twoc_audio_startup) lifts it; a mute the player set in the
 * mixer is never touched.
 *
 * One worker thread with its own COM apartment applies the latest level and
 * mute asked for; the game's threads never call COM.
 */

#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <stdio.h>
#include <string.h>

#include "settings.h"
#include "setup.h"

static volatile LONG g_want = -1;       /* the latest level asked for, -1: none pending */
static volatile LONG g_want_mute = -1;  /* 1 mute, 0 unmute, -1 none pending */
static HANDLE g_wake;

static const char *marker_path(void)
{
    static char path[MAX_PATH * 4];

    /* step 107: beside the exe, or in the data folder when the exe's folder can't be written to (setup.h) */
    if (!path[0] && twoc_settings_file("crash_twoc.muted", path, sizeof(path)))
        snprintf(path, sizeof(path), "crash_twoc.muted");
    return path;
}

/* This process's session volume on the default output, or NULL. */
static ISimpleAudioVolume *session(HRESULT *phr)
{
    IMMDeviceEnumerator *en = NULL;
    IMMDevice *dev = NULL;
    IAudioSessionManager *sm = NULL;
    ISimpleAudioVolume *vol = NULL;
    HRESULT hr;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                          (void **)&en);
    if (SUCCEEDED(hr))
        hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    if (SUCCEEDED(hr))
        hr = dev->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, NULL, (void **)&sm);
    if (SUCCEEDED(hr))
        hr = sm->GetSimpleAudioVolume(NULL, FALSE, &vol);
    if (sm)
        sm->Release();
    if (dev)
        dev->Release();
    if (en)
        en->Release();
    *phr = hr;
    return SUCCEEDED(hr) ? vol : NULL;
}

static DWORD WINAPI worker(LPVOID)
{
    int told = 0;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    for (;;) {
        LONG v, m;
        WaitForSingleObject(g_wake, INFINITE);
        for (;;) {
            HRESULT hr = S_OK;
            ISimpleAudioVolume *vol;
            v = InterlockedExchange(&g_want, -1);
            m = InterlockedExchange(&g_want_mute, -1);
            if (v < 0 && m < 0)
                break;
            vol = session(&hr);
            if (vol && v >= 0)
                hr = vol->SetMasterVolume((float)v / 100.0f, NULL);
            if (v >= 0 && (FAILED(hr) || !told++))  /* the first level and any failure; not every slider step */
                fprintf(stderr, "[AUDIO] game volume %ld%%%s (0x%08lX)\n", v, FAILED(hr) ? " not set" : "",
                        (unsigned long)hr);
            if (vol && m >= 0) {
                if (m) {                /* the marker first: a mute is never left unmarked */
                    FILE *f = fopen(marker_path(), "w");
                    if (f) {
                        fputs("The settings overlay muted the game; the next start unmutes it.\n", f);
                        fclose(f);
                    }
                }
                hr = vol->SetMute(m ? TRUE : FALSE, NULL);
                if (!m && SUCCEEDED(hr))
                    DeleteFileA(marker_path());
                if (FAILED(hr))
                    fprintf(stderr, "[AUDIO] game %s failed (0x%08lX)\n", m ? "mute" : "unmute", (unsigned long)hr);
            }
            if (vol)
                vol->Release();
        }
    }
}

static void wake(void)
{
    static volatile LONG started;

    if (!InterlockedExchange(&started, 1)) {
        HANDLE th;
        g_wake = CreateEventA(NULL, FALSE, FALSE, NULL);
        th = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        if (th)
            CloseHandle(th);
    }
    while (!g_wake)
        Sleep(0);
    SetEvent(g_wake);
}

extern "C" void twoc_audio_set_volume(int percent)
{
    if (percent < 0)
        return;
    InterlockedExchange(&g_want, percent > 100 ? 100 : percent);
    wake();
}

extern "C" void twoc_audio_mute(int on)
{
    InterlockedExchange(&g_want_mute, on ? 1 : 0);
    wake();
}

/* Music and effects apart (notes 18.58): a gain in the APU's voice
 * processor (xboxrecomp apu_vp.c) on stream voices (music, ambience, speech)
 * or buffer voices (effects), linear like the session level above. A plain
 * store: the APU's frame thread reads it at its next voice. */
extern "C" void mcpx_apu_set_voice_gain(int stream, float gain);

extern "C" void twoc_audio_set_kind(int stream, int percent)
{
    static int told[2];
    int p = percent < 0 ? 100 : percent > 100 ? 100 : percent;

    mcpx_apu_set_voice_gain(stream, (float)p / 100.0f);
    if (!told[stream ? 1 : 0]++)        /* the first; not every slider step */
        fprintf(stderr, "[AUDIO] %s volume %d%%\n", stream ? "music" : "effects", p);
}

/* 3D placement (notes 18.59): the APU's HRTF filter on 3D voices, a plain
 * store like the gains above. */
extern "C" void mcpx_apu_set_hrtf(int on);

extern "C" void twoc_audio_set_hrtf(int on)
{
    mcpx_apu_set_hrtf(on);
    fprintf(stderr, "[AUDIO] 3D placement (HRTF) %s\n", on ? "on" : "off");
}

/* Step 97: the game quitting from the menu (Quit game, Restart now, restart.c): the APU's voices silenced at
 * its next frames, a moment for XAudio2's queue (a few buffers of 256 samples) to play that out, and a mute
 * the menu left (its marker) lifted on this thread, so the process ends without a click and Windows keeps
 * no mute of ours. */
extern "C" void twoc_audio_close(void)
{
    mcpx_apu_set_voice_gain(1, 0.0f);
    mcpx_apu_set_voice_gain(0, 0.0f);
    Sleep(100);
    if (GetFileAttributesA(marker_path()) != INVALID_FILE_ATTRIBUTES) {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        ISimpleAudioVolume *vol = session(&hr);
        if (vol) {
            if (SUCCEEDED(vol->SetMute(FALSE, NULL)))
                DeleteFileA(marker_path());
            vol->Release();
        }
    }
}

extern "C" void twoc_audio_startup(void)
{
    if (GetFileAttributesA(marker_path()) == INVALID_FILE_ATTRIBUTES)
        return;                         /* no mute of ours left over: nothing touched */
    fprintf(stderr, "[AUDIO] the last run ended with the overlay open: unmuting the game\n");
    twoc_audio_mute(0);
}
