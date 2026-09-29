// What a window shows at each desktop frame the display gets, from DXGI Desktop
// Duplication: every frame DWM presents to the monitor comes with its present
// time (LastPresentTime, QPC), and a hash of the window's area of it says which
// picture was on screen. A picture held for 4 refreshes at 240 Hz is 60 fps
// shown evenly; 5 then 3 is PresentMon's 20.8 / 12.5 ms. Needs no elevation.
//
//   ddtimes.exe "Window title" OUT.txt SECONDS [--dump=FROM_S,N]
//
// --dump=FROM_S,N also saves the window's area of N consecutive desktop frames
// from FROM_S seconds on, as OUT.txt_NNNNN.bmp.
//
// Writes one line per desktop frame: present time in ms (QPC clock), frames the
// duplication accumulated (more than 1: some weren't seen), and the hash of the
// window's client area (every 4th pixel of every 4th row). Summarise with
// ddsum.py. Build with ddtimes_build.bat.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <cstdint>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")

static void save_bmp(const char *path, const uint8_t *bgra, unsigned pitch, unsigned w, unsigned h)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    unsigned rowbytes = w * 3, pad = (4 - (rowbytes & 3)) & 3, size = 54 + (rowbytes + pad) * h;
    uint8_t hdr[54] = { 'B', 'M' };
    memcpy(hdr + 2, &size, 4);
    hdr[10] = 54;
    hdr[14] = 40;
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    hdr[26] = 1;
    hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    std::vector<uint8_t> row(rowbytes + pad);
    for (unsigned y = h; y-- > 0;) {
        const uint8_t *s = bgra + (size_t)y * pitch;
        for (unsigned x = 0; x < w; x++) {
            row[3 * x] = s[4 * x];
            row[3 * x + 1] = s[4 * x + 1];
            row[3 * x + 2] = s[4 * x + 2];
        }
        fwrite(row.data(), 1, row.size(), f);
    }
    fclose(f);
}

template <class T> static void rel(T *&p) { if (p) { p->Release(); p = nullptr; } }

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: ddtimes \"title\" OUT.txt SECONDS\n");
        return 2;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND hwnd = NULL;
    for (int i = 0; i < 1200 && !hwnd; i++) {
        hwnd = FindWindowA(NULL, argv[1]);
        if (!hwnd)
            Sleep(100);
    }
    if (!hwnd) {
        printf("window not found\n");
        return 1;
    }
    double secs = atof(argv[3]);
    double dump_from = -1;
    int dump_n = 0, dumped = 0;
    for (int i = 4; i < argc; i++)
        if (strncmp(argv[i], "--dump=", 7) == 0)
            sscanf(argv[i] + 7, "%lf,%d", &dump_from, &dump_n);
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

    IDXGIFactory1 *fac = nullptr;
    CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&fac);
    IDXGIAdapter1 *ad = nullptr;
    IDXGIOutput *out = nullptr;
    DXGI_OUTPUT_DESC od = {};
    for (UINT a = 0; !out && fac->EnumAdapters1(a, &ad) != DXGI_ERROR_NOT_FOUND; a++) {
        for (UINT o = 0; ad->EnumOutputs(o, &out) != DXGI_ERROR_NOT_FOUND; o++) {
            out->GetDesc(&od);
            if (od.Monitor == mon)
                break;
            rel(out);
        }
        if (!out)
            rel(ad);
    }
    if (!out) {
        printf("no output for the window's monitor\n");
        return 1;
    }
    ID3D11Device *dev = nullptr;
    ID3D11DeviceContext *ctx = nullptr;
    if (FAILED(D3D11CreateDevice(ad, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                 nullptr, &ctx))) {
        printf("D3D11CreateDevice failed\n");
        return 1;
    }
    IDXGIOutput1 *out1 = nullptr;
    out->QueryInterface(__uuidof(IDXGIOutput1), (void **)&out1);
    IDXGIOutputDuplication *dup = nullptr;
    HRESULT hr = out1->DuplicateOutput(dev, &dup);
    if (FAILED(hr)) {
        printf("DuplicateOutput failed 0x%08lX\n", (unsigned long)hr);
        return 1;
    }

    struct row { LONGLONG qpc; UINT acc; unsigned long long h; };
    std::vector<row> rows;
    rows.reserve(1 << 17);
    ID3D11Texture2D *st = nullptr;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    ULONGLONG t0 = GetTickCount64();
    printf("duplicating output %ls for %.0f s\n", od.DeviceName, secs);
    fflush(stdout);
    while ((GetTickCount64() - t0) / 1000.0 < secs && IsWindow(hwnd)) {
        DXGI_OUTDUPL_FRAME_INFO fi;
        IDXGIResource *res = nullptr;
        hr = dup->AcquireNextFrame(100, &fi, &res);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
            continue;
        if (FAILED(hr)) {
            printf("AcquireNextFrame 0x%08lX\n", (unsigned long)hr);
            break;
        }
        if (fi.LastPresentTime.QuadPart) {
            ID3D11Texture2D *tex = nullptr;
            res->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&tex);
            RECT rc;
            POINT p0 = { 0, 0 };
            GetClientRect(hwnd, &rc);
            ClientToScreen(hwnd, &p0);
            LONG x0 = p0.x - od.DesktopCoordinates.left, y0 = p0.y - od.DesktopCoordinates.top;
            LONG w = rc.right, hgt = rc.bottom;
            D3D11_TEXTURE2D_DESC d;
            tex->GetDesc(&d);
            if (x0 < 0) { w += x0; x0 = 0; }
            if (y0 < 0) { hgt += y0; y0 = 0; }
            if (x0 + w > (LONG)d.Width) w = d.Width - x0;
            if (y0 + hgt > (LONG)d.Height) hgt = d.Height - y0;
            unsigned long long h = 0;
            if (w > 0 && hgt > 0) {
                D3D11_TEXTURE2D_DESC sd;
                if (st)
                    st->GetDesc(&sd);
                if (!st || sd.Width != (UINT)w || sd.Height != (UINT)hgt) {
                    rel(st);
                    sd = d;
                    sd.Width = w;
                    sd.Height = hgt;
                    sd.MipLevels = 1;
                    sd.ArraySize = 1;
                    sd.Usage = D3D11_USAGE_STAGING;
                    sd.BindFlags = 0;
                    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                    sd.MiscFlags = 0;
                    dev->CreateTexture2D(&sd, nullptr, &st);
                }
                D3D11_BOX box = { (UINT)x0, (UINT)y0, 0, (UINT)(x0 + w), (UINT)(y0 + hgt), 1 };
                ctx->CopySubresourceRegion(st, 0, 0, 0, 0, tex, 0, &box);
                D3D11_MAPPED_SUBRESOURCE m;
                if (SUCCEEDED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
                    h = 1469598103934665603ull;
                    for (LONG y = 0; y < hgt; y += 4) {
                        const uint32_t *px = (const uint32_t *)((const uint8_t *)m.pData + (size_t)y * m.RowPitch);
                        for (LONG x = 0; x < w; x += 4)
                            h = (h ^ px[x]) * 1099511628211ull;
                    }
                    if (dump_from >= 0 && dumped < dump_n && (GetTickCount64() - t0) / 1000.0 >= dump_from) {
                        char path[1024];
                        snprintf(path, sizeof(path), "%s_%05d.bmp", argv[2], (int)rows.size());
                        save_bmp(path, (const uint8_t *)m.pData, m.RowPitch, w, hgt);
                        dumped++;
                    }
                    ctx->Unmap(st, 0);
                }
            }
            rel(tex);
            rows.push_back({ fi.LastPresentTime.QuadPart, fi.AccumulatedFrames, h });
        }
        rel(res);
        dup->ReleaseFrame();
    }
    FILE *fo = fopen(argv[2], "w");
    for (auto &r : rows)
        fprintf(fo, "%.4f %u %016llx\n", r.qpc * 1000.0 / f.QuadPart, r.acc, r.h);
    fclose(fo);
    printf("%zu desktop frames\n", rows.size());
    return 0;
}
