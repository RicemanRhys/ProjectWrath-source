/**
 * Host side of the D3D mirror bridge (Option B, session S2, notes 18.33)
 *
 * Everything that touches the host GPU or the host window is in this file.
 * d3d_bridge.c (the guest side) calls it from whichever guest thread is
 * drawing: the main thread, or a loading screen's own thread (18.32).
 *
 * Threading:
 *   - One bridge lock (g_lock) around every host call. The D3D11 immediate
 *     context isn't thread-safe and xbox_d3d8 keeps its state in globals. The
 *     game serialises its own D3D calls with its lock 0x4EA440, and
 *     d3d_bridge.c checks that it does, but the host side doesn't rely on it.
 *   - A window thread creates the window and pumps its messages for the life
 *     of the process. It never takes g_lock, so a Present that makes DXGI send
 *     the window a message can't deadlock against it. xbox_d3d8's own
 *     Swap/Present pump messages on the calling thread; the bridge calls
 *     neither.
 *   - The window is shown without activation and doesn't steal focus. Alt+Enter
 *     and DXGI's window tracking are off. Closing it ends the process.
 *
 * Two swap chains: xbox_d3d8 creates a blt-model one (DXGI_SWAP_EFFECT_DISCARD)
 * and renders into its back buffer. On this machine a blt-model swap chain
 * never reaches the screen: Present returns S_OK and the window stays black,
 * the same in a standalone D3D11 test (hardware or WARP, DPI-aware or not),
 * while a flip-model one shows (notes 18.33). So xbox_d3d8's swap chain gets a
 * hidden window of its own, and the visible window has a flip-model swap
 * chain (FLIP_DISCARD, 2 buffers) on the same D3D11 device. Each present
 * copies xbox_d3d8's back buffer into it. Nothing in xbox_d3d8 changes.
 *
 * Pacing: every present uses one DXGI sync interval, chosen by d3d_bridge.c.
 * It is 0 while the NV2A executor paces the game (the S2 default), so the
 * mirror never adds a wait. Native Swap doesn't pace without the executor
 * (18.32), so after S7 turns the executor off, the game's own interval
 * (D3DPRESENT_INTERVAL_ONE) has to be used here.
 *
 * Presents skip xbox_d3d8's gamma pass; the game's gamma ramp isn't mirrored
 * yet.
 *
 * Dumps: with a prefix (from RECOMP_FB_DUMP, as -DumpDir sets it), the back
 * buffer is written as <prefix>NNN.bmp before the present, on the executor's
 * cadence: the first 8 frames that had mirrored work, then one every 10 s.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "d3d8_internal.h"      /* xbox_d3d8, and its D3D11 accessors */
#include <dxgi1_2.h>

#include "d3d_host.h"

#define WIN_CLASS           "TwocD3DBridge"
#define WIN_TITLE           "Crash TWOC - D3D bridge (host)"
#define DUMP_FIRST_FRAMES   8
#define DUMP_PERIOD_MS      10000

static CRITICAL_SECTION g_lock;
static volatile LONG g_ready;
static HWND g_hwnd;                     /* visible: the flip-model swap chain */
static HWND g_hwnd_dev;                 /* hidden: xbox_d3d8's own swap chain */
static IDXGISwapChain1 *g_flip;
static unsigned g_w, g_h, g_interval;
static IDirect3DDevice8 *g_dev;
static ID3D11Texture2D *g_staging;
static char g_dump_prefix[512];
static unsigned g_dump_seq, g_dump_first;
static ULONGLONG g_last_dump_ms;
static int g_frame_work;                /* mirrored work since the last present */
static d3dh_stats_t g_st;

/* ── Window thread ─────────────────────────────────────────── */

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CLOSE) {
        fprintf(stderr, "[D3DB] host window closed: ending the process\n");
        fflush(stderr);
        TerminateProcess(GetCurrentProcess(), 0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

typedef struct {
    HANDLE made;
} win_args_t;

static DWORD WINAPI win_thread(LPVOID p)
{
    win_args_t *args = (win_args_t *)p;
    WNDCLASSA wc;
    RECT r;
    MSG msg;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = WIN_CLASS;
    RegisterClassA(&wc);

    r.left = 0;
    r.top = 0;
    r.right = (LONG)g_w;
    r.bottom = (LONG)g_h;
    AdjustWindowRect(&r, style, FALSE);
    g_hwnd_dev = CreateWindowExA(0, WIN_CLASS, WIN_TITLE " (device)", style, 0, 0,
                                 r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    g_hwnd = CreateWindowExA(0, WIN_CLASS, WIN_TITLE, style, CW_USEDEFAULT, CW_USEDEFAULT,
                             r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    if (g_hwnd)
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    SetEvent(args->made);
    if (!g_hwnd || !g_hwnd_dev)
        return 1;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}

/* ── Device ────────────────────────────────────────────────── */

int d3dh_create(unsigned width, unsigned height, unsigned interval, const char *dump_prefix)
{
    static LONG once;
    win_args_t args;
    HANDLE th;
    IDirect3D8 *d3d;
    D3DPRESENT_PARAMETERS pp;
    HRESULT hr;

    if (InterlockedExchange(&once, 1))
        return g_ready ? 0 : -1;        /* the game creates its device once */
    InitializeCriticalSection(&g_lock);
    g_w = width ? width : 640;
    g_h = height ? height : 480;
    g_interval = interval;
    if (dump_prefix)
        snprintf(g_dump_prefix, sizeof(g_dump_prefix), "%s", dump_prefix);

    args.made = CreateEventA(NULL, TRUE, FALSE, NULL);
    th = CreateThread(NULL, 0, win_thread, &args, 0, NULL);
    if (!th) {
        fprintf(stderr, "[D3DB] host: window thread not created (%lu)\n", GetLastError());
        return -1;
    }
    WaitForSingleObject(args.made, INFINITE);
    CloseHandle(args.made);
    CloseHandle(th);
    if (!g_hwnd || !g_hwnd_dev) {
        fprintf(stderr, "[D3DB] host: CreateWindow failed\n");
        return -1;
    }

    EnterCriticalSection(&g_lock);
    d3d = xbox_Direct3DCreate8(0);
    memset(&pp, 0, sizeof(pp));
    pp.BackBufferWidth = g_w;
    pp.BackBufferHeight = g_h;
    pp.BackBufferCount = 1;
    pp.hDeviceWindow = g_hwnd_dev;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;
    hr = d3d->lpVtbl->CreateDevice(d3d, 0, 1, g_hwnd_dev, 0, &pp, &g_dev);
    if (SUCCEEDED(hr)) {
        IDXGIFactory2 *f = NULL;
        DXGI_SWAP_CHAIN_DESC1 sd;
        hr = IDXGISwapChain_GetParent(d3d8_GetSwapChain(), &IID_IDXGIFactory2, (void **)&f);
        if (SUCCEEDED(hr)) {
            memset(&sd, 0, sizeof(sd));
            sd.Width = g_w;
            sd.Height = g_h;
            sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;     /* xbox_d3d8's back buffer format */
            sd.SampleDesc.Count = 1;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.BufferCount = 2;
            sd.Scaling = DXGI_SCALING_STRETCH;
            sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            hr = IDXGIFactory2_CreateSwapChainForHwnd(f, (IUnknown *)d3d8_GetD3D11Device(), g_hwnd, &sd,
                                                      NULL, NULL, &g_flip);
            IDXGIFactory2_MakeWindowAssociation(f, g_hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
            IDXGIFactory2_Release(f);
        }
        if (SUCCEEDED(hr))
            InterlockedExchange(&g_ready, 1);
        else
            fprintf(stderr, "[D3DB] host: flip-model swap chain failed 0x%08lX\n", (unsigned long)hr);
    }
    LeaveCriticalSection(&g_lock);

    if (FAILED(hr)) {
        fprintf(stderr, "[D3DB] host: xbox_d3d8 CreateDevice failed 0x%08lX\n", (unsigned long)hr);
        return -1;
    }
    fprintf(stderr, "[D3DB] host device %ux%u, window %p (own thread, flip model), present interval %u%s%s\n",
            g_w, g_h, (void *)g_hwnd, g_interval,
            g_dump_prefix[0] ? ", dumps " : "", g_dump_prefix[0] ? g_dump_prefix : "");
    return 0;
}

int d3dh_ready(void)
{
    return g_ready;
}

/* ── Clear ─────────────────────────────────────────────────── */

void d3dh_clear(uint32_t xdk_flags, uint32_t color, float z, uint32_t stencil)
{
    DWORD pc = 0;

    if (!g_ready)
        return;
    if ((xdk_flags & 0xF0) == 0xF0)
        pc |= D3DCLEAR_TARGET;
    if (xdk_flags & 1)
        pc |= D3DCLEAR_ZBUFFER;
    if (xdk_flags & 2)
        pc |= D3DCLEAR_STENCIL;

    EnterCriticalSection(&g_lock);
    g_st.clears++;
    if ((xdk_flags & 0xF0) && (xdk_flags & 0xF0) != 0xF0)
        g_st.clears_partial_colour++;   /* D3D11 can't mask a clear: not mirrored */
    if (pc & D3DCLEAR_TARGET)
        g_st.clears_colour++;
    if (pc & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL))
        g_st.clears_depth++;
    if (pc) {
        g_dev->lpVtbl->Clear(g_dev, 0, NULL, pc, (D3DCOLOR)color, z, stencil);
        g_frame_work = 1;
    }
    LeaveCriticalSection(&g_lock);
}

/* ── Dumps ─────────────────────────────────────────────────── */

/* The back buffer (R8G8B8A8) as a bottom-up 24-bit BMP, like the executor's. */
static void dump_back_buffer(const char *path)
{
    ID3D11Device *d = d3d8_GetD3D11Device();
    ID3D11DeviceContext *ctx = d3d8_GetD3D11Context();
    ID3D11Texture2D *bb = NULL;
    D3D11_TEXTURE2D_DESC desc;
    D3D11_MAPPED_SUBRESOURCE m;
    uint8_t hdr[54], *row;
    uint32_t w, h, y, x, pad, filesz, rowbytes;
    FILE *f;

    if (FAILED(IDXGISwapChain_GetBuffer(d3d8_GetSwapChain(), 0, &IID_ID3D11Texture2D, (void **)&bb)))
        return;
    ID3D11Texture2D_GetDesc(bb, &desc);
    if (!g_staging) {
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        if (FAILED(ID3D11Device_CreateTexture2D(d, &desc, NULL, &g_staging))) {
            ID3D11Texture2D_Release(bb);
            return;
        }
    }
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)g_staging, (ID3D11Resource *)bb);
    ID3D11Texture2D_Release(bb);
    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)g_staging, 0, D3D11_MAP_READ, 0, &m)))
        return;

    w = desc.Width;
    h = desc.Height;
    rowbytes = w * 3;
    pad = (4 - (rowbytes & 3)) & 3;
    filesz = 54 + (rowbytes + pad) * h;
    f = fopen(path, "wb");
    if (f) {
        memset(hdr, 0, sizeof(hdr));
        hdr[0] = 'B';
        hdr[1] = 'M';
        memcpy(hdr + 2, &filesz, 4);
        hdr[10] = 54;
        hdr[14] = 40;
        memcpy(hdr + 18, &w, 4);
        memcpy(hdr + 22, &h, 4);
        hdr[26] = 1;
        hdr[28] = 24;
        fwrite(hdr, 1, sizeof(hdr), f);
        row = (uint8_t *)malloc(rowbytes + pad);
        for (y = h; row && y-- > 0; ) {
            const uint8_t *src = (const uint8_t *)m.pData + (size_t)y * m.RowPitch;
            for (x = 0; x < w; x++) {
                row[3 * x + 0] = src[4 * x + 2];
                row[3 * x + 1] = src[4 * x + 1];
                row[3 * x + 2] = src[4 * x + 0];
            }
            memset(row + rowbytes, 0, pad);
            fwrite(row, 1, rowbytes + pad, f);
        }
        free(row);
        fclose(f);
    }
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)g_staging, 0);
}

/* ── Present ───────────────────────────────────────────────── */

void d3dh_present(double t_s, unsigned swap_no)
{
    ULONGLONG now;
    HRESULT hr;

    if (!g_ready)
        return;
    EnterCriticalSection(&g_lock);
    now = GetTickCount64();
    if (g_dump_prefix[0] &&
        ((g_frame_work && g_dump_first < DUMP_FIRST_FRAMES) || now - g_last_dump_ms >= DUMP_PERIOD_MS)) {
        char path[600];
        snprintf(path, sizeof(path), "%s%03u.bmp", g_dump_prefix, g_dump_seq++);
        dump_back_buffer(path);
        if (g_frame_work && g_dump_first < DUMP_FIRST_FRAMES)
            g_dump_first++;
        g_last_dump_ms = now;
        g_st.dumps++;
        fprintf(stderr, "[D3DB] host dump %s at +%.1f s, guest swap %u\n", path, t_s, swap_no);
    }
    {   /* xbox_d3d8's back buffer -> the visible flip-model swap chain */
        ID3D11Texture2D *src = NULL, *dst = NULL;
        if (SUCCEEDED(IDXGISwapChain_GetBuffer(d3d8_GetSwapChain(), 0, &IID_ID3D11Texture2D, (void **)&src)) &&
            SUCCEEDED(IDXGISwapChain1_GetBuffer(g_flip, 0, &IID_ID3D11Texture2D, (void **)&dst)))
            ID3D11DeviceContext_CopyResource(d3d8_GetD3D11Context(), (ID3D11Resource *)dst, (ID3D11Resource *)src);
        if (src)
            ID3D11Texture2D_Release(src);
        if (dst)
            ID3D11Texture2D_Release(dst);
    }
    hr = IDXGISwapChain1_Present(g_flip, g_interval, 0);
    g_st.presents++;
    if (FAILED(hr) && ++g_st.present_fails <= 5)
        fprintf(stderr, "[D3DB] host Present failed 0x%08lX\n", (unsigned long)hr);
    else if (hr != S_OK && ++g_st.present_status <= 5)   /* e.g. DXGI_STATUS_OCCLUDED: not shown */
        fprintf(stderr, "[D3DB] host Present returned status 0x%08lX\n", (unsigned long)hr);
    g_frame_work = 0;
    LeaveCriticalSection(&g_lock);
}

void d3dh_stats(d3dh_stats_t *out)
{
    if (!g_ready) {
        memset(out, 0, sizeof(*out));
        return;
    }
    EnterCriticalSection(&g_lock);
    *out = g_st;
    LeaveCriticalSection(&g_lock);
}
