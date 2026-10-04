// chantone: a 1 kHz tone in each output channel in turn, 0.8 s on / 0.4 s off.
//   chantone xa2   - XAudio2, 6-channel mastering + source voice (5.1)
//   chantone sac   - ISpatialAudioClient static objects FL FR FC LFE BL BR (side if no back)
#define INITGUID
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <spatialaudioclient.h>
#include <xaudio2.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "xaudio2.lib")

static const int RATE = 48000, ON = 38400, OFF = 19200;

static int run_xa2()
{
    IXAudio2 *xa = NULL;
    IXAudio2MasteringVoice *m = NULL;
    IXAudio2SourceVoice *s = NULL;
    if (FAILED(XAudio2Create(&xa, 0, XAUDIO2_DEFAULT_PROCESSOR)) ||
        FAILED(xa->CreateMasteringVoice(&m, 6, RATE, 0, NULL, NULL, AudioCategory_GameEffects)))
        return 1;
    WAVEFORMATEX w = {};
    w.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    w.nChannels = 6;
    w.nSamplesPerSec = RATE;
    w.wBitsPerSample = 32;
    w.nBlockAlign = 24;
    w.nAvgBytesPerSec = RATE * 24;
    if (FAILED(xa->CreateSourceVoice(&s, &w)))
        return 2;
    float mat[36] = {};
    for (int i = 0; i < 6; i++)
        mat[i * 6 + i] = 1.0f;
    s->SetOutputMatrix(m, 6, 6, mat);
    static std::vector<float> buf((ON + OFF) * 6 * 6);
    for (int c = 0; c < 6; c++)
        for (int i = 0; i < ON; i++)
            buf[((c * (ON + OFF)) + i) * 6 + c] = 0.25f * sinf(6.2831853f * 1000.0f * i / RATE);
    XAUDIO2_BUFFER b = {};
    b.AudioBytes = (UINT32)(buf.size() * 4);
    b.pAudioData = (const BYTE *)buf.data();
    b.Flags = XAUDIO2_END_OF_STREAM;
    s->SubmitSourceBuffer(&b);
    s->Start();
    Sleep(6 * 1200 + 300);
    return 0;
}

static int run_sac()
{
    IMMDeviceEnumerator *en = NULL;
    IMMDevice *dev = NULL;
    ISpatialAudioClient *sac = NULL;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void **)&en);
    en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    if (FAILED(dev->Activate(__uuidof(ISpatialAudioClient), CLSCTX_INPROC_SERVER, NULL, (void **)&sac)))
        return 1;
    AudioObjectType types[6] = { AudioObjectType_FrontLeft, AudioObjectType_FrontRight, AudioObjectType_FrontCenter,
                                 AudioObjectType_LowFrequency, AudioObjectType_BackLeft, AudioObjectType_BackRight };
    AudioObjectType mask = AudioObjectType_None;
    sac->GetNativeStaticObjectTypeMask(&mask);
    if (!(mask & AudioObjectType_BackLeft)) {
        types[4] = AudioObjectType_SideLeft;
        types[5] = AudioObjectType_SideRight;
    }
    AudioObjectType want = (AudioObjectType)0;
    for (int i = 0; i < 6; i++)
        want = (AudioObjectType)(want | types[i]);
    WAVEFORMATEX w = {};
    w.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    w.nChannels = 1;
    w.nSamplesPerSec = RATE;
    w.wBitsPerSample = 32;
    w.nBlockAlign = 4;
    w.nAvgBytesPerSec = RATE * 4;
    HANDLE ev = CreateEvent(NULL, FALSE, FALSE, NULL);
    SpatialAudioObjectRenderStreamActivationParams p = {};
    p.ObjectFormat = &w;
    p.StaticObjectTypeMask = want;
    p.MinDynamicObjectCount = 0;
    p.MaxDynamicObjectCount = 0;
    p.Category = AudioCategory_GameEffects;
    p.EventHandle = ev;
    PROPVARIANT pv;
    PropVariantInit(&pv);
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(p);
    pv.blob.pBlobData = (BYTE *)&p;
    ISpatialAudioObjectRenderStream *rs = NULL;
    HRESULT hr = sac->ActivateSpatialAudioStream(&pv, __uuidof(ISpatialAudioObjectRenderStream), (void **)&rs);
    if (FAILED(hr)) {
        printf("ActivateSpatialAudioStream 0x%08lX\n", (unsigned long)hr);
        return 2;
    }
    ISpatialAudioObject *obj[6] = {};
    for (int i = 0; i < 6; i++)
        if (FAILED(rs->ActivateSpatialAudioObject(types[i], &obj[i])))
            printf("object %d not activated\n", i);
    rs->Start();
    long long pos = 0, total = 6LL * (ON + OFF);
    while (pos < total) {
        WaitForSingleObject(ev, 100);
        UINT32 dyn = 0, frames = 0;
        if (FAILED(rs->BeginUpdatingAudioObjects(&dyn, &frames)))
            break;
        for (int c = 0; c < 6; c++) {
            BYTE *b = NULL;
            UINT32 len = 0;
            if (!obj[c] || FAILED(obj[c]->GetBuffer(&b, &len)))
                continue;
            float *f = (float *)b;
            for (UINT32 i = 0; i < frames; i++) {
                long long t = pos + i, seg = t / (ON + OFF), in = t % (ON + OFF);
                f[i] = (seg == c && in < ON) ? 0.25f * sinf(6.2831853f * 1000.0f * in / RATE) : 0.0f;
            }
        }
        rs->EndUpdatingAudioObjects();
        pos += frames;
    }
    Sleep(300);
    rs->Stop();
    return 0;
}

int main(int argc, char **argv)
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    int r = (argc > 1 && !strcmp(argv[1], "sac")) ? run_sac() : run_xa2();
    printf("done %d\n", r);
    return r;
}
