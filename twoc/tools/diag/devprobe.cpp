// devprobe: what the default render device accepts (5.1 / 7.1 in shared mode,
// spatial audio, XAudio2's default mastering voice).
#define INITGUID
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <spatialaudioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmreg.h>
#include <ksmedia.h>
#include <xaudio2.h>
#include <stdio.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "xaudio2.lib")

static void try_fmt(IAudioClient *ac, int ch, DWORD mask, const char *name)
{
    WAVEFORMATEXTENSIBLE w = {};
    w.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    w.Format.nChannels = (WORD)ch;
    w.Format.nSamplesPerSec = 48000;
    w.Format.wBitsPerSample = 32;
    w.Format.nBlockAlign = (WORD)(ch * 4);
    w.Format.nAvgBytesPerSec = 48000 * ch * 4;
    w.Format.cbSize = 22;
    w.Samples.wValidBitsPerSample = 32;
    w.dwChannelMask = mask;
    w.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    WAVEFORMATEX *close = NULL;
    HRESULT hr = ac->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &w.Format, &close);
    printf("  shared %-22s: hr 0x%08lX%s", name, (unsigned long)hr, hr == S_OK ? " (accepted)" : "");
    if (close)
        printf(", closest %u ch", close->nChannels);
    printf("\n");
    if (close)
        CoTaskMemFree(close);
}

int main()
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IMMDeviceEnumerator *en = NULL;
    IMMDevice *dev = NULL;
    IAudioClient *ac = NULL;
    IPropertyStore *ps = NULL;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                (void **)&en)) ||
        FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) {
        printf("no default device\n");
        return 1;
    }
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps))) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
            printf("device: %ls\n", v.pwszVal);
        PropVariantClear(&v);
        if (SUCCEEDED(ps->GetValue(PKEY_AudioEndpoint_PhysicalSpeakers, &v)))
            printf("physical speakers mask: 0x%lX (vt %u)\n", (unsigned long)v.ulVal, v.vt);
        PropVariantClear(&v);
        if (SUCCEEDED(ps->GetValue(PKEY_AudioEngine_DeviceFormat, &v)) && v.vt == VT_BLOB) {
            WAVEFORMATEX *f = (WAVEFORMATEX *)v.blob.pBlobData;
            printf("device format: %u ch, %u Hz, %u bits", f->nChannels, f->nSamplesPerSec, f->wBitsPerSample);
            if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
                printf(", mask 0x%lX", (unsigned long)((WAVEFORMATEXTENSIBLE *)f)->dwChannelMask);
            printf("\n");
        }
        PropVariantClear(&v);
    }
    if (SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void **)&ac))) {
        WAVEFORMATEX *mf = NULL;
        ac->GetMixFormat(&mf);
        printf("mix format: %u ch, %u Hz", mf->nChannels, mf->nSamplesPerSec);
        if (mf->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
            printf(", mask 0x%lX", (unsigned long)((WAVEFORMATEXTENSIBLE *)mf)->dwChannelMask);
        printf("\n");
        try_fmt(ac, 2, KSAUDIO_SPEAKER_STEREO, "stereo");
        try_fmt(ac, 6, KSAUDIO_SPEAKER_5POINT1, "5.1 (back)");
        try_fmt(ac, 6, KSAUDIO_SPEAKER_5POINT1_SURROUND, "5.1 (side)");
        try_fmt(ac, 8, KSAUDIO_SPEAKER_7POINT1_SURROUND, "7.1");
        CoTaskMemFree(mf);
    }
    ISpatialAudioClient *sac = NULL;
    HRESULT hr = dev->Activate(__uuidof(ISpatialAudioClient), CLSCTX_INPROC_SERVER, NULL, (void **)&sac);
    printf("spatial audio client: hr 0x%08lX\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        AudioObjectType mask = AudioObjectType_None;
        UINT32 maxdyn = 0;
        sac->GetNativeStaticObjectTypeMask(&mask);
        sac->GetMaxDynamicObjectCount(&maxdyn);
        printf("  native static object mask 0x%X, max dynamic objects %u\n", (unsigned)mask, maxdyn);
        IAudioFormatEnumerator *fe = NULL;
        if (SUCCEEDED(sac->GetSupportedAudioObjectFormatEnumerator(&fe))) {
            WAVEFORMATEX *f = NULL;
            if (SUCCEEDED(fe->GetFormat(0, &f)))
                printf("  object format: %u ch, %u Hz, %u bits\n", f->nChannels, f->nSamplesPerSec, f->wBitsPerSample);
            fe->Release();
        }
        sac->Release();
    }
    IXAudio2 *xa = NULL;
    if (SUCCEEDED(XAudio2Create(&xa, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
        IXAudio2MasteringVoice *m = NULL;
        hr = xa->CreateMasteringVoice(&m, XAUDIO2_DEFAULT_CHANNELS, 48000, 0, NULL, NULL, AudioCategory_GameEffects);
        if (SUCCEEDED(hr)) {
            XAUDIO2_VOICE_DETAILS d;
            DWORD cm = 0;
            m->GetVoiceDetails(&d);
            m->GetChannelMask(&cm);
            printf("XAudio2 default mastering voice: %u ch, mask 0x%lX\n", d.InputChannels, (unsigned long)cm);
            m->DestroyVoice();
        } else
            printf("XAudio2 default mastering voice failed 0x%08lX\n", (unsigned long)hr);
        hr = xa->CreateMasteringVoice(&m, 6, 48000, 0, NULL, NULL, AudioCategory_GameEffects);
        if (SUCCEEDED(hr)) {
            DWORD cm = 0;
            m->GetChannelMask(&cm);
            printf("XAudio2 6-channel mastering voice: created, mask 0x%lX\n", (unsigned long)cm);
            m->DestroyVoice();
        } else
            printf("XAudio2 6-channel mastering voice failed 0x%08lX\n", (unsigned long)hr);
        xa->Release();
    }
    return 0;
}
