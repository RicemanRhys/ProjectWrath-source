// Grab a window's own composited content with Windows Graphics Capture, which
// works while other windows cover it (not while it is minimised).
//
//   wgcgrab.exe "Window title" OUTPREFIX T1 T2 ...
//
// Waits up to 120 s for the window, then writes OUTPREFIX_<T>s.bmp for each
// time T (seconds after the window was found), and prints the game's frame
// counter (guest 0x25A660, host +0x10000) read around each grab.
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <inspectable.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <atomic>
#include <mutex>

using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

extern "C" HRESULT __stdcall CreateDirect3D11DeviceFromDXGIDevice(::IDXGIDevice *, ::IInspectable **);

static HANDLE open_game()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe = { sizeof(pe) };
    HANDLE h = NULL;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (_wcsicmp(pe.szExeFile, L"crash_twoc.exe") == 0) {
            h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pe.th32ProcessID);
            break;
        }
    CloseHandle(snap);
    return h;
}

static long frame_counter(HANDLE h)
{
    unsigned v = 0;
    SIZE_T got = 0;
    if (!h || !ReadProcessMemory(h, (LPCVOID)(uintptr_t)(0x25A660 + 0x10000), &v, 4, &got))
        return -1;
    return (long)v;
}

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

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: wgcgrab \"title\" OUTPREFIX T1 [T2 ...]\n");
        return 2;
    }
    init_apartment(apartment_type::multi_threaded);
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
    ULONGLONG t0 = GetTickCount64();
    HANDLE game = open_game();

    com_ptr<ID3D11Device> dev;
    com_ptr<ID3D11DeviceContext> ctx;
    check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                    nullptr, 0, D3D11_SDK_VERSION, dev.put(), nullptr, ctx.put()));
    com_ptr<IDXGIDevice> dxgi = dev.as<IDXGIDevice>();
    com_ptr<::IInspectable> insp;
    check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), insp.put()));
    IDirect3DDevice d3d = insp.as<IDirect3DDevice>();

    auto interop = get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{ nullptr };
    check_hresult(interop->CreateForWindow(hwnd, guid_of<GraphicsCaptureItem>(), put_abi(item)));
    auto size = item.Size();
    auto pool = Direct3D11CaptureFramePool::CreateFreeThreaded(d3d, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
    auto session = pool.CreateCaptureSession(item);
    try { session.IsCursorCaptureEnabled(false); } catch (...) {}
    try { session.IsBorderRequired(false); } catch (...) {}

    std::mutex mu;
    com_ptr<ID3D11Texture2D> latest;
    std::atomic<long> frames{ 0 };
    pool.FrameArrived([&](Direct3D11CaptureFramePool const &p, auto const &) {
        auto frame = p.TryGetNextFrame();
        if (!frame)
            return;
        auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        com_ptr<ID3D11Texture2D> tex;
        access->GetInterface(guid_of<ID3D11Texture2D>(), tex.put_void());
        D3D11_TEXTURE2D_DESC d;
        tex->GetDesc(&d);
        d.Usage = D3D11_USAGE_STAGING;
        d.BindFlags = 0;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        d.MiscFlags = 0;
        com_ptr<ID3D11Texture2D> st;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, st.put())))
            return;
        std::lock_guard<std::mutex> lock(mu);
        ctx->CopyResource(st.get(), tex.get());
        latest = st;
        frames++;
    });
    session.StartCapture();
    printf("capturing window %p, %dx%d\n", (void *)hwnd, size.Width, size.Height);
    fflush(stdout);

    for (int i = 3; i < argc; i++) {
        double t = atof(argv[i]);
        while ((GetTickCount64() - t0) / 1000.0 < t)
            Sleep(10);
        if (!IsWindow(hwnd)) {
            printf("window gone\n");
            break;
        }
        long f0 = frame_counter(game);
        long before = frames.load();
        for (int k = 0; k < 50 && frames.load() == before; k++)   /* a frame newer than the request */
            Sleep(5);
        com_ptr<ID3D11Texture2D> st;
        {
            std::lock_guard<std::mutex> lock(mu);
            st = latest;
        }
        long f1 = frame_counter(game);
        if (!st) {
            printf("%.1f s: no frame yet\n", (GetTickCount64() - t0) / 1000.0);
            continue;
        }
        D3D11_TEXTURE2D_DESC d;
        st->GetDesc(&d);
        D3D11_MAPPED_SUBRESOURCE m;
        std::lock_guard<std::mutex> lock(mu);
        if (SUCCEEDED(ctx->Map(st.get(), 0, D3D11_MAP_READ, 0, &m))) {
            char path[1024];
            snprintf(path, sizeof(path), "%s_%05.1fs.bmp", argv[2], t);
            save_bmp(path, (const uint8_t *)m.pData, m.RowPitch, d.Width, d.Height);
            ctx->Unmap(st.get(), 0);
            printf("%.1f s: %s %ux%u, frame counter %ld..%ld, capture frames %ld\n",
                   (GetTickCount64() - t0) / 1000.0, path, d.Width, d.Height, f0, f1, frames.load());
            fflush(stdout);
        }
    }
    session.Close();
    pool.Close();
    return 0;
}
