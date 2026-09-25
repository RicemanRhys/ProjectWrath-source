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
 * RECOMP_D3D_BRIDGE_DUMP_EVERY=N adds one every Nth guest swap (S3 uses it to
 * catch the splash after its fade-in, which the executor's cadence misses).
 * RECOMP_D3D_BRIDGE_DUMP_RT=1 (S4) also writes every host render-target
 * texture with each dump, as <prefix>NNN_rt_<Data>.bmp.
 * RECOMP_D3D_BRIDGE_DRAWLOG=A-B (step 35, with d3d_bridge.c's draw log) dumps
 * every guest swap from A to B.
 *
 * S4 (notes 18.35):
 *   - Render targets. A guest render target other than the back buffer (the
 *     title's six 128x128 A8R8G8B8 texture levels) gets a host render-target
 *     texture of its size and format, level 0 only, plus a depth buffer of the
 *     same size (D3D11 won't pair a render target with a depth view of
 *     another size, and xbox_d3d8 binds its 640x480 default when given none).
 *     Draws and clears go to it; a guest texture with the same Data samples
 *     it instead of guest memory. Its other levels are never rendered by the
 *     game, so one level stands for them.
 *   - P8 textures are expanded through the stage's guest palette to A8R8G8B8
 *     here (the swizzle is per texel, so it survives), and the palette's
 *     signature is part of the cache key. xbox_d3d8's own P8 path bakes a
 *     per-stage palette into the texture it uploads, which doesn't fit a cache
 *     shared by stages.
 *   - Pixel shaders arrive already re-encoded for xbox_d3d8's combiner
 *     translator (d3d_bridge.c translate_ps) and are set before each draw.
 *     d3dh_ps_probe draws one pixel with one, for RECOMP_D3D_BRIDGE_PSPROBE.
 *
 * Step 34 (notes 18.36): fixed-function 3D. A draw with untransformed
 * vertices carries world/view/projection, the material and the enabled
 * lights (apply_ff), and an indexed one goes through xbox_d3d8's
 * DrawIndexedPrimitiveUP with 16-bit indices already rebased to its vertex
 * range and turned into lists where xbox_d3d8 can't draw the primitive
 * indexed (d3d_bridge.c index_list).
 *
 * Step 35 (notes 18.37): CopyRects from the back buffer to a texture (the
 * hub's only kind) copies the host back buffer into a host render-target
 * texture for the destination's Data, in R8G8B8A8 like the back buffer
 * (d3dh_copy_back); a draw that binds that texture samples the copy.
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
static unsigned g_dump_every;           /* RECOMP_D3D_BRIDGE_DUMP_EVERY: also every Nth guest swap */
static unsigned g_dump_from, g_dump_to; /* RECOMP_D3D_BRIDGE_DRAWLOG: every swap in this range */
static ULONGLONG g_last_dump_ms;
static int g_frame_work;                /* mirrored work since the last present */
static int g_dump_rt;                   /* RECOMP_D3D_BRIDGE_DUMP_RT: render targets too (S4) */
static d3dh_stats_t g_st;

/* S4: host render-target textures, by guest Data. */
#define RT_SLOTS 16
typedef struct {
    uint32_t data, width, height, format;
    IDirect3DTexture8 *tex;
    IDirect3DSurface8 *surf, *depth;
} host_rt_t;
static host_rt_t g_rt[RT_SLOTS];
static int g_cur_target = -1;           /* -1 the back buffer, else a g_rt index; -2 unknown */

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
    if (dump_prefix) {
        const char *every = getenv("RECOMP_D3D_BRIDGE_DUMP_EVERY");
        const char *rt = getenv("RECOMP_D3D_BRIDGE_DUMP_RT");
        const char *log = getenv("RECOMP_D3D_BRIDGE_DRAWLOG");
        snprintf(g_dump_prefix, sizeof(g_dump_prefix), "%s", dump_prefix);
        g_dump_every = every ? (unsigned)strtoul(every, NULL, 0) : 0;
        g_dump_rt = rt && *rt && strcmp(rt, "0") != 0;
        if (log && *log) {
            char *e;
            g_dump_from = (unsigned)strtoul(log, &e, 0);
            g_dump_to = *e == '-' ? (unsigned)strtoul(e + 1, NULL, 0) : g_dump_from;
        }
    }

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

/* ── Render targets (S4) ───────────────────────────────────── */

/* The host render target for guest Data, created on first use. Called with
 * g_lock held. Returns its index, or -1. */
static int rt_find(const d3dh_target_t *t, int create)
{
    int i, s, free_i = -1;
    IDirect3DTexture8 *tex = NULL;
    IDirect3DSurface8 *surf = NULL, *depth = NULL;
    HRESULT hr;

    for (i = 0; i < RT_SLOTS; i++) {
        if (g_rt[i].tex && g_rt[i].data == t->data) {
            if (g_rt[i].width == t->width && g_rt[i].height == t->height && g_rt[i].format == t->format)
                return i;
            if (!create)
                return -1;
            if (g_cur_target == i) {    /* the guest reused the memory for another shape */
                g_dev->lpVtbl->SetRenderTarget(g_dev, NULL, NULL);
                g_cur_target = -1;
            }
            for (s = 0; s < 4; s++)     /* xbox_d3d8 keeps the bound pointer */
                g_dev->lpVtbl->SetTexture(g_dev, s, NULL);
            g_rt[i].depth->lpVtbl->Release(g_rt[i].depth);
            g_rt[i].surf->lpVtbl->Release(g_rt[i].surf);
            g_rt[i].tex->lpVtbl->Release(g_rt[i].tex);
            memset(&g_rt[i], 0, sizeof(g_rt[i]));
            free_i = i;
            break;
        }
        if (!g_rt[i].tex && free_i < 0)
            free_i = i;
    }
    if (!create || free_i < 0)
        return -1;
    hr = g_dev->lpVtbl->CreateTexture(g_dev, t->width, t->height, 1, D3DUSAGE_RENDERTARGET, (D3DFORMAT)t->format,
                                      D3DPOOL_DEFAULT, &tex);
    if (SUCCEEDED(hr))
        hr = tex->lpVtbl->GetSurfaceLevel(tex, 0, &surf);
    if (SUCCEEDED(hr))
        hr = g_dev->lpVtbl->CreateDepthStencilSurface(g_dev, t->width, t->height, D3DFMT_D24S8,
                                                      D3DMULTISAMPLE_NONE, &depth);
    if (FAILED(hr) || !surf || !depth) {
        if (depth)
            depth->lpVtbl->Release(depth);
        if (surf)
            surf->lpVtbl->Release(surf);
        if (tex)
            tex->lpVtbl->Release(tex);
        if (++g_st.rt_fails <= 5)
            fprintf(stderr, "[D3DB] host render target %08X (%ux%u format %02X) not created: 0x%08lX\n",
                    t->data, t->width, t->height, t->format, (unsigned long)hr);
        return -1;
    }
    g_rt[free_i].data = t->data;
    g_rt[free_i].width = t->width;
    g_rt[free_i].height = t->height;
    g_rt[free_i].format = t->format;
    g_rt[free_i].tex = tex;
    g_rt[free_i].surf = surf;
    g_rt[free_i].depth = depth;
    g_st.rt_created++;
    fprintf(stderr, "[D3DB] host render target %d for guest Data %08X: %ux%u format %02X\n", free_i, t->data,
            t->width, t->height, t->format);
    /* Start from black, as the guest's memory was (never seen otherwise). */
    g_dev->lpVtbl->SetRenderTarget(g_dev, surf, depth);
    g_dev->lpVtbl->Clear(g_dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
    g_cur_target = free_i;
    return free_i;
}

/* Bind where a draw or clear goes. Called with g_lock held. Returns 0, or -1
 * if the host has no render target for it. */
static int bind_target(const d3dh_target_t *t)
{
    int i;

    if (!t->data) {
        if (g_cur_target != -1) {
            g_dev->lpVtbl->SetRenderTarget(g_dev, NULL, NULL);     /* the default views */
            g_cur_target = -1;
        }
        return 0;
    }
    i = rt_find(t, 1);
    if (i < 0)
        return -1;
    if (g_cur_target != i) {
        g_dev->lpVtbl->SetRenderTarget(g_dev, g_rt[i].surf, g_rt[i].depth);
        g_cur_target = i;
    }
    return 0;
}

/* ── Clear ─────────────────────────────────────────────────── */

void d3dh_clear(const d3dh_target_t *target, uint32_t xdk_flags, uint32_t color, float z, uint32_t stencil)
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
    if (bind_target(target) != 0) {
        LeaveCriticalSection(&g_lock);
        return;
    }
    if (target->data)
        g_st.rt_clears++;
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

/* ── Textures (S3) ─────────────────────────────────────────── */

/* xbox_d3d8 textures made from guest ones, keyed by the guest header. An
 * entry is reused only while the header's Data, format, size and content
 * signature all match; otherwise it is recreated and uploaded again. The
 * least recently used entry makes room when the table is full. */
#define TEX_CACHE_SLOTS 1024

typedef struct {
    d3dh_tex_t desc;                    /* bits unused once uploaded */
    IDirect3DTexture8 *tex;
    unsigned last_use;
} host_tex_t;

static host_tex_t g_tex[TEX_CACHE_SLOTS];
static unsigned g_tex_clock;

static uint32_t level_dim(uint32_t d, uint32_t level)
{
    d >>= level;
    return d ? d : 1;
}

static uint32_t level_bytes(D3DFORMAT fmt, uint32_t w, uint32_t h, uint32_t pitch)
{
    if (pitch)
        return pitch * h;
    return d3d8_row_pitch(fmt, w) * (d3d8_format_is_compressed(fmt) ? (h + 3) / 4 : h);
}

#define XDK_FMT_P8          0x0B
#define XDK_FMT_A8R8G8B8    0x06        /* swizzled, like P8: what a P8 texture is uploaded as */

int d3dh_format_supported(uint32_t format)
{
    return (format == XDK_FMT_P8 || !d3d8_format_is_palettized((D3DFORMAT)format)) &&
           d3d8_to_dxgi_format((D3DFORMAT)format) != DXGI_FORMAT_UNKNOWN &&
           d3d8_format_bpp((D3DFORMAT)format) != 0;
}

uint32_t d3dh_texture_bytes(uint32_t format, uint32_t width, uint32_t height, uint32_t levels, uint32_t pitch)
{
    uint32_t l, n = 0;

    if (!d3dh_format_supported(format))
        return 0;
    for (l = 0; l < levels; l++)
        n += level_bytes((D3DFORMAT)format, level_dim(width, l), level_dim(height, l), pitch);
    return n;
}

static void tex_release(host_tex_t *e)
{
    int s;

    if (!e->tex)
        return;
    for (s = 0; s < 4; s++)             /* xbox_d3d8 keeps the bound pointer */
        g_dev->lpVtbl->SetTexture(g_dev, s, NULL);
    e->tex->lpVtbl->Release(e->tex);
    e->tex = NULL;
}

/* Copy each level from the guest layout into xbox_d3d8's (the same bytes:
 * levels back to back; a linear format's rows at the guest pitch). */
static int tex_upload(IDirect3DTexture8 *tex, const d3dh_tex_t *t)
{
    const uint8_t *src = t->bits;
    uint32_t l;

    for (l = 0; l < t->levels; l++) {
        uint32_t w = level_dim(t->width, l), h = level_dim(t->height, l);
        uint32_t n = level_bytes((D3DFORMAT)t->format, w, h, t->pitch);
        D3DLOCKED_RECT lr;
        if (FAILED(tex->lpVtbl->LockRect(tex, l, &lr, NULL, 0)))
            return -1;
        if (t->format == XDK_FMT_P8) {  /* S4: indices -> ARGB, texel for texel (swizzled order kept) */
            uint32_t *dst = (uint32_t *)lr.pBits, i, mask = t->pal_entries - 1;
            for (i = 0; i < w * h; i++)
                dst[i] = t->palette[src[i] & mask];
        } else if (t->pitch) {
            uint32_t row = d3d8_row_pitch((D3DFORMAT)t->format, w), y;
            for (y = 0; y < h; y++)
                memcpy((uint8_t *)lr.pBits + (size_t)y * lr.Pitch, src + (size_t)y * t->pitch,
                       row < t->pitch ? row : t->pitch);
        } else {
            memcpy(lr.pBits, src, n);
        }
        tex->lpVtbl->UnlockRect(tex, l);
        src += n;
    }
    return 0;
}

static IDirect3DTexture8 *tex_get(const d3dh_tex_t *t)
{
    host_tex_t *e = NULL, *oldest = NULL;
    uint32_t i, h = (t->key >> 4) * 2654435761u;

    for (i = 0; i < RT_SLOTS; i++)      /* S4: what the host rendered, not guest memory */
        if (g_rt[i].tex && g_rt[i].data == t->data) {
            g_st.rt_sampled++;
            if (g_cur_target == (int)i)
                g_st.rt_feedback++;     /* D3D11 unbinds the view: the stage samples black */
            return g_rt[i].tex;
        }
    g_tex_clock++;
    for (i = 0; i < TEX_CACHE_SLOTS; i++) {
        host_tex_t *s = &g_tex[(h + i) & (TEX_CACHE_SLOTS - 1)];
        if (s->tex && s->desc.key == t->key) {
            e = s;
            break;
        }
        if (!s->tex) {
            if (!e)
                e = s;
            break;
        }
        if (!oldest || s->last_use < oldest->last_use)
            oldest = s;
    }
    if (!e) {                           /* full along this probe run: evict */
        e = oldest;
        tex_release(e);
        g_st.tex_evictions++;
    }
    if (e->tex && (e->desc.data != t->data || e->desc.format != t->format || e->desc.width != t->width ||
                   e->desc.height != t->height || e->desc.levels != t->levels || e->desc.pitch != t->pitch ||
                   e->desc.sig != t->sig || e->desc.pal_sig != t->pal_sig))
        tex_release(e);
    if (!e->tex) {
        IDirect3DTexture8 *tex = NULL;
        uint32_t host_fmt = t->format == XDK_FMT_P8 ? XDK_FMT_A8R8G8B8 : t->format;
        if (t->format == XDK_FMT_P8 && (!t->palette || !t->pal_entries))
            return NULL;
        if (FAILED(g_dev->lpVtbl->CreateTexture(g_dev, t->width, t->height, t->levels, 0,
                                                (D3DFORMAT)host_fmt, D3DPOOL_MANAGED, &tex)) ||
            tex_upload(tex, t) != 0) {
            if (tex)
                tex->lpVtbl->Release(tex);
            if (++g_st.tex_upload_fails <= 5)
                fprintf(stderr, "[D3DB] host texture upload failed: header %08X format %02X %ux%u levels %u\n",
                        t->key, t->format, t->width, t->height, t->levels);
            return NULL;
        }
        e->tex = tex;
        e->desc = *t;
        e->desc.bits = NULL;
        e->desc.palette = NULL;
        g_st.tex_uploads++;
        if (t->format == XDK_FMT_P8)
            g_st.p8_uploads++;
    }
    e->last_use = g_tex_clock;
    return e->tex;
}

void d3dh_texture_touch(uint32_t data)
{
    uint32_t i;

    if (!g_ready)
        return;
    EnterCriticalSection(&g_lock);
    for (i = 0; i < TEX_CACHE_SLOTS; i++)
        if (g_tex[i].tex && data >= g_tex[i].desc.data && data - g_tex[i].desc.data < g_tex[i].desc.bytes)
            g_tex[i].desc.sig ^= 0x80000000u;   /* no longer matches: re-upload at its next draw */
    LeaveCriticalSection(&g_lock);
}

/* ── Draws (S3) ────────────────────────────────────────────── */

/* xbox_d3d8's fixed-function vertex shader maps pre-transformed (XYZRHW)
 * x and y through the back buffer's size, whatever the viewport: on a
 * 128x128 render target a full-target quad lands in its top-left fifth.
 * D3D takes them as pixels of the render target. So such vertices are
 * copied and rescaled, x' = (x - vp.X) * back_w / vp.W (y likewise), which
 * the viewport then maps back to pixel x. Identity for a full-screen
 * viewport on the back buffer (the splash). Called with g_lock held. */
static const void *rescale_rhw(const void *verts, uint32_t count, uint32_t stride, const D3DVIEWPORT8 *vp)
{
    static uint8_t *buf;
    static size_t cap;
    size_t n = (size_t)count * stride;
    float sx, sy;
    uint32_t i;

    if (!vp->Width || !vp->Height || (vp->X == 0 && vp->Y == 0 && vp->Width == g_w && vp->Height == g_h))
        return verts;
    if (n > cap) {
        uint8_t *nb = (uint8_t *)realloc(buf, n);
        if (!nb)
            return verts;
        buf = nb;
        cap = n;
    }
    memcpy(buf, verts, n);
    sx = (float)g_w / (float)vp->Width;
    sy = (float)g_h / (float)vp->Height;
    for (i = 0; i < count; i++) {
        float *p = (float *)(buf + (size_t)i * stride);
        p[0] = (p[0] - (float)vp->X) * sx;
        p[1] = (p[1] - (float)vp->Y) * sy;
    }
    return buf;
}

/* Fixed-function 3D: world, view and projection, the material, and the
 * enabled lights packed from index 0 (the rest disabled). The texture
 * matrices stay identity: the game never sets TEXTURETRANSFORMFLAGS (the
 * bridge counts it if it does). Called with g_lock held. */
static void apply_ff(const d3dh_ff_t *ff, int lit)
{
    static unsigned enabled;            /* host lights enabled by the last call */
    D3DLIGHT8 l;
    D3DMATERIAL8 m;
    unsigned i;

    g_dev->lpVtbl->SetTransform(g_dev, D3DTS_WORLD, (const D3DMATRIX *)ff->world);
    g_dev->lpVtbl->SetTransform(g_dev, D3DTS_VIEW, (const D3DMATRIX *)ff->view);
    g_dev->lpVtbl->SetTransform(g_dev, D3DTS_PROJECTION, (const D3DMATRIX *)ff->proj);
    memcpy(&m, ff->material, sizeof(m));
    g_dev->lpVtbl->SetMaterial(g_dev, &m);
    for (i = 0; i < ff->n_lights && i < D3DH_MAX_LIGHTS; i++) {
        memcpy(&l, ff->light[i], sizeof(l));
        g_dev->lpVtbl->SetLight(g_dev, i, &l);
        g_dev->lpVtbl->LightEnable(g_dev, i, TRUE);
    }
    for (; i < enabled; i++)
        g_dev->lpVtbl->LightEnable(g_dev, i, FALSE);
    enabled = ff->n_lights;
    g_st.ff_draws++;
    if (lit)
        g_st.lit_draws++;
}

/* ── Vertex programs (S5 part 1) ───────────────────────────────
 *
 * A guest program is created once per microcode hash through xbox_d3d8's
 * CreateVertexShader (its translator, d3d8_vsh.c, fixed locally in
 * xboxrecomp for this: notes 18.38; its device vtable has no
 * CreateVertexShader, so d3d8_vsh_create_shader directly), and bound with
 * SetVertexShader. The
 * constants arrive whole with each draw; only the registers that differ
 * from the last upload are passed to SetVertexShaderConstant, which
 * xbox_d3d8 then uploads at its draw. */
#define VS_SLOTS 64
static struct {
    uint32_t key;
    DWORD handle;                       /* 0: couldn't be created */
    uint16_t inputs;
} g_vs[VS_SLOTS];
static unsigned g_vs_n;
static float g_vs_consts[192 * 4];
static int g_vs_consts_valid;

/* Called with g_lock held. */
static int vs_slot(const d3dh_vprog_t *p)
{
    static NV2AVshProgram parsed;       /* ~30 KB: not on the stack */
    unsigned i;

    for (i = 0; i < g_vs_n; i++)
        if (g_vs[i].key == p->key)
            return (int)i;
    if (g_vs_n == VS_SLOTS)
        return -1;
    g_vs[i].key = p->key;
    d3d8_vsh_parse((const DWORD *)p->code, (int)p->n_insns, &parsed);
    g_vs[i].inputs = parsed.inputs_read;
    if (FAILED(d3d8_vsh_create_shader((const DWORD *)p->code, (int)p->n_insns, &g_vs[i].handle))) {
        g_vs[i].handle = 0;
        g_st.vs_fails++;
    } else {
        g_st.vs_created++;
    }
    g_vs_n++;
    return (int)i;
}

uint16_t d3dh_vs_inputs(const d3dh_vprog_t *p)
{
    int i;
    uint16_t r;

    if (!g_ready)
        return 0;
    EnterCriticalSection(&g_lock);
    i = vs_slot(p);
    r = i < 0 ? 0 : g_vs[i].inputs;
    LeaveCriticalSection(&g_lock);
    return r;
}

/* Bind the program and upload the constants that changed. Returns 0, or -1
 * if the host has no shader for it. Called with g_lock held. */
static int vs_bind(const d3dh_vprog_t *p)
{
    int i = vs_slot(p), lo = -1, hi = -1, k;

    if (i < 0 || !g_vs[i].handle)
        return -1;
    for (k = 0; k < 192; k++)
        if (!g_vs_consts_valid || memcmp(&g_vs_consts[4 * k], &p->consts[4 * k], 16) != 0) {
            if (lo < 0)
                lo = k;
            hi = k;
        }
    if (lo >= 0) {
        memcpy(&g_vs_consts[4 * lo], &p->consts[4 * lo], 16 * (size_t)(hi - lo + 1));
        g_dev->lpVtbl->SetVertexShaderConstant(g_dev, lo, &p->consts[4 * lo], (DWORD)(hi - lo + 1));
        g_vs_consts_valid = 1;
        g_st.vs_const_uploads++;
    }
    g_dev->lpVtbl->SetVertexShader(g_dev, g_vs[i].handle);
    return 0;
}

int d3dh_draw_up(const d3dh_draw_t *d)
{
    D3DVIEWPORT8 vp;
    HRESULT hr;
    unsigned i, s;
    const void *verts;
    int lit = 0;

    if (!g_ready)
        return -1;
    EnterCriticalSection(&g_lock);
    if (bind_target(&d->target) != 0) {
        LeaveCriticalSection(&g_lock);
        return -1;
    }
    vp.X = d->vp_x;
    vp.Y = d->vp_y;
    vp.Width = d->vp_w;
    vp.Height = d->vp_h;
    vp.MinZ = d->vp_minz;
    vp.MaxZ = d->vp_maxz;
    g_dev->lpVtbl->SetViewport(g_dev, &vp);
    /* Textures before stage states: xbox_d3d8's SetTexture edits COLOROP. */
    for (s = 0; s < 4; s++)
        g_dev->lpVtbl->SetTexture(g_dev, s, d->tex[s] ? (IDirect3DBaseTexture8 *)tex_get(d->tex[s]) : NULL);
    for (s = 0; s < 4; s++)
        for (i = 0; i < d->n_tss[s]; i++)
            g_dev->lpVtbl->SetTextureStageState(g_dev, s, (D3DTEXTURESTAGESTATETYPE)d->tss[s][i].state,
                                                d->tss[s][i].value);
    for (i = 0; i < d->n_rs; i++) {
        g_dev->lpVtbl->SetRenderState(g_dev, (D3DRENDERSTATETYPE)d->rs[i].state, d->rs[i].value);
        if (d->rs[i].state == D3DRS_LIGHTING)
            lit = d->rs[i].value != 0;
    }
    if (d->ff)
        apply_ff(d->ff, lit);
    if (d->ps && d->ps->token) {        /* S4: the combiner states, then the token */
        for (i = 0; i < d->ps->n_rs; i++)
            g_dev->lpVtbl->SetRenderState(g_dev, (D3DRENDERSTATETYPE)d->ps->rs[i].state, d->ps->rs[i].value);
        g_dev->lpVtbl->SetPixelShader(g_dev, d->ps->token);
        g_st.ps_draws++;
    } else {
        g_dev->lpVtbl->SetPixelShader(g_dev, 0);
    }
    if (d->vprog) {                     /* S5: the vertices are already in the program's layout */
        if (vs_bind(d->vprog) != 0) {
            g_st.draw_fails++;
            LeaveCriticalSection(&g_lock);
            return -1;
        }
        verts = d->verts;
        g_st.vp_draws++;
    } else {
        g_dev->lpVtbl->SetVertexShader(g_dev, d->fvf);
        verts = (d->fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW ? rescale_rhw(d->verts, d->vert_count, d->stride, &vp)
                                                                   : d->verts;
    }
    if (d->indices) {                   /* xbox_d3d8 uploads vert_count vertices from `verts` */
        hr = g_dev->lpVtbl->DrawIndexedPrimitiveUP(g_dev, (D3DPRIMITIVETYPE)d->prim, 0, d->vert_count,
                                                   d->prim_count, d->indices, D3DFMT_INDEX16, verts, d->stride);
        g_st.indexed_draws++;
    } else {
        hr = g_dev->lpVtbl->DrawPrimitiveUP(g_dev, (D3DPRIMITIVETYPE)d->prim, d->prim_count, verts, d->stride);
    }
    g_st.draws++;
    if (d->target.data)
        g_st.rt_draws++;
    if (FAILED(hr) && ++g_st.draw_fails <= 5)
        fprintf(stderr, "[D3DB] host Draw%sPrimitiveUP failed 0x%08lX (prim %u count %u stride %u fvf %X verts %u)\n",
                d->indices ? "Indexed" : "", (unsigned long)hr, d->prim, d->prim_count, d->stride, d->fvf,
                d->vert_count);
    g_frame_work = 1;
    LeaveCriticalSection(&g_lock);
    return FAILED(hr) ? -1 : 0;
}

/* ── CopyRects (step 35) ───────────────────────────────────── */

#define XDK_FMT_LIN_A8B8G8R8 0x3F      /* R8G8B8A8 in xbox_d3d8, the back buffer's DXGI format */

int d3dh_copy_back(const d3dh_target_t *dst, const int32_t *rects, const int32_t *points, unsigned n)
{
    d3dh_target_t t = *dst;
    ID3D11Texture2D *bb = NULL, *dt;
    ID3D11DeviceContext *ctx;
    D3D11_BOX box;
    unsigned k;
    int i, ok = -1;

    if (!g_ready)
        return -1;
    /* D3D11 copies only within a format family, and the guest's A8R8G8B8 is
     * B8G8R8A8 there: the copy is kept as R8G8B8A8, which samples the same. */
    t.format = XDK_FMT_LIN_A8B8G8R8;
    EnterCriticalSection(&g_lock);
    i = rt_find(&t, 1);
    if (i >= 0 && SUCCEEDED(IDXGISwapChain_GetBuffer(d3d8_GetSwapChain(), 0, &IID_ID3D11Texture2D, (void **)&bb))) {
        ctx = d3d8_GetD3D11Context();
        dt = ((D3D8Texture *)g_rt[i].tex)->d3d11_texture;
        for (k = 0; k < (n ? n : 1); k++) {
            int32_t l = n ? rects[4 * k] : 0, top = n ? rects[4 * k + 1] : 0;
            int32_t r = n ? rects[4 * k + 2] : (int32_t)g_w, b = n ? rects[4 * k + 3] : (int32_t)g_h;
            int32_t x = points ? points[2 * k] : l, y = points ? points[2 * k + 1] : top;
            if (r > (int32_t)g_w)
                r = (int32_t)g_w;
            if (b > (int32_t)g_h)
                b = (int32_t)g_h;
            if (x < 0 || y < 0 || l < 0 || top < 0)
                continue;
            if (r - l > (int32_t)t.width - x)
                r = l + (int32_t)t.width - x;
            if (b - top > (int32_t)t.height - y)
                b = top + (int32_t)t.height - y;
            if (r <= l || b <= top)
                continue;
            box.left = (UINT)l;
            box.top = (UINT)top;
            box.front = 0;
            box.right = (UINT)r;
            box.bottom = (UINT)b;
            box.back = 1;
            ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)dt, 0, (UINT)x, (UINT)y, 0,
                                                      (ID3D11Resource *)bb, 0, &box);
        }
        ID3D11Texture2D_Release(bb);
        g_st.copies++;
        g_frame_work = 1;
        ok = 0;
    } else if (++g_st.copy_fails <= 5) {
        fprintf(stderr, "[D3DB] host CopyRects to Data %08X (%ux%u) failed\n", t.data, t.width, t.height);
    }
    LeaveCriticalSection(&g_lock);
    return ok;
}

/* ── Dumps ─────────────────────────────────────────────────── */

/* Level 0 of a 32-bit colour texture (R8G8B8A8 like the back buffer, or
 * B8G8R8A8 like an A8R8G8B8 render target) as a bottom-up 24-bit BMP, like
 * the executor's, or with `alpha` a 32-bit one that keeps alpha. `staging`
 * caches a staging copy (NULL: a temporary one). */
static void dump_texture(ID3D11Texture2D *src, ID3D11Texture2D **staging, const char *path, int alpha)
{
    ID3D11Device *d = d3d8_GetD3D11Device();
    ID3D11DeviceContext *ctx = d3d8_GetD3D11Context();
    ID3D11Texture2D *st = staging ? *staging : NULL;
    D3D11_TEXTURE2D_DESC desc;
    D3D11_MAPPED_SUBRESOURCE m;
    uint8_t hdr[54], *row;
    uint32_t w, h, y, x, pad, filesz, rowbytes;
    int bgra;
    FILE *f;

    ID3D11Texture2D_GetDesc(src, &desc);
    bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8X8_UNORM;
    if (!bgra && desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
        return;
    if (!st) {
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        if (FAILED(ID3D11Device_CreateTexture2D(d, &desc, NULL, &st)))
            return;
        if (staging)
            *staging = st;
    }
    ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)st, 0, 0, 0, 0, (ID3D11Resource *)src, 0, NULL);
    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)st, 0, D3D11_MAP_READ, 0, &m))) {
        if (!staging)
            ID3D11Texture2D_Release(st);
        return;
    }

    w = desc.Width;
    h = desc.Height;
    rowbytes = w * (alpha ? 4 : 3);
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
        hdr[28] = alpha ? 32 : 24;
        fwrite(hdr, 1, sizeof(hdr), f);
        row = (uint8_t *)malloc(rowbytes + pad);
        for (y = h; row && y-- > 0; ) {
            const uint8_t *px = (const uint8_t *)m.pData + (size_t)y * m.RowPitch;
            uint32_t k = alpha ? 4 : 3;
            for (x = 0; x < w; x++) {
                row[k * x + 0] = px[4 * x + (bgra ? 0 : 2)];
                row[k * x + 1] = px[4 * x + 1];
                row[k * x + 2] = px[4 * x + (bgra ? 2 : 0)];
                if (alpha)
                    row[k * x + 3] = px[4 * x + 3];
            }
            memset(row + rowbytes, 0, pad);
            fwrite(row, 1, rowbytes + pad, f);
        }
        free(row);
        fclose(f);
    }
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)st, 0);
    if (!staging)
        ID3D11Texture2D_Release(st);
}

static void dump_back_buffer(const char *path)
{
    ID3D11Texture2D *bb = NULL;

    if (FAILED(IDXGISwapChain_GetBuffer(d3d8_GetSwapChain(), 0, &IID_ID3D11Texture2D, (void **)&bb)))
        return;
    dump_texture(bb, &g_staging, path, 0);
    ID3D11Texture2D_Release(bb);
}

/* S4: every host render target, as <dump path without .bmp>_rt_<Data>.bmp. */
static void dump_targets(const char *path)
{
    char p[640];
    size_t n = strlen(path);
    int i;

    for (i = 0; i < RT_SLOTS; i++)
        if (g_rt[i].tex) {
            snprintf(p, sizeof(p), "%.*s_rt_%08X.bmp", (int)(n > 4 ? n - 4 : n), path, g_rt[i].data);
            dump_texture(((D3D8Texture *)g_rt[i].tex)->d3d11_texture, NULL, p, 0);
        }
}

/* S4: upload a guest texture the way a draw would, without drawing, and
 * write its level 0 as a 32-bit BMP (the P8 check). */
int d3dh_texture_check(const d3dh_tex_t *t, const char *path)
{
    IDirect3DTexture8 *tex;
    int ok = -1;

    if (!g_ready)
        return -1;
    EnterCriticalSection(&g_lock);
    tex = tex_get(t);
    if (tex) {
        dump_texture(((D3D8Texture *)tex)->d3d11_texture, NULL, path, 1);
        ok = 0;
    }
    LeaveCriticalSection(&g_lock);
    return ok;
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
        ((g_frame_work && g_dump_first < DUMP_FIRST_FRAMES) || now - g_last_dump_ms >= DUMP_PERIOD_MS ||
         (g_dump_every && swap_no % g_dump_every == 0) || (swap_no >= g_dump_from && swap_no <= g_dump_to))) {
        char path[600];
        snprintf(path, sizeof(path), "%s%03u.bmp", g_dump_prefix, g_dump_seq++);
        dump_back_buffer(path);
        if (g_dump_rt)
            dump_targets(path);
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

/* ── Pixel-shader probe (S4) ───────────────────────────────── */

#define PROBE_SIZE 4

int d3dh_ps_probe(const d3dh_ps_t *ps, uint32_t diffuse, uint32_t specular, const uint32_t tex_argb[4],
                  uint32_t *out_argb)
{
    static IDirect3DTexture8 *rt_tex, *src[4];
    static IDirect3DSurface8 *rt_surf, *rt_depth;
    static ID3D11Texture2D *staging;
    static const d3dh_state_t fixed_rs[] = {
        { 7, 0 }, { 14, 0 }, { 27, 0 }, { 15, 0 }, { 28, 0 }, { 22, 1 }, { 52, 0 }, { 168, 0xF },
        { 137, 0 }, { 8, 3 }, { 9, 2 }, { 29, 1 },
    };  /* ZENABLE, ZWRITEENABLE, ALPHABLENDENABLE, ALPHATESTENABLE, FOGENABLE, CULLMODE NONE, STENCILENABLE,
         * COLORWRITEENABLE, LIGHTING, FILLMODE SOLID, SHADEMODE GOURAUD, SPECULARENABLE */
    struct {
        float x, y, z, rhw;
        uint32_t diffuse, specular;
        float uv[4][2];
    } v[4];
    D3DVIEWPORT8 vp;
    D3DLOCKED_RECT lr;
    D3D11_MAPPED_SUBRESOURCE m;
    ID3D11DeviceContext *ctx;
    HRESULT hr = S_OK;
    unsigned i, s;
    int ok = -1;

    if (!g_ready || !ps || !ps->token)
        return -1;
    EnterCriticalSection(&g_lock);
    ctx = d3d8_GetD3D11Context();
    if (!rt_tex) {
        D3D11_TEXTURE2D_DESC desc;
        hr = g_dev->lpVtbl->CreateTexture(g_dev, PROBE_SIZE, PROBE_SIZE, 1, D3DUSAGE_RENDERTARGET,
                                          (D3DFORMAT)XDK_FMT_A8R8G8B8, D3DPOOL_DEFAULT, &rt_tex);
        if (SUCCEEDED(hr))
            hr = rt_tex->lpVtbl->GetSurfaceLevel(rt_tex, 0, &rt_surf);
        if (SUCCEEDED(hr))
            hr = g_dev->lpVtbl->CreateDepthStencilSurface(g_dev, PROBE_SIZE, PROBE_SIZE, D3DFMT_D24S8,
                                                          D3DMULTISAMPLE_NONE, &rt_depth);
        for (s = 0; s < 4 && SUCCEEDED(hr); s++)
            hr = g_dev->lpVtbl->CreateTexture(g_dev, 1, 1, 1, 0, (D3DFORMAT)XDK_FMT_A8R8G8B8, D3DPOOL_MANAGED,
                                              &src[s]);
        if (SUCCEEDED(hr)) {
            ID3D11Texture2D_GetDesc(((D3D8Texture *)rt_tex)->d3d11_texture, &desc);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;
            hr = ID3D11Device_CreateTexture2D(d3d8_GetD3D11Device(), &desc, NULL, &staging);
        }
        if (FAILED(hr)) {
            fprintf(stderr, "[D3DB] host pixel-shader probe unavailable: 0x%08lX\n", (unsigned long)hr);
            rt_tex = NULL;              /* leaks what was made; tried once per call */
            LeaveCriticalSection(&g_lock);
            return -1;
        }
    }
    for (s = 0; s < 4; s++)
        if (SUCCEEDED(src[s]->lpVtbl->LockRect(src[s], 0, &lr, NULL, 0))) {
            *(uint32_t *)lr.pBits = tex_argb[s];
            src[s]->lpVtbl->UnlockRect(src[s], 0);
        }

    g_dev->lpVtbl->SetRenderTarget(g_dev, rt_surf, rt_depth);
    g_cur_target = -2;                  /* the next mirrored draw rebinds its own */
    g_dev->lpVtbl->Clear(g_dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0x7F7F7F7F, 1.0f, 0);
    vp.X = vp.Y = 0;
    vp.Width = vp.Height = PROBE_SIZE;
    vp.MinZ = 0.0f;
    vp.MaxZ = 1.0f;
    g_dev->lpVtbl->SetViewport(g_dev, &vp);
    for (s = 0; s < 4; s++) {
        g_dev->lpVtbl->SetTexture(g_dev, s, (IDirect3DBaseTexture8 *)src[s]);
        g_dev->lpVtbl->SetTextureStageState(g_dev, s, D3DTSS_MAGFILTER, 1);
        g_dev->lpVtbl->SetTextureStageState(g_dev, s, D3DTSS_MINFILTER, 1);
        g_dev->lpVtbl->SetTextureStageState(g_dev, s, D3DTSS_MIPFILTER, 0);
        g_dev->lpVtbl->SetTextureStageState(g_dev, s, D3DTSS_ADDRESSU, 3);
        g_dev->lpVtbl->SetTextureStageState(g_dev, s, D3DTSS_ADDRESSV, 3);
        g_dev->lpVtbl->SetTextureStageState(g_dev, s, D3DTSS_TEXCOORDINDEX, s);
    }
    for (i = 0; i < sizeof(fixed_rs) / sizeof(fixed_rs[0]); i++)
        g_dev->lpVtbl->SetRenderState(g_dev, (D3DRENDERSTATETYPE)fixed_rs[i].state, fixed_rs[i].value);
    for (i = 0; i < ps->n_rs; i++)
        g_dev->lpVtbl->SetRenderState(g_dev, (D3DRENDERSTATETYPE)ps->rs[i].state, ps->rs[i].value);
    g_dev->lpVtbl->SetPixelShader(g_dev, ps->token);
    g_dev->lpVtbl->SetVertexShader(g_dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX4);
    memset(v, 0, sizeof(v));
    for (i = 0; i < 4; i++) {
        v[i].x = (i & 1) ? (float)PROBE_SIZE : 0.0f;
        v[i].y = (i & 2) ? (float)PROBE_SIZE : 0.0f;
        v[i].z = 0.5f;
        v[i].rhw = 1.0f;
        v[i].diffuse = diffuse;
        v[i].specular = specular;
        for (s = 0; s < 4; s++)
            v[i].uv[s][0] = v[i].uv[s][1] = 0.5f;
    }
    hr = g_dev->lpVtbl->DrawPrimitiveUP(g_dev, D3DPT_TRIANGLESTRIP, 2, rescale_rhw(v, 4, sizeof(v[0]), &vp), sizeof(v[0]));
    g_dev->lpVtbl->SetPixelShader(g_dev, 0);
    if (SUCCEEDED(hr)) {
        ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)staging, 0, 0, 0, 0,
                                                  (ID3D11Resource *)((D3D8Texture *)rt_tex)->d3d11_texture, 0, NULL);
        if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &m))) {
            *out_argb = *(const uint32_t *)((const uint8_t *)m.pData + m.RowPitch + 4);    /* pixel (1,1) */
            ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
            ok = 0;
        }
    }
    g_st.ps_probes++;
    LeaveCriticalSection(&g_lock);
    return ok;
}

/* ── Vertex-program probe (S5) ─────────────────────────────── */

#define VS_PROBE_SENTINEL 0x01020304u

int d3dh_vs_probe(const d3dh_vprog_t *p, const void *vert, uint32_t stride, const d3dh_draw_t *view, int *px,
                  int *py, uint32_t *argb)
{
    static IDirect3DTexture8 *rt_tex;
    static IDirect3DSurface8 *rt_surf, *rt_depth;
    static ID3D11Texture2D *staging;
    static uint32_t rt_w, rt_h;
    static const d3dh_state_t fixed_rs[] = {
        { 7, 0 }, { 14, 0 }, { 27, 0 }, { 15, 0 }, { 28, 0 }, { 22, 1 }, { 52, 0 }, { 168, 0xF },
        { 137, 0 }, { 8, 3 }, { 9, 2 }, { 29, 0 },
    };  /* as the pixel-shader probe, SPECULARENABLE off */
    uint32_t w = view->vp_x + view->vp_w, h = view->vp_y + view->vp_h, x, y;
    D3DVIEWPORT8 vp;
    D3D11_MAPPED_SUBRESOURCE m;
    ID3D11DeviceContext *ctx;
    HRESULT hr = S_OK;
    unsigned i, s;
    int found = -1;

    if (!g_ready || !w || !h || w > 4096 || h > 4096)
        return -1;
    EnterCriticalSection(&g_lock);
    ctx = d3d8_GetD3D11Context();
    if (!rt_tex || rt_w != w || rt_h != h) {
        D3D11_TEXTURE2D_DESC desc;
        if (rt_tex) {                   /* a new size: drop the old target */
            g_dev->lpVtbl->SetRenderTarget(g_dev, NULL, NULL);
            g_cur_target = -2;
            rt_surf->lpVtbl->Release(rt_surf);
            rt_depth->lpVtbl->Release(rt_depth);
            rt_tex->lpVtbl->Release(rt_tex);
            ID3D11Texture2D_Release(staging);
            rt_tex = NULL;
        }
        hr = g_dev->lpVtbl->CreateTexture(g_dev, w, h, 1, D3DUSAGE_RENDERTARGET, (D3DFORMAT)XDK_FMT_A8R8G8B8,
                                          D3DPOOL_DEFAULT, &rt_tex);
        if (SUCCEEDED(hr))
            hr = rt_tex->lpVtbl->GetSurfaceLevel(rt_tex, 0, &rt_surf);
        if (SUCCEEDED(hr))
            hr = g_dev->lpVtbl->CreateDepthStencilSurface(g_dev, w, h, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &rt_depth);
        if (SUCCEEDED(hr)) {
            ID3D11Texture2D_GetDesc(((D3D8Texture *)rt_tex)->d3d11_texture, &desc);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;
            hr = ID3D11Device_CreateTexture2D(d3d8_GetD3D11Device(), &desc, NULL, &staging);
        }
        if (FAILED(hr)) {
            fprintf(stderr, "[D3DB] host vertex-program probe unavailable: 0x%08lX\n", (unsigned long)hr);
            rt_tex = NULL;              /* leaks what was made; tried again at the next call */
            LeaveCriticalSection(&g_lock);
            return -1;
        }
        rt_w = w;
        rt_h = h;
    }
    g_dev->lpVtbl->SetRenderTarget(g_dev, rt_surf, rt_depth);
    g_cur_target = -2;                  /* the next mirrored draw rebinds its own */
    g_dev->lpVtbl->Clear(g_dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, VS_PROBE_SENTINEL, 1.0f, 0);
    vp.X = view->vp_x;
    vp.Y = view->vp_y;
    vp.Width = view->vp_w;
    vp.Height = view->vp_h;
    vp.MinZ = view->vp_minz;
    vp.MaxZ = view->vp_maxz;
    g_dev->lpVtbl->SetViewport(g_dev, &vp);
    for (s = 0; s < 4; s++)
        g_dev->lpVtbl->SetTexture(g_dev, s, NULL);
    /* Stage 0 passes the vertex colour (CURRENT is the diffuse there; xbox_d3d8
     * reads an argument of 0, D3DTA_DIFFUSE, as its default). */
    g_dev->lpVtbl->SetTextureStageState(g_dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_dev->lpVtbl->SetTextureStageState(g_dev, 0, D3DTSS_COLORARG1, D3DTA_CURRENT);
    g_dev->lpVtbl->SetTextureStageState(g_dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    g_dev->lpVtbl->SetTextureStageState(g_dev, 0, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
    g_dev->lpVtbl->SetTextureStageState(g_dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    g_dev->lpVtbl->SetTextureStageState(g_dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    for (i = 0; i < sizeof(fixed_rs) / sizeof(fixed_rs[0]); i++)
        g_dev->lpVtbl->SetRenderState(g_dev, (D3DRENDERSTATETYPE)fixed_rs[i].state, fixed_rs[i].value);
    g_dev->lpVtbl->SetPixelShader(g_dev, 0);
    if (vs_bind(p) == 0)
        hr = g_dev->lpVtbl->DrawPrimitiveUP(g_dev, D3DPT_POINTLIST, 1, vert, stride);
    else
        hr = E_FAIL;
    if (SUCCEEDED(hr)) {
        ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)staging, 0, 0, 0, 0,
                                                  (ID3D11Resource *)((D3D8Texture *)rt_tex)->d3d11_texture, 0, NULL);
        if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &m))) {
            found = 0;
            for (y = 0; y < h; y++)
                for (x = 0; x < w; x++) {
                    uint32_t c = ((const uint32_t *)((const uint8_t *)m.pData + (size_t)y * m.RowPitch))[x];
                    if (c != VS_PROBE_SENTINEL && found++ == 0) {
                        *px = (int)x;
                        *py = (int)y;
                        *argb = c;
                    }
                }
            ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
        }
    }
    g_st.vs_probes++;
    LeaveCriticalSection(&g_lock);
    return found;
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
