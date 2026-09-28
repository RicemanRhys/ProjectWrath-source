// loopcap.exe OUT.wav SECONDS [--wait-proc=NAME]
//
// Records what the default render device plays (WASAPI loopback) into a 16-bit PCM WAV, and prints the
// level of each second (RMS and peak in dBFS, per channel). Step 42 (notes 18.44): the game's audio
// reaches the device through XAudio2, so this hears exactly that path, plus anything else playing.
// --wait-proc waits for a process of that name before recording starts (at most 120 s).
// Build with loopcap_build.bat (Visual Studio 2022's vcvars64).
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <tlhelp32.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static bool proc_running(const char *name)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    bool found = false;
    if (snap == INVALID_HANDLE_VALUE)
        return false;
    for (BOOL ok = Process32First(snap, &pe); ok && !found; ok = Process32Next(snap, &pe))
        found = _stricmp(pe.szExeFile, name) == 0;
    CloseHandle(snap);
    return found;
}

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: loopcap OUT.wav SECONDS [--wait-proc=NAME]\n");
        return 2;
    }
    const char *out = argv[1];
    double secs = atof(argv[2]);
    for (int i = 3; i < argc; i++)
        if (!strncmp(argv[i], "--wait-proc=", 12)) {
            for (int t = 0; t < 1200 && !proc_running(argv[i] + 12); t++)
                Sleep(100);
            if (!proc_running(argv[i] + 12)) {
                fprintf(stderr, "no process %s\n", argv[i] + 12);
                return 1;
            }
        }

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IMMDeviceEnumerator *en = NULL;
    IMMDevice *dev = NULL;
    IAudioClient *ac = NULL;
    IAudioCaptureClient *cap = NULL;
    WAVEFORMATEX *wf = NULL;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                (void **)&en)) ||
        FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) ||
        FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void **)&ac)) ||
        FAILED(ac->GetMixFormat(&wf)) ||
        FAILED(ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, wf, NULL)) ||
        FAILED(ac->GetService(__uuidof(IAudioCaptureClient), (void **)&cap))) {
        fprintf(stderr, "WASAPI loopback setup failed\n");
        return 1;
    }
    bool is_float = wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                    (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wf->wBitsPerSample == 32 &&
                     ((WAVEFORMATEXTENSIBLE *)wf)->SubFormat.Data1 == 3);
    unsigned ch = wf->nChannels, rate = wf->nSamplesPerSec;
    printf("device mix format: %u Hz, %u channels, %u bits%s\n", rate, ch, wf->wBitsPerSample, is_float ? " float" : "");

    FILE *f = fopen(out, "wb");
    if (!f) {
        fprintf(stderr, "can't write %s\n", out);
        return 1;
    }
    fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, f);
    put32(f, 16); put16(f, 1); put16(f, (uint16_t)ch); put32(f, rate); put32(f, rate * ch * 2);
    put16(f, (uint16_t)(ch * 2)); put16(f, 16);
    fwrite("data\0\0\0\0", 1, 8, f);

    ac->Start();
    uint64_t frames = 0, want = (uint64_t)(secs * rate), sec_frames = 0;
    std::vector<double> sum(ch), peak(ch);
    unsigned sec = 0;
    ULONGLONG t0 = GetTickCount64();
    while (frames < want) {
        Sleep(10);
        UINT32 n = 0;
        while (SUCCEEDED(cap->GetNextPacketSize(&n)) && n) {
            BYTE *data;
            DWORD flags;
            if (FAILED(cap->GetBuffer(&data, &n, &flags, NULL, NULL)))
                break;
            for (UINT32 i = 0; i < n; i++) {
                for (unsigned c = 0; c < ch; c++) {
                    double x = 0;
                    if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT))
                        x = is_float ? ((float *)data)[i * ch + c] : ((int16_t *)data)[i * ch + c] / 32768.0;
                    int16_t s = (int16_t)std::lround(std::fmax(-1.0, std::fmin(1.0, x)) * 32767.0);
                    fwrite(&s, 2, 1, f);
                    sum[c] += x * x;
                    peak[c] = std::fmax(peak[c], std::fabs(x));
                }
                if (++sec_frames == rate) {
                    printf("%3u s:", sec);
                    for (unsigned c = 0; c < ch && c < 8; c++)
                        printf("  ch%u rms %6.1f peak %6.1f", c, 10 * log10(sum[c] / rate + 1e-12),
                               20 * log10(peak[c] + 1e-12));
                    printf("\n");
                    fflush(stdout);
                    sec++;
                    sec_frames = 0;
                    for (unsigned c = 0; c < ch; c++)
                        sum[c] = peak[c] = 0;
                }
            }
            frames += n;
            cap->ReleaseBuffer(n);
        }
        // Loopback delivers nothing while the device is idle: count that as silence by the clock.
        uint64_t by_clock = (GetTickCount64() - t0) * rate / 1000;
        if (by_clock > frames + rate / 2) {
            uint64_t gap = by_clock - frames;
            int16_t z = 0;
            for (uint64_t i = 0; i < gap * ch; i++)
                fwrite(&z, 2, 1, f);
            frames += gap;
        }
    }
    ac->Stop();
    long end = ftell(f);
    fseek(f, 4, SEEK_SET);
    put32(f, (uint32_t)(end - 8));
    fseek(f, 40, SEEK_SET);
    put32(f, (uint32_t)(end - 44));
    fclose(f);
    printf("wrote %s: %.1f s\n", out, frames / (double)rate);
    return 0;
}
