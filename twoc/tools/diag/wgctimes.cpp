// When a window's new frames reach the screen, from Windows Graphics Capture:
// each capture frame is a DWM composition that carried new content for the
// window, stamped with its composition time (SystemRelativeTime, QPC-based).
// Needs no elevation, unlike PresentMon.
//
//   wgctimes.exe "Window title" OUT.txt SECONDS [--hash]
//
// With --hash each frame is also copied back and hashed (every 4th pixel of
// every 4th row), as a third column: equal hashes in a row are the same
// picture shown twice, which judders at a steady 60 fps and which frame
// timing can't show.
//
// Waits up to 120 s for the window, then writes one line per frame for
// SECONDS: the composition time in ms (QPC clock) and the game's frame counter
// (guest 0x25A660, host +0x10000) read when the frame arrived.
// Summarise with wgcsum.py. Build with wgctimes_build.bat.
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
#include <cstring>
#include <mutex>
#include <vector>

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

static long frame_counter(HANDLE game)
{
    uint32_t v = 0;
    SIZE_T got = 0;
    if (!game || !ReadProcessMemory(game, (LPCVOID)(uintptr_t)(0x25A660 + 0x10000), &v, 4, &got))
        return -1;
    return (long)v;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: wgctimes \"title\" OUT.txt SECONDS\n");
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
    HANDLE game = open_game();
    double secs = atof(argv[3]);

    com_ptr<ID3D11Device> dev;
    check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                    nullptr, 0, D3D11_SDK_VERSION, dev.put(), nullptr, nullptr));
    com_ptr<::IInspectable> insp;
    check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dev.as<IDXGIDevice>().get(), insp.put()));
    IDirect3DDevice d3d = insp.as<IDirect3DDevice>();

    auto interop = get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{ nullptr };
    check_hresult(interop->CreateForWindow(hwnd, guid_of<GraphicsCaptureItem>(), put_abi(item)));
    auto pool = Direct3D11CaptureFramePool::CreateFreeThreaded(d3d, DirectXPixelFormat::B8G8R8A8UIntNormalized, 3,
                                                               item.Size());
    auto session = pool.CreateCaptureSession(item);
    try { session.IsCursorCaptureEnabled(false); } catch (...) {}
    try { session.IsBorderRequired(false); } catch (...) {}

    bool hash = argc > 4 && strcmp(argv[4], "--hash") == 0;
    com_ptr<ID3D11DeviceContext> ctx;
    dev->GetImmediateContext(ctx.put());
    com_ptr<ID3D11Texture2D> st;
    std::mutex mu;
    struct row { double ms; long fc; unsigned long long h; };
    std::vector<row> rows;
    rows.reserve(1 << 16);
    pool.FrameArrived([&](Direct3D11CaptureFramePool const &p, auto const &) {
        auto frame = p.TryGetNextFrame();
        if (!frame)
            return;
        double ms = frame.SystemRelativeTime().count() / 10000.0;   // 100 ns units on the QPC clock
        long fc = frame_counter(game);
        unsigned long long h = 0;
        std::lock_guard<std::mutex> lock(mu);
        if (hash) {
            auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            com_ptr<ID3D11Texture2D> tex;
            access->GetInterface(guid_of<ID3D11Texture2D>(), tex.put_void());
            D3D11_TEXTURE2D_DESC d;
            tex->GetDesc(&d);
            if (!st) {
                d.Usage = D3D11_USAGE_STAGING;
                d.BindFlags = 0;
                d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                d.MiscFlags = 0;
                dev->CreateTexture2D(&d, nullptr, st.put());
            }
            D3D11_MAPPED_SUBRESOURCE m;
            if (st) {
                ctx->CopyResource(st.get(), tex.get());
                if (SUCCEEDED(ctx->Map(st.get(), 0, D3D11_MAP_READ, 0, &m))) {
                    D3D11_TEXTURE2D_DESC sd;
                    st->GetDesc(&sd);
                    h = 1469598103934665603ull;
                    for (UINT y = 0; y < sd.Height; y += 4) {
                        const uint32_t *px = (const uint32_t *)((const uint8_t *)m.pData + (size_t)y * m.RowPitch);
                        for (UINT x = 0; x < sd.Width; x += 4)
                            h = (h ^ px[x]) * 1099511628211ull;
                    }
                    ctx->Unmap(st.get(), 0);
                }
            }
        }
        frame.Close();
        rows.push_back({ ms, fc, h });
    });
    session.StartCapture();
    printf("capturing window %p for %.0f s\n", (void *)hwnd, secs);
    fflush(stdout);
    ULONGLONG t0 = GetTickCount64();
    while ((GetTickCount64() - t0) / 1000.0 < secs && IsWindow(hwnd))
        Sleep(50);
    session.Close();
    pool.Close();
    FILE *f = fopen(argv[2], "w");
    std::lock_guard<std::mutex> lock(mu);
    for (auto &r : rows)
        fprintf(f, "%.4f %ld %016llx\n", r.ms, r.fc, r.h);
    fclose(f);
    printf("%zu frames\n", rows.size());
    return 0;
}
