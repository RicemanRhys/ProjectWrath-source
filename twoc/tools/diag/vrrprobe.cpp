// Step 77 (notes 18.80): does this display show frames at the rate they are
// presented (variable refresh), or only on its fixed refresh grid? Windows has no
// user-mode call that says whether variable refresh is on (the driver's
// VariableRefresh capability bit is kernel-side), so this presents a test
// picture at a fixed rate from a QPC limiter and reads back, from DXGI's frame
// statistics, when each frame reached the screen (SyncQPCTime). On a fixed
// refresh a 90 fps present lands 2 or 3 refreshes apart at 240 Hz (8.3 / 12.5
// ms); with variable refresh every frame is 11.1 ms apart. It also prints what
// the display says of itself: its mode, its EDID's vertical range (the range
// limits descriptor, the "continuous frequency" bit) and AMD's FreeSync data
// block, and DXGI's tearing support (needed for variable refresh in a window
// or borderless).
//
//   vrrprobe.exe HZ SECONDS [--window] [--tear] [--interval=N] [--log=FILE]
//
// Default: borderless fullscreen on the primary monitor, DXGI interval 1.
// --tear: the swap chain allows tearing and presents at interval 0 with
// DXGI_PRESENT_ALLOW_TEARING (Microsoft's recipe for variable refresh in a
// window). Esc ends early. It also lists the display's modes (its sizes and
// refresh rates), its rotation and scale. Build with vrrprobe_build.bat.
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_5.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shcore.lib")

static volatile bool g_quit;

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_KEYDOWN && w == VK_ESCAPE)
        g_quit = true;
    if (m == WM_CLOSE || m == WM_DESTROY) {
        g_quit = true;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// The primary monitor's mode rate and its EDID, from the display configuration.
static double monitor_info(std::vector<unsigned char> &edid)
{
    UINT32 np = 0, nm = 0;
    double hz = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS)
        return 0;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(np);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), NULL) != ERROR_SUCCESS)
        return 0;
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    POINT o = { 0, 0 };
    GetMonitorInfoW(MonitorFromPoint(o, MONITOR_DEFAULTTOPRIMARY), &mi);
    for (UINT32 i = 0; i < np; i++) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME sn = {};
        sn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        sn.header.size = sizeof(sn);
        sn.header.adapterId = paths[i].sourceInfo.adapterId;
        sn.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&sn.header) != ERROR_SUCCESS || wcscmp(sn.viewGdiDeviceName, mi.szDevice) != 0)
            continue;
        if (paths[i].targetInfo.refreshRate.Denominator)
            hz = (double)paths[i].targetInfo.refreshRate.Numerator / paths[i].targetInfo.refreshRate.Denominator;
        DISPLAYCONFIG_TARGET_DEVICE_NAME tn = {};
        tn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        tn.header.size = sizeof(tn);
        tn.header.adapterId = paths[i].targetInfo.adapterId;
        tn.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&tn.header) == ERROR_SUCCESS) {
            // \\?\DISPLAY#MSI3DD3#5&2db65555&0&UID4353#{e6f07b5f-...} -> Enum\DISPLAY\MSI3DD3\5&2db65555&0&UID4353
            wchar_t key[512], *p = tn.monitorDevicePath;
            wprintf(L"monitor: %ls (%ls)\n", tn.monitorFriendlyDeviceName, p);
            if (wcsncmp(p, L"\\\\?\\", 4) == 0)
                p += 4;
            wchar_t part[3][200] = {};
            int n = 0;
            for (wchar_t *q = p; *q && n < 3; n++) {
                wchar_t *e = wcschr(q, L'#');
                size_t len = e ? (size_t)(e - q) : wcslen(q);
                wcsncpy_s(part[n], q, len < 199 ? len : 199);
                q = e ? e + 1 : q + len;
            }
            swprintf_s(key, L"SYSTEM\\CurrentControlSet\\Enum\\%ls\\%ls\\%ls\\Device Parameters", part[0], part[1], part[2]);
            HKEY k;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &k) == ERROR_SUCCESS) {
                DWORD sz = 0;
                if (RegQueryValueExW(k, L"EDID", NULL, NULL, NULL, &sz) == ERROR_SUCCESS && sz) {
                    edid.resize(sz);
                    RegQueryValueExW(k, L"EDID", NULL, NULL, edid.data(), &sz);
                }
                RegCloseKey(k);
            }
        }
        break;
    }
    return hz;
}

static void edid_print(const std::vector<unsigned char> &e)
{
    if (e.size() < 128) {
        printf("EDID: not found\n");
        return;
    }
    printf("EDID: %zu bytes, version %d.%d, continuous frequency bit %d\n", e.size(), e[18], e[19], e[24] & 1);
    for (int d = 54; d < 126; d += 18)
        if (!e[d] && !e[d + 1] && e[d + 3] == 0xFD) {
            int mn = e[d + 5] + ((e[d + 4] & 1) ? 255 : 0), mx = e[d + 6] + ((e[d + 4] & 2) ? 255 : 0);
            printf("EDID range limits: %d-%d Hz vertical\n", mn, mx);
        }
    for (size_t x = 1; (x + 1) * 128 <= e.size(); x++) {
        const unsigned char *b = &e[x * 128];
        if (b[0] != 2)
            continue;
        for (int p = 4; p < b[2] && p < 127;) {
            int tag = b[p] >> 5, len = b[p] & 31;
            if (tag == 3 && len >= 8 && b[p + 1] == 0x1A && b[p + 2] == 0 && b[p + 3] == 0)
                printf("EDID AMD FreeSync data block: %d-%d Hz\n", b[p + 6], b[p + 7]);
            p += 1 + len;
        }
    }
}

// The Legion Go package (twoc/tools/package): the primary monitor's current mode,
// its rotation and scale, and every mode it offers (size and refresh), so a log
// brought back from another PC says which rates it could have run.
static void modes_print(void)
{
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    POINT o = { 0, 0 };
    HMONITOR mon = MonitorFromPoint(o, MONITOR_DEFAULTTOPRIMARY);
    GetMonitorInfoW(mon, &mi);
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsExW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm, 0)) {
        UINT dx = 0, dy = 0;
        GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy);
        printf("current: %lux%lu at %lu Hz, %lu bits, rotated %lu degrees, scale %u%%\n", dm.dmPelsWidth, dm.dmPelsHeight,
               dm.dmDisplayFrequency, dm.dmBitsPerPel, dm.dmDisplayOrientation * 90, dx * 100 / 96);
    }
    std::vector<std::pair<std::pair<DWORD, DWORD>, std::vector<DWORD>>> sizes;  // WxH -> rates
    for (DWORD i = 0; EnumDisplaySettingsExW(mi.szDevice, i, &dm, 0); i++) {
        if (dm.dmBitsPerPel != 32)
            continue;
        std::pair<DWORD, DWORD> wh(dm.dmPelsWidth, dm.dmPelsHeight);
        size_t k = 0;
        while (k < sizes.size() && sizes[k].first != wh)
            k++;
        if (k == sizes.size())
            sizes.push_back({ wh, {} });
        std::vector<DWORD> &r = sizes[k].second;
        if (std::find(r.begin(), r.end(), dm.dmDisplayFrequency) == r.end())
            r.push_back(dm.dmDisplayFrequency);
    }
    std::sort(sizes.begin(), sizes.end(), [](const auto &a, const auto &b) {
        return a.first.first * a.first.second > b.first.first * b.first.second;
    });
    printf("modes (%zu sizes):\n", sizes.size());
    for (auto &s : sizes) {
        std::sort(s.second.begin(), s.second.end());
        printf("  %lux%lu:", s.first.first, s.first.second);
        for (DWORD hz : s.second)
            printf(" %lu", hz);
        printf(" Hz\n");
    }
}

// NVIDIA only: what the driver says of variable refresh on the primary display
// (NvAPI_Disp_GetVRRInfo, R525+; ids and layout from NVIDIA's MIT nvapi headers).
static void nvapi_print(void)
{
    HMODULE m = LoadLibraryW(L"nvapi64.dll");
    if (!m) {
        printf("NVAPI: not present (not an NVIDIA driver)\n");
        return;
    }
    typedef void *(__cdecl *qi_t)(unsigned);
    qi_t qi = (qi_t)GetProcAddress(m, "nvapi_QueryInterface");
    typedef int(__cdecl *init_t)(void);
    typedef int(__cdecl *byname_t)(const char *, unsigned *);
    typedef int(__cdecl *vrr_t)(unsigned, void *);
    init_t init = qi ? (init_t)qi(0x0150e828) : NULL;
    byname_t byname = qi ? (byname_t)qi(0xae457190) : NULL;
    vrr_t vrr = qi ? (vrr_t)qi(0xdf8fda57) : NULL;
    if (!init || !byname || !vrr || init() != 0) {
        printf("NVAPI: no VRR query in this driver\n");
        return;
    }
    MONITORINFOEXA mi = {};
    mi.cbSize = sizeof(mi);
    POINT o = { 0, 0 };
    GetMonitorInfoA(MonitorFromPoint(o, MONITOR_DEFAULTTOPRIMARY), &mi);
    unsigned id = 0, info[6] = {};
    info[0] = (unsigned)sizeof(info) | (1u << 16);  // NV_GET_VRR_INFO_VER1
    int st = byname(mi.szDevice, &id);
    if (st != 0 || (st = vrr(id, info)) != 0) {
        printf("NVAPI: VRR query failed (%d)\n", st);
        return;
    }
    printf("NVAPI VRR (%s): enabled %u, possible %u, requested %u, indicator %u, display in VRR mode %u\n", mi.szDevice,
           info[1] & 1, (info[1] >> 1) & 1, (info[1] >> 2) & 1, (info[1] >> 3) & 1, (info[1] >> 4) & 1);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("vrrprobe.exe HZ SECONDS [--window] [--tear] [--interval=N] [--log=FILE]\n");
        return 2;
    }
    double hz = atof(argv[1]), secs = atof(argv[2]);
    bool windowed = false, tear = false;
    int interval = 1;
    const char *logpath = NULL;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--window"))
            windowed = true;
        else if (!strcmp(argv[i], "--tear"))
            tear = true;
        else if (!strncmp(argv[i], "--interval=", 11))
            interval = atoi(argv[i] + 11);
        else if (!strncmp(argv[i], "--log=", 6))
            logpath = argv[i] + 6;
    }
    if (tear)
        interval = 0;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::vector<unsigned char> edid;
    double mode = monitor_info(edid);
    printf("mode: %.3f Hz\n", mode);
    modes_print();
    edid_print(edid);

    IDXGIFactory5 *f5 = NULL;
    BOOL allow = FALSE;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory5), (void **)&f5)))
        f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow));
    printf("DXGI tearing support: %s\n", allow ? "yes" : "no");
    nvapi_print();
    if (secs <= 0)
        return 0;
    if (tear && !allow) {
        printf("--tear asked for but not supported\n");
        tear = false;
        interval = 1;
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"vrrprobe";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);
    MONITORINFO mi = { sizeof(mi) };
    POINT o = { 0, 0 };
    GetMonitorInfoW(MonitorFromPoint(o, MONITOR_DEFAULTTOPRIMARY), &mi);
    RECT r = mi.rcMonitor;
    if (windowed)
        r = { 100, 100, 100 + 960, 100 + 540 };
    HWND hwnd = CreateWindowExW(windowed ? 0 : WS_EX_TOPMOST, L"vrrprobe", L"vrrprobe",
                                windowed ? WS_OVERLAPPEDWINDOW | WS_VISIBLE : WS_POPUP | WS_VISIBLE, r.left, r.top,
                                r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    RECT cr;
    GetClientRect(hwnd, &cr);
    UINT w = cr.right - cr.left, h = cr.bottom - cr.top;

    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    if (FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, NULL, &ctx))) {
        printf("no D3D11 device\n");
        return 1;
    }
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = w;
    sd.Height = h;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 3;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = tear ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    IDXGISwapChain1 *sc = NULL;
    HRESULT hr = f5->CreateSwapChainForHwnd(dev, hwnd, &sd, NULL, NULL, &sc);
    if (FAILED(hr)) {
        printf("swap chain failed 0x%08lX\n", (unsigned long)hr);
        return 1;
    }
    f5->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    printf("presenting %.3f fps for %.1f s, %s %ux%u, interval %d%s\n", hz, secs, windowed ? "window" : "borderless fullscreen",
           w, h, interval, tear ? ", tearing allowed" : "");

    LARGE_INTEGER fq, now;
    QueryPerformanceFrequency(&fq);
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    double period = fq.QuadPart / hz, next;
    QueryPerformanceCounter(&now);
    next = (double)now.QuadPart + period;
    double end = (double)now.QuadPart + secs * fq.QuadPart;
    struct shown_t { UINT pc, prc; LONGLONG qpc; };
    std::vector<shown_t> shown;
    std::vector<double> sent;           // QPC of each present, by present count - 1
    UINT last_pc = 0;
    unsigned frame = 0;
    while (!g_quit && (double)now.QuadPart < end) {
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        for (;;) {                      // the limiter: sleep to ~1 ms before the slot, then spin
            QueryPerformanceCounter(&now);
            double left = (next - (double)now.QuadPart) * 1000.0 / fq.QuadPart;
            if (left <= 0)
                break;
            if (left > 1.5 && timer) {
                LARGE_INTEGER due;
                due.QuadPart = -(LONGLONG)((left - 1.0) * 10000.0);
                SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE);
                WaitForSingleObject(timer, INFINITE);
            }
        }
        if ((double)now.QuadPart - next > period)
            next = (double)now.QuadPart;    // a whole period late: from now
        next += period;
        ID3D11Texture2D *bb = NULL;
        ID3D11RenderTargetView *rtv = NULL;
        sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb);
        dev->CreateRenderTargetView(bb, NULL, &rtv);
        float c[4] = { 0.1f, 0.1f, 0.1f, 1 };
        ctx->ClearRenderTargetView(rtv, c);
        D3D11_RECT bar = { (LONG)((frame * 8) % w), 0, (LONG)((frame * 8) % w) + 32, (LONG)h };
        // a moving bar: the test picture changes every frame (one clear per scissor is enough for that)
        ID3D11DeviceContext1 *c1 = NULL;
        if (SUCCEEDED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), (void **)&c1))) {
            float white[4] = { 0.9f, 0.9f, 0.9f, 1 };
            c1->ClearView(rtv, white, &bar, 1);
            c1->Release();
        }
        rtv->Release();
        bb->Release();
        QueryPerformanceCounter(&now);
        sent.push_back((double)now.QuadPart);
        sc->Present(interval, tear ? DXGI_PRESENT_ALLOW_TEARING : 0);
        frame++;
        DXGI_FRAME_STATISTICS fs;
        if (SUCCEEDED(sc->GetFrameStatistics(&fs)) && fs.PresentCount && fs.PresentCount != last_pc) {
            shown.push_back({ fs.PresentCount, fs.PresentRefreshCount, fs.SyncQPCTime.QuadPart });
            last_pc = fs.PresentCount;
        }
        QueryPerformanceCounter(&now);
    }
    sc->SetFullscreenState(FALSE, NULL);
    DestroyWindow(hwnd);

    // Consecutive presents both seen: their gap on screen.
    double p_ms = 1000.0 / hz, r_ms = mode > 0 ? 1000.0 / mode : 0;
    std::vector<double> gaps;
    std::vector<int> rgaps;
    for (size_t i = 1; i < shown.size(); i++)
        if (shown[i].pc == shown[i - 1].pc + 1) {
            gaps.push_back((shown[i].qpc - shown[i - 1].qpc) * 1000.0 / fq.QuadPart);
            rgaps.push_back((int)(shown[i].prc - shown[i - 1].prc));
        }
    FILE *lf = logpath ? fopen(logpath, "w") : NULL;
    unsigned follow = 0, grid = 0, diag = 0;
    double sum = 0;
    for (size_t i = 0; i < gaps.size(); i++) {
        double g = gaps[i], near_grid = r_ms > 0 ? fabs(g - r_ms * floor(g / r_ms + 0.5)) : 1e9;
        double off = fabs(g - p_ms);
        bool diagnostic = r_ms > 0 && fabs(p_ms - r_ms * floor(p_ms / r_ms + 0.5)) > 0.3;
        sum += g;
        if (diagnostic) {
            diag++;
            if (off < near_grid)
                follow++;
            else
                grid++;
        }
        if (lf)
            fprintf(lf, "%.3f %d\n", g, rgaps[i]);
    }
    if (lf)
        fclose(lf);
    std::vector<double> s = gaps;
    std::sort(s.begin(), s.end());
    printf("presents %u, frames seen shown %zu, consecutive pairs %zu\n", frame, shown.size(), gaps.size());
    if (!s.empty()) {
        printf("on-screen gap ms: mean %.3f (presented every %.3f), p5 %.3f, p50 %.3f, p95 %.3f, min %.3f, max %.3f\n",
               sum / gaps.size(), p_ms, s[s.size() * 5 / 100], s[s.size() / 2], s[s.size() * 95 / 100], s.front(), s.back());
        int hist[10] = {};
        for (int g : rgaps)
            hist[g < 0 ? 0 : g > 9 ? 9 : g]++;
        printf("refreshes between shown frames (DXGI's count): 0 %d, 1 %d, 2 %d, 3 %d, 4 %d, 5 %d, 6+ %d\n", hist[0], hist[1],
               hist[2], hist[3], hist[4], hist[5], hist[6] + hist[7] + hist[8] + hist[9]);
        unsigned within = 0;
        for (double g : gaps)
            within += fabs(g - p_ms) < 0.5;
        printf("gaps within 0.5 ms of the presented %.3f ms: %.1f%%\n", p_ms, 100.0 * within / gaps.size());
    }
    if (diag)
        printf("verdict: %.1f%% of gaps nearer the presented interval than the %.3f ms refresh grid: %s\n",
               100.0 * follow / diag, r_ms,
               follow * 10 >= diag * 8 ? "variable refresh (frames shown as presented)" :
               grid * 10 >= diag * 8 ? "fixed refresh (frames shown on the refresh grid)" : "mixed");
    else
        printf("verdict: none (%.3f fps is a whole fraction of the %.3f Hz refresh: try a rate between)\n", hz, mode);
    return 0;
}
