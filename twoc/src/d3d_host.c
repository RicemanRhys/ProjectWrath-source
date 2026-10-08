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
 * (18.32), and DXGI vsync paces to the host monitor (240 presents/s on a
 * 239 Hz display, 18.33). So with the executor off (S7) the host present
 * paces the game itself: a frame limiter on the Xbox's 59.94 Hz clock
 * (divided by the game's D3DPRESENT_INTERVAL), independent of the monitor.
 * Each present waits for its slot, on a high-resolution waitable timer and a
 * short spin; a present later than one whole period restarts the clock
 * instead of hurrying to catch up.
 *
 * When the monitor's refresh is a whole multiple of that rate (239.76 Hz =
 * 4 x 59.94 here), d3d_bridge.c asks for DXGI interval N and no limiter
 * instead (18.45): the display paces, and each frame is shown for exactly N
 * refreshes. The limiter's clock isn't locked to the display, so its frames
 * landed 3, 4 or 5 refreshes apart (step 41's micro-stutter). The swap chain
 * then has a frame-latency waitable object (2 frames), and each present waits
 * on it where the limiter waited, outside g_lock, so Present itself doesn't
 * block while holding it.
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
 *
 * S5 part 2 (notes 18.39): cube maps go into the same cache as xbox_d3d8
 * cube textures, uploaded face by face (tex_upload); its fixed-function and
 * combiner pixel shaders sample a bound cube with the coordinates' xyz.
 *
 * Step 54 (notes 18.57): borderless fullscreen. Alt+Enter on the window, or
 * RECOMP_FULLSCREEN=1 at the start (run_twoc.ps1 -Fullscreen), turns the
 * window into a borderless popup over its monitor, and back. The window
 * thread restyles the window; the visible swap chain follows the client
 * size at the next present (flip_fit), and the picture is drawn into it
 * letterboxed at its own aspect (blit). In a window the swap chain keeps the
 * host back buffer's size and the present copies it as before.
 *
 * Step 54 (notes 18.57): the settings overlay. Esc on the window opens it at
 * the next present, which then holds the game (overlay_pause) and draws Dear
 * ImGui (overlay.cpp) over the frame it was about to show until Esc again.
 * Its keyboard and mouse go to ImGui only; the game reads no keys. Settings
 * apply at once: fullscreen (the window thread), anisotropic filtering (each
 * draw's stage states), the volume (host_audio.cpp), and through d3d_bridge.c
 * the aspect, the render size (d3dh_resize: a render target of the new size
 * stands in for xbox_d3d8's fixed back buffer) and the pacing
 * (d3dh_set_pacing: the swap chain made again when it needs the other kind).
 * Without Esc, and with RECOMP_ANISOTROPY unset, nothing here runs.
 *
 * Step 57: with "Pause the game" off (RECOMP_OVERLAY_PAUSE=0) the overlay
 * doesn't hold the game: each present draws it over the new frame and the
 * game's sound plays on, while the controller gate gives the game an idle
 * pad until the overlay has closed (g_ovl_session). The toggle applies at
 * once, both ways.
 *
 * Step 58 (notes 18.61): anti-aliasing, RECOMP_ANTIALIASING, off unless set,
 * live from the overlay. FXAA and SMAA filter the frame at the present
 * (frame_tex, aa.c): what is shown and dumped. MSAA 2x/4x/8x draws the back
 * buffer and the screen-sized render targets into multisampled surfaces
 * (the stand-in back buffer g_bb is then always used) and resolves each into
 * its texture before anything reads it: a CopyRects, a draw that samples the
 * target, a dump, the present. Off, nothing here changes.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "d3d8_internal.h"      /* xbox_d3d8, and its D3D11 accessors */
#include <dxgi1_3.h>
#include <dwmapi.h>
#include <d3dcompiler.h>
#pragma comment(lib, "dwmapi.lib")

#include "d3d_host.h"
#include "overlay.h"
#include "aa.h"

#define WIN_CLASS          "TwocD3DBridge"
#define WIN_TITLE           "Crash TWOC - D3D bridge (host)"
#define DUMP_FIRST_FRAMES   8
#define DUMP_PERIOD_MS      10000

static CRITICAL_SECTION g_lock;
static volatile LONG g_ready;
static HWND g_hwnd;                     /* visible: the flip-model swap chain */
static HWND g_hwnd_dev;                 /* hidden: xbox_d3d8's own swap chain */
static IDXGISwapChain1 *g_flip;
static unsigned g_w, g_h, g_interval;   /* g_w, g_h: the host back buffer */
static unsigned g_gw, g_gh;             /* the guest back buffer (18.56) */
static double g_wide;                   /* 18.56: the host picture's width / height if wider than the guest's, else 0 */
static int g_dpi_aware;                 /* 18.56: the window in real pixels (a render size was asked for) */
static double g_frame_hz;               /* S7: the frame limiter's rate, 0 = off */
static HANDLE g_latency;                /* vsync pacing (18.45): the frame-latency waitable object */
#define VSYNC_WAIT_MS 50                /* the longest a present waits for it (18.46) */
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
static LARGE_INTEGER g_qpf;                 /* step 60: QueryPerformanceFrequency */
static int g_replay;                    /* step 60, RECOMP_D3D_BRIDGE_REPLAY: passes at each present, 0 off */
static int g_replay_check;              /* RECOMP_D3D_BRIDGE_REPLAY_CHECK: drawn at once too, and compared */

/* Step 54: borderless fullscreen. g_fs and the client size are the window
 * thread's; g_buf_w x g_buf_h is the visible swap chain's, changed only by a
 * present (flip_fit). */
static volatile LONG g_fs;              /* the window is borderless over its monitor */
static int g_fs_at_start;               /* RECOMP_FULLSCREEN */
static volatile LONG g_client_w, g_client_h;
static unsigned g_buf_w, g_buf_h;
static UINT g_flip_flags;               /* the swap chain's flags, for ResizeBuffers */
#define WM_TWOC_FULLSCREEN (WM_APP + 1) /* wParam 1 on, 0 off, 2 toggle */
#define WM_TWOC_FIT (WM_APP + 2)        /* the host picture's size changed: fit the window to it */

/* Step 54: the settings overlay and live settings (overlay.cpp). */
static unsigned g_dev_w, g_dev_h;       /* xbox_d3d8's own back buffer: its size is fixed */
static struct {                         /* stands in for it after a live size change (d3dh_resize), or with MSAA */
    IDirect3DTexture8 *tex;             /* what is sampled, copied and shown */
    IDirect3DSurface8 *surf, *depth;    /* what is drawn into: tex's level 0, or (step 58) multisampled surfaces */
    unsigned ms;                        /* step 58: surf's samples a pixel, 0 when it is tex's level 0 */
    int dirty;                          /* step 58: drawn into since its last resolve */
} g_bb;
static volatile LONG g_aniso = 1;       /* RECOMP_ANISOTROPY: 1 leaves the game's filtering */
static int g_aa;                        /* step 58, RECOMP_ANTIALIASING: TWOC_AA_* */
static unsigned g_msaa = 1;             /* step 58: the samples a pixel MSAA asks for, 1 without */
static volatile LONG g_ovl_open;        /* Esc: the next present runs the overlay */
static SRWLOCK g_pause_lock = SRWLOCK_INIT;
static d3dh_apply_fn g_apply;

/* S4: host render-target textures, by guest Data. A target lives until the
 * guest frees its memory (d3dh_memory_freed) or, with every slot in use, it
 * is the least recently used one (step 52). Before, 16 slots were never given
 * back: the title and every level load make six new 128x128 targets, so by
 * Sea Shell's first visit the last slot went to its 640x480 underwater
 * target and the pause still's 256x256 one got none (the still was black). */
#define RT_SLOTS 32
typedef struct {
    uint32_t data, width, height, format;   /* the guest's description */
    uint32_t hw, hh;                        /* the host texture's size (18.56: a screen-sized one is the host's) */
    IDirect3DTexture8 *tex;
    IDirect3DSurface8 *surf, *depth;
    unsigned last_use;
    IDirect3DSurface8 *ms, *ms_depth;   /* step 58: a screen-sized target's multisampled surfaces, made when bound */
    int dirty;                          /* step 58: ms drawn into since its last resolve */
} host_rt_t;
static host_rt_t g_rt[RT_SLOTS];
static unsigned g_rt_clock;
static int g_cur_target = -1;           /* -1 the back buffer, else a g_rt index; -2 unknown */

/* ── Guest pixels on the host (18.56) ──────────────────────────
 *
 * The host back buffer can be bigger than the guest's (RECOMP_RENDER_SIZE)
 * and wider (RECOMP_WIDESCREEN). The guest keeps drawing for its own 640x480,
 * and the host maps its pixels: on the back buffer, and on a render target of
 * the back buffer's size (made at the host's size, rt_find), x and y scale by
 * host / guest, so the viewport, and with it 3D and vertex programs, covers
 * the whole host picture (the game widens its projection to match,
 * d3db_camera_aspect_scale). Pre-transformed 2D that spans the guest's whole
 * width (fades, full-screen passes, the stencil shadow's quad) is stretched
 * the same way; other 2D (the HUD, menus, text) keeps its 4:3 shape,
 * pillarboxed in the middle: both axes scale by the height, centred. Every
 * other render target keeps its size, mapped 1:1. Off, both scales are 1 and
 * nothing moves. */
typedef struct {
    float sx, sy, ox, oy;
} xmap_t;

static int screen_target(const d3dh_target_t *t)
{
    return !t->data || (t->width == g_gw && t->height == g_gh);
}

static void target_map(const d3dh_target_t *t, int pillar, xmap_t *m)
{
    m->ox = m->oy = 0.0f;
    m->sx = m->sy = 1.0f;
    if (!screen_target(t) || (g_w == g_gw && g_h == g_gh))
        return;
    m->sx = (float)g_w / (float)g_gw;
    m->sy = (float)g_h / (float)g_gh;
    if (pillar && g_wide > 0) {
        m->sx = m->sy;
        m->ox = ((float)g_w - (float)g_gw * m->sy) * 0.5f;
    }
}

static int map_identity(const xmap_t *m)
{
    return m->sx == 1.0f && m->sy == 1.0f && m->ox == 0.0f && m->oy == 0.0f;
}

static LONG map_coord(float o, float s, uint32_t v)
{
    return (LONG)(o + (float)v * s + 0.5f);
}

static void map_viewport(D3DVIEWPORT8 *vp, const xmap_t *m)
{
    LONG x0, y0, x1, y1;

    if (map_identity(m))
        return;
    x0 = map_coord(m->ox, m->sx, vp->X);
    y0 = map_coord(m->oy, m->sy, vp->Y);
    x1 = map_coord(m->ox, m->sx, vp->X + vp->Width);
    y1 = map_coord(m->oy, m->sy, vp->Y + vp->Height);
    vp->X = (DWORD)x0;
    vp->Y = (DWORD)y0;
    vp->Width = (DWORD)(x1 - x0);
    vp->Height = (DWORD)(y1 - y0);
}

/* ── Window thread ─────────────────────────────────────────── */

/* Step 54: borderless fullscreen on the window's monitor, or back to the
 * window as it was. Window thread only. */
static void set_fullscreen(HWND hwnd, int on)
{
    static WINDOWPLACEMENT placed;
    static LONG style;
    MONITORINFO mi;

    if (!on == !g_fs)
        return;
    if (on) {
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        placed.length = sizeof(placed);
        GetWindowPlacement(hwnd, &placed);
        style = GetWindowLongA(hwnd, GWL_STYLE);
        if (!GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
            return;
        InterlockedExchange(&g_fs, 1);
        SetWindowLongA(hwnd, GWL_STYLE, (style & ~(LONG)(WS_OVERLAPPEDWINDOW)) | (LONG)WS_POPUP);
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
        fprintf(stderr, "[D3DB] host window: borderless fullscreen, %ldx%ld at %ld,%ld (Alt+Enter: back)\n",
                mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, mi.rcMonitor.left,
                mi.rcMonitor.top);
    } else {
        InterlockedExchange(&g_fs, 0);
        SetWindowLongA(hwnd, GWL_STYLE, style);
        SetWindowPlacement(hwnd, &placed);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        fprintf(stderr, "[D3DB] host window: windowed\n");
    }
}

/* Step 54: a live change of the host picture's size. In a window, the client
 * becomes the new size, shrunk to 90% of the work area if needed, around the
 * window's centre (in this window's own pixels: real ones if it was made DPI
 * aware, Windows' scaled ones if not). Fullscreen keeps the monitor. */
static void fit_window(HWND hwnd)
{
    RECT w, c, wa;
    LONG cw = (LONG)g_w, ch = (LONG)g_h, fw, fh, cx, cy;

    if (g_fs || !SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0))
        return;
    if (cw > (wa.right - wa.left) * 9 / 10 || ch > (wa.bottom - wa.top) * 9 / 10) {
        double k = (double)(wa.right - wa.left) * 0.9 / cw < (double)(wa.bottom - wa.top) * 0.9 / ch
                       ? (double)(wa.right - wa.left) * 0.9 / cw
                       : (double)(wa.bottom - wa.top) * 0.9 / ch;
        cw = (LONG)(cw * k);
        ch = (LONG)(ch * k);
    }
    GetWindowRect(hwnd, &w);
    GetClientRect(hwnd, &c);
    fw = (w.right - w.left) - (c.right - c.left) + cw;
    fh = (w.bottom - w.top) - (c.bottom - c.top) + ch;
    cx = (w.left + w.right) / 2;
    cy = (w.top + w.bottom) / 2;
    cx = cx - fw / 2 < wa.left ? wa.left + fw / 2 : cx;
    cy = cy - fh / 2 < wa.top ? wa.top + fh / 2 : cy;
    SetWindowPos(hwnd, NULL, cx - fw / 2, cy - fh / 2, fw, fh, SWP_NOZORDER | SWP_NOACTIVATE);
}

/* Step 54: messages for the open overlay, in the swap chain's pixels. */
static int overlay_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    RECT c;
    float sx = 1.0f, sy = 1.0f;

    if (!g_ovl_open)
        return 0;
    switch (msg) {
    case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: case WM_RBUTTONDOWN:
    case WM_RBUTTONUP: case WM_RBUTTONDBLCLK: case WM_MOUSEWHEEL: case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYUP:
    case WM_SETFOCUS: case WM_KILLFOCUS:
        break;
    default:
        return 0;
    }
    if (GetClientRect(hwnd, &c) && c.right > 0 && c.bottom > 0 && g_buf_w && g_buf_h) {
        sx = (float)g_buf_w / (float)c.right;
        sy = (float)g_buf_h / (float)c.bottom;
    }
    ovl_input(msg, wp, lp, sx, sy);
    return msg != WM_SETFOCUS && msg != WM_KILLFOCUS && msg != WM_SYSKEYUP;
}

/* Step 57: the keys held on the window, for the keyboard's pad (controls.c),
 * from its own messages: only while it has the focus, and all let go when
 * it loses it. Left and right Shift, Ctrl and Alt apart. */
static volatile unsigned char g_key_held[256];

static void key_track(UINT msg, WPARAM wp, LPARAM lp)
{
    int vk = (int)wp, down;

    switch (msg) {
    case WM_SYSKEYDOWN:
        if ((lp & (1 << 29)) && wp != VK_MENU)
            return;                     /* pressed with Alt (Alt+Enter, Alt+Tab): never held for the game */
        down = 1;
        break;
    case WM_KEYDOWN:
        down = 1;
        break;
    case WM_KEYUP: case WM_SYSKEYUP:
        down = 0;
        break;
    case WM_KILLFOCUS:
        memset((void *)g_key_held, 0, sizeof(g_key_held));
        return;
    default:
        return;
    }
    if (vk == VK_SHIFT)
        vk = (int)MapVirtualKeyA((UINT)(lp >> 16) & 0xFF, MAPVK_VSC_TO_VK_EX);
    else if (vk == VK_CONTROL)
        vk = (lp & (1 << 24)) ? VK_RCONTROL : VK_LCONTROL;
    else if (vk == VK_MENU)
        vk = (lp & (1 << 24)) ? VK_RMENU : VK_LMENU;
    if (vk == 0 && wp == VK_SHIFT)
        vk = VK_LSHIFT;
    if (vk > 0 && vk < 256)
        g_key_held[vk] = (unsigned char)down;
}

int d3dh_key_down(int vk)
{
    if (vk == VK_MENU)
        return g_key_held[VK_LMENU] || g_key_held[VK_RMENU];
    return vk > 0 && vk < 256 && g_key_held[vk];
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (hwnd == g_hwnd)
        key_track(msg, wp, lp);
    if (hwnd == g_hwnd && msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        if (!(lp & (1 << 30)) && g_ready) { /* step 54: Esc opens and closes the settings overlay */
            if (g_ovl_open && ovl_cancel_capture())
                return 0;               /* step 57: it was waiting for a key or button: that wait ends */
            if (!g_ovl_open)
                ovl_opened();
            InterlockedExchange(&g_ovl_open, !g_ovl_open);
        }
        return 0;
    }
    if (hwnd == g_hwnd && overlay_message(hwnd, msg, wp, lp))
        return 0;
    switch (msg) {
    case WM_CLOSE:
        fprintf(stderr, "[D3DB] host window closed: ending the process\n");
        fflush(stderr);
        TerminateProcess(GetCurrentProcess(), 0);
        return 0;
    case WM_SIZE:                       /* the swap chain follows at the next present */
        if (hwnd == g_hwnd && wp != SIZE_MINIMIZED) {
            InterlockedExchange(&g_client_w, (LONG)LOWORD(lp));
            InterlockedExchange(&g_client_h, (LONG)HIWORD(lp));
        }
        break;
    case WM_SYSKEYDOWN:                 /* Alt+Enter (step 54); the game reads no keys */
        if (wp == VK_RETURN && (lp & (1 << 29)) && !(lp & (1 << 30))) {
            set_fullscreen(hwnd, !g_fs);
            return 0;
        }
        break;
    case WM_SYSCHAR:                    /* no beep for Alt+Enter */
        if (wp == '\r')
            return 0;
        break;
    case WM_TWOC_FULLSCREEN:
        set_fullscreen(hwnd, wp == 2 ? !g_fs : (int)wp);
        return 0;
    case WM_TWOC_FIT:
        fit_window(hwnd);
        return 0;
    case WM_SYSCOMMAND:                 /* step 57: Alt or F10 alone doesn't open the window menu (keys go dead) */
        if ((wp & 0xFFF0) == SC_KEYMENU && lp == 0)
            return 0;
        break;
    case WM_SETCURSOR:                  /* no pointer over the fullscreen picture, unless the overlay is open */
        if (g_fs && !g_ovl_open && LOWORD(lp) == HTCLIENT) {
            SetCursor(NULL);
            return TRUE;
        }
        break;
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
    RECT r, wa = { 0, 0, 0, 0 };
    MSG msg;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    int aware = 0;

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
    if (g_dpi_aware) {
        /* 18.56: a render size was asked for, so the window's pixels are the
         * monitor's (the process isn't DPI aware, and Windows would scale a
         * 1920x1080 client by 225% here). This thread only: the rest of the
         * process, and the window without a render size, stay as before.
         * The client is the back buffer, shrunk to the work area if needed. */
        typedef HANDLE(WINAPI * set_ctx_t)(HANDLE);
        set_ctx_t set_ctx = (set_ctx_t)GetProcAddress(GetModuleHandleA("user32.dll"),
                                                      "SetThreadDpiAwarenessContext");
        if (set_ctx && set_ctx((HANDLE)-4 /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */)) {
            aware = 1;
            if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0)) {
                LONG aw = (wa.right - wa.left) * 9 / 10, ah = (wa.bottom - wa.top) * 9 / 10;
                if (r.right > aw || r.bottom > ah) {
                    double k = (double)aw / r.right < (double)ah / r.bottom ? (double)aw / r.right
                                                                             : (double)ah / r.bottom;
                    r.right = (LONG)(r.right * k);
                    r.bottom = (LONG)(r.bottom * k);
                }
            }
        } else {
            fprintf(stderr, "[D3DB] host: no per-monitor DPI awareness; Windows scales the window\n");
        }
    }
    {
        LONG cw = r.right, ch = r.bottom;
        AdjustWindowRect(&r, style, FALSE);
        g_hwnd_dev = CreateWindowExA(0, WIN_CLASS, WIN_TITLE " (device)", style, 0, 0,
                                     r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
        g_hwnd = CreateWindowExA(0, WIN_CLASS, WIN_TITLE, style, CW_USEDEFAULT, CW_USEDEFAULT,
                                 r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
        if (g_hwnd && aware) {          /* the frame at this monitor's DPI: client exactly cw x ch, centred */
            RECT c, w;
            GetClientRect(g_hwnd, &c);
            GetWindowRect(g_hwnd, &w);
            r.right = (w.right - w.left) + cw - (c.right - c.left);
            r.bottom = (w.bottom - w.top) + ch - (c.bottom - c.top);
            SetWindowPos(g_hwnd, NULL, wa.left + ((wa.right - wa.left) - r.right) / 2,
                         wa.top + ((wa.bottom - wa.top) - r.bottom) / 2, r.right, r.bottom,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            GetClientRect(g_hwnd, &c);
            fprintf(stderr, "[D3DB] host window: client %ldx%ld real pixels\n", c.right - c.left, c.bottom - c.top);
        }
    }
    if (g_hwnd && g_fs_at_start) {      /* step 54: RECOMP_FULLSCREEN; the player asked for it, so it takes focus */
        set_fullscreen(g_hwnd, 1);
        SetForegroundWindow(g_hwnd);
    } else if (g_hwnd) {
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    }
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

static void set_antialiasing(int aa);

int d3dh_create(unsigned width, unsigned height, unsigned host_w, unsigned host_h, double wide, int dpi_aware,
                unsigned interval, double frame_hz, const char *dump_prefix)
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
    QueryPerformanceFrequency(&g_qpf);
    {   /* step 60 */
        const char *e = getenv("RECOMP_D3D_BRIDGE_REPLAY");
        g_replay = e && *e ? atoi(e) : 0;
        g_replay = g_replay < 0 ? 0 : g_replay > 8 ? 8 : g_replay;
        e = getenv("RECOMP_D3D_BRIDGE_REPLAY_CHECK");
        g_replay_check = e && *e && strcmp(e, "0") != 0;
        if (g_replay_check) {
            g_replay = 1;
            fprintf(stderr, "[D3DB] host replay check: each frame drawn at once and again from its recording, the"
                    " two back buffers compared\n");
        }
        if (g_replay)
            fprintf(stderr, "[D3DB] host record and replay: each frame's host draws recorded and drawn at its present"
                    "%s (%d pass%s)\n", g_replay > 1 ? ", again and again as a cost check" : "", g_replay,
                    g_replay > 1 ? "es" : "");
    }
    g_gw = width ? width : 640;
    g_gh = height ? height : 480;
    g_w = host_w ? host_w : g_gw;
    g_h = host_h ? host_h : g_gh;
    g_wide = wide;
    g_dpi_aware = dpi_aware;
    g_interval = interval;
    g_frame_hz = frame_hz > 0 ? frame_hz : 0;
    g_dev_w = g_w;
    g_dev_h = g_h;
    {
        const char *fs = getenv("RECOMP_FULLSCREEN"), *an = getenv("RECOMP_ANISOTROPY");
        int a = an ? atoi(an) : 1;
        g_fs_at_start = fs && *fs && strcmp(fs, "0") != 0;
        g_aniso = a >= 16 ? 16 : a >= 8 ? 8 : a >= 4 ? 4 : a >= 2 ? 2 : 1;
        if (g_aniso > 1)
            fprintf(stderr, "[D3DB] host: anisotropic filtering %ldx on linearly filtered stages\n", g_aniso);
    }
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
            if (g_interval && g_frame_hz <= 0)
                sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
            hr = IDXGIFactory2_CreateSwapChainForHwnd(f, (IUnknown *)d3d8_GetD3D11Device(), g_hwnd, &sd,
                                                      NULL, NULL, &g_flip);
            g_flip_flags = sd.Flags;
            g_buf_w = sd.Width;
            g_buf_h = sd.Height;
            if (SUCCEEDED(hr) && sd.Flags) {
                IDXGISwapChain2 *sc2 = NULL;
                if (SUCCEEDED(IDXGISwapChain1_QueryInterface(g_flip, &IID_IDXGISwapChain2, (void **)&sc2))) {
                    IDXGISwapChain2_SetMaximumFrameLatency(sc2, 2);
                    g_latency = IDXGISwapChain2_GetFrameLatencyWaitableObject(sc2);
                    IDXGISwapChain2_Release(sc2);
                }
                if (!g_latency)
                    fprintf(stderr, "[D3DB] host: no frame-latency waitable object; presents block in Present\n");
            }
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
    {   /* step 58 */
        int aa = twoc_aa_parse(getenv("RECOMP_ANTIALIASING"));
        if (aa != TWOC_AA_OFF)
            set_antialiasing(aa);
    }
    if (g_w != g_gw || g_h != g_gh)     /* 18.56 */
        fprintf(stderr, "[D3DB] host renders %ux%u for the guest's %ux%u: %s; full-width 2D stretched, other 2D"
                " %s\n", g_w, g_h, g_gw, g_gh, g_wide > 0 ? "widescreen" : "4:3",
                g_wide > 0 ? "pillarboxed to 4:3" : "scaled");
    fprintf(stderr, "[D3DB] host device %ux%u, window %p (own thread, flip model), present interval %u%s, frame limiter"
            " %s%.3f Hz%s%s\n",
            g_w, g_h, (void *)g_hwnd, g_interval, g_latency ? " (vsync paced)" : "",
            g_frame_hz > 0 ? "" : "off, ", g_frame_hz,
            g_dump_prefix[0] ? ", dumps " : "", g_dump_prefix[0] ? g_dump_prefix : "");
    return 0;
}

int d3dh_ready(void)
{
    return g_ready;
}

/* ── MSAA (step 58, notes 18.61) ───────────────────────────────
 *
 * With MSAA the back buffer (g_bb) and every render target of the guest
 * back buffer's size (H2 Oh No's underwater target, the CopyRects copy) are
 * drawn into multisampled surfaces, each beside its single-sample texture:
 * the texture is what is sampled, copied, dumped and shown, and it is
 * resolved from the surface before any of that whenever the surface was
 * drawn into since. Smaller targets (the 128x128 passes, the pause still's
 * 256x256) stay single-sampled. A target's surfaces are made when it is
 * first bound for a draw and filled from its texture (aa_copy), so what a
 * CopyRects or an earlier mode left there carries over; a CopyRects into a
 * target with surfaces refills them the same way.
 *
 * xbox_d3d8 makes multisampled textures in CreateRenderTarget and
 * CreateDepthStencilSurface but views them as TEXTURE2D, which D3D11 refuses
 * for a multisampled texture, so those surfaces have no view. The surfaces
 * are made here instead: xbox_d3d8's surface wrapper around the texture
 * (with no view of its own), given a TEXTURE2DMS view. Nothing in xbox_d3d8
 * changes. */

/* The most samples up to g_msaa that colour `fmt` and the D24S8 depth both
 * take, 1 if none. */
static unsigned ms_samples(D3DFORMAT fmt)
{
    ID3D11Device *d = d3d8_GetD3D11Device();
    unsigned n;
    UINT q;

    for (n = g_msaa; n > 1; n /= 2)
        if (SUCCEEDED(ID3D11Device_CheckMultisampleQualityLevels(d, d3d8_to_dxgi_format(fmt), n, &q)) && q &&
            SUCCEEDED(ID3D11Device_CheckMultisampleQualityLevels(d, DXGI_FORMAT_D24_UNORM_S8_UINT, n, &q)) && q)
            return n;
    return 1;
}

/* A multisampled colour (or D24S8 depth) surface xbox_d3d8 can bind. */
static IDirect3DSurface8 *ms_surface(unsigned w, unsigned h, D3DFORMAT fmt, unsigned samples)
{
    ID3D11Device *d = d3d8_GetD3D11Device();
    int depth = fmt == D3DFMT_D24S8;
    D3D11_TEXTURE2D_DESC td;
    ID3D11Texture2D *tex = NULL;
    IDirect3DSurface8 *s;
    D3D8Surface *sf;
    HRESULT hr = E_OUTOFMEMORY;

    memset(&td, 0, sizeof(td));
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = depth ? DXGI_FORMAT_D24_UNORM_S8_UINT : d3d8_to_dxgi_format(fmt);
    td.SampleDesc.Count = samples;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = depth ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_RENDER_TARGET;
    if (FAILED(ID3D11Device_CreateTexture2D(d, &td, NULL, &tex)))
        return NULL;
    s = xbox_d3d8_surface_wrap(tex, w, h, depth ? D3DFMT_A8R8G8B8 : fmt);  /* a colour format: no view made */
    sf = (D3D8Surface *)s;
    if (sf && depth) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dv;
        memset(&dv, 0, sizeof(dv));
        dv.Format = td.Format;
        dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
        hr = ID3D11Device_CreateDepthStencilView(d, (ID3D11Resource *)tex, &dv, &sf->dsv);
        sf->format = D3DFMT_D24S8;
        sf->usage = D3DUSAGE_DEPTHSTENCIL;
    } else if (sf) {
        D3D11_RENDER_TARGET_VIEW_DESC rv;
        memset(&rv, 0, sizeof(rv));
        rv.Format = td.Format;
        rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
        hr = ID3D11Device_CreateRenderTargetView(d, (ID3D11Resource *)tex, &rv, &sf->rtv);
        sf->usage = D3DUSAGE_RENDERTARGET;
    }
    ID3D11Texture2D_Release(tex);
    if (s && FAILED(hr)) {
        s->lpVtbl->Release(s);
        s = NULL;
    }
    return s;
}

/* Resolve multisampled `ms` into `tex`'s level 0. */
static void ms_resolve(IDirect3DTexture8 *tex, IDirect3DSurface8 *ms)
{
    ID3D11Texture2D *dst = ((D3D8Texture *)tex)->d3d11_texture;
    D3D11_TEXTURE2D_DESC dd;

    ID3D11Texture2D_GetDesc(dst, &dd);
    ID3D11DeviceContext_ResolveSubresource(d3d8_GetD3D11Context(), (ID3D11Resource *)dst, 0,
                                           (ID3D11Resource *)((D3D8Surface *)ms)->d3d11_texture, 0, dd.Format);
    g_st.ms_resolves++;
}

/* Fill multisampled `ms` from `tex` (every sample of a pixel gets it) and
 * clear `ms_depth`. Leaves them bound: the caller sets g_cur_target. */
static void ms_fill(IDirect3DSurface8 *ms, IDirect3DSurface8 *ms_depth, IDirect3DTexture8 *tex, unsigned w,
                    unsigned h)
{
    g_dev->lpVtbl->SetRenderTarget(g_dev, ms, ms_depth);
    g_dev->lpVtbl->Clear(g_dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
    if (aa_copy(((D3D8Surface *)ms)->rtv, ((D3D8Texture *)tex)->srv, w, h) == 0)
        g_st.ms_fills++;
}

/* Where the last draw or clear went: its surface now holds more than its
 * texture (MSAA only). Called with g_lock held. */
static void target_drawn(void)
{
    if (g_cur_target == -1 && g_bb.ms)
        g_bb.dirty = 1;
    else if (g_cur_target >= 0 && g_rt[g_cur_target].ms)
        g_rt[g_cur_target].dirty = 1;
}

/* Render target i's texture brought up to date with its multisampled
 * surface. Called with g_lock held. */
static void rt_resolve(int i)
{
    if (g_rt[i].ms && g_rt[i].dirty) {
        ms_resolve(g_rt[i].tex, g_rt[i].ms);
        g_rt[i].dirty = 0;
    }
}

/* Drop render target i's multisampled surfaces, its texture kept up to date. */
static void rt_ms_release(int i)
{
    if (!g_rt[i].ms)
        return;
    rt_resolve(i);
    if (g_cur_target == i) {
        g_dev->lpVtbl->SetRenderTarget(g_dev, NULL, NULL);
        g_cur_target = -2;
    }
    g_rt[i].ms_depth->lpVtbl->Release(g_rt[i].ms_depth);
    g_rt[i].ms->lpVtbl->Release(g_rt[i].ms);
    g_rt[i].ms = g_rt[i].ms_depth = NULL;
}

/* With MSAA, render target i's multisampled surfaces when it is screen-sized,
 * made and filled from its texture if it has none. Called with g_lock held. */
static void rt_ms_make(int i)
{
    unsigned n;

    if (g_msaa <= 1 || g_rt[i].ms || g_rt[i].width != g_gw || g_rt[i].height != g_gh)
        return;
    n = ms_samples((D3DFORMAT)g_rt[i].format);
    if (n <= 1)
        return;
    g_rt[i].ms = ms_surface(g_rt[i].hw, g_rt[i].hh, (D3DFORMAT)g_rt[i].format, n);
    g_rt[i].ms_depth = g_rt[i].ms ? ms_surface(g_rt[i].hw, g_rt[i].hh, D3DFMT_D24S8, n) : NULL;
    if (!g_rt[i].ms_depth) {
        if (g_rt[i].ms)
            g_rt[i].ms->lpVtbl->Release(g_rt[i].ms);
        g_rt[i].ms = NULL;
        if (++g_st.ms_fails <= 5)
            fprintf(stderr, "[D3DB] host render target %d (Data %08X): no %ux multisampled surfaces; drawn without\n",
                    i, g_rt[i].data, n);
        return;
    }
    ms_fill(g_rt[i].ms, g_rt[i].ms_depth, g_rt[i].tex, g_rt[i].hw, g_rt[i].hh);
    g_cur_target = -2;
    g_rt[i].dirty = 0;
    g_st.ms_targets++;
    fprintf(stderr, "[D3DB] host render target %d for guest Data %08X: %ux%u, %ux MSAA\n", i, g_rt[i].data,
            g_rt[i].hw, g_rt[i].hh, n);
}

/* ── Render targets (S4) ───────────────────────────────────── */

/* Release render target i. Called with g_lock held. */
static void rt_release(int i)
{
    int s;

    if (g_cur_target == i) {
        g_dev->lpVtbl->SetRenderTarget(g_dev, NULL, NULL);
        g_cur_target = g_bb.tex ? -2 : -1;      /* step 54: the stand-in back buffer is bound at the next use */
    }
    for (s = 0; s < 4; s++)             /* xbox_d3d8 keeps the bound pointer */
        g_dev->lpVtbl->SetTexture(g_dev, s, NULL);
    if (g_rt[i].ms) {                   /* step 58 */
        g_rt[i].ms_depth->lpVtbl->Release(g_rt[i].ms_depth);
        g_rt[i].ms->lpVtbl->Release(g_rt[i].ms);
    }
    g_rt[i].depth->lpVtbl->Release(g_rt[i].depth);
    g_rt[i].surf->lpVtbl->Release(g_rt[i].surf);
    g_rt[i].tex->lpVtbl->Release(g_rt[i].tex);
    memset(&g_rt[i], 0, sizeof(g_rt[i]));
}

/* The host render target for guest Data, created on first use. Called with
 * g_lock held. Returns its index, or -1. */
static int rt_find(const d3dh_target_t *t, int create)
{
    int i, free_i = -1, lru = -1;
    uint32_t hw, hh;
    IDirect3DTexture8 *tex = NULL;
    IDirect3DSurface8 *surf = NULL, *depth = NULL;
    HRESULT hr;

    for (i = 0; i < RT_SLOTS; i++) {
        if (g_rt[i].tex && g_rt[i].data == t->data) {
            if (g_rt[i].width == t->width && g_rt[i].height == t->height && g_rt[i].format == t->format) {
                g_rt[i].last_use = ++g_rt_clock;
                return i;
            }
            if (!create)
                return -1;
            rt_release(i);              /* the guest reused the memory for another shape */
            free_i = i;
            break;
        }
        if (!g_rt[i].tex && free_i < 0)
            free_i = i;
        if (g_rt[i].tex && i != g_cur_target && (lru < 0 || g_rt[i].last_use < g_rt[lru].last_use))
            lru = i;
    }
    if (!create)
        return -1;
    if (free_i < 0) {                   /* every slot in use: the least recently used one goes */
        if (lru < 0)
            return -1;
        if (++g_st.rt_evicted <= 5)
            fprintf(stderr, "[D3DB] host render targets full: target %d (guest Data %08X, %ux%u) evicted for"
                    " Data %08X\n", lru, g_rt[lru].data, g_rt[lru].width, g_rt[lru].height, t->data);
        rt_release(lru);
        free_i = lru;
    }
    hw = screen_target(t) ? g_w : t->width;     /* 18.56: a screen-sized target at the host's size */
    hh = screen_target(t) ? g_h : t->height;
    hr = g_dev->lpVtbl->CreateTexture(g_dev, hw, hh, 1, D3DUSAGE_RENDERTARGET, (D3DFORMAT)t->format,
                                      D3DPOOL_DEFAULT, &tex);
    if (SUCCEEDED(hr))
        hr = tex->lpVtbl->GetSurfaceLevel(tex, 0, &surf);
    if (SUCCEEDED(hr))
        hr = g_dev->lpVtbl->CreateDepthStencilSurface(g_dev, hw, hh, D3DFMT_D24S8,
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
    g_rt[free_i].hw = hw;
    g_rt[free_i].hh = hh;
    g_rt[free_i].tex = tex;
    g_rt[free_i].surf = surf;
    g_rt[free_i].depth = depth;
    g_rt[free_i].last_use = ++g_rt_clock;
    g_st.rt_created++;
    if (hw != t->width || hh != t->height)
        fprintf(stderr, "[D3DB] host render target %d for guest Data %08X: %ux%u format %02X, at %ux%u on the host\n",
                free_i, t->data, t->width, t->height, t->format, hw, hh);
    else
        fprintf(stderr, "[D3DB] host render target %d for guest Data %08X: %ux%u format %02X\n", free_i, t->data,
                t->width, t->height, t->format);
    /* Start from black, as the guest's memory was (never seen otherwise). */
    g_dev->lpVtbl->SetRenderTarget(g_dev, surf, depth);
    g_dev->lpVtbl->Clear(g_dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
    g_cur_target = g_msaa > 1 ? -2 : free_i;   /* step 58: a draw binds the multisampled surfaces */
    return free_i;
}

/* Bind where a draw or clear goes. Called with g_lock held. Returns 0, or -1
 * if the host has no render target for it. */
static int bind_target(const d3dh_target_t *t)
{
    int i;

    if (!t->data) {
        if (g_cur_target != -1) {
            if (g_bb.tex)               /* step 54: the host picture's size changed */
                g_dev->lpVtbl->SetRenderTarget(g_dev, g_bb.surf, g_bb.depth);
            else
                g_dev->lpVtbl->SetRenderTarget(g_dev, NULL, NULL);     /* the default views */
            g_cur_target = -1;
        }
        return 0;
    }
    i = rt_find(t, 1);
    if (i < 0)
        return -1;
    if (g_cur_target != i) {
        rt_ms_make(i);                  /* step 58 */
        if (g_rt[i].ms)
            g_dev->lpVtbl->SetRenderTarget(g_dev, g_rt[i].ms, g_rt[i].ms_depth);
        else
            g_dev->lpVtbl->SetRenderTarget(g_dev, g_rt[i].surf, g_rt[i].depth);
        g_cur_target = i;
    }
    return 0;
}

/* ── Step 60: record and replay ───────────────────────────────
 *
 * RECOMP_D3D_BRIDGE_REPLAY=N (N 1-8; unset or 0: off, every host call runs
 * at once as before): the host's draws, clears, copies and render-target
 * releases are recorded as they arrive and run, in their order, at the
 * guest's present, N times (the last is what's shown; N > 1 only measures
 * what drawing a frame again costs). A recorded draw owns copies of all it
 * points to (the vertices, indices, states, matrices, the program and its
 * constants, the texture descriptions), and its textures are fetched through
 * the cache when it is recorded and held until the replay, so a texture the
 * guest rewrites later in the frame draws as it was. A texture whose Data is
 * a host render target, or may be one by then (a target drawn, cleared or
 * copied into earlier in the frame), is fetched at the replay instead, where
 * the target's contents are made. Stage 2 interpolates between two recorded
 * frames (notes 18.62). */
enum { RC_DRAW, RC_CLEAR, RC_COPY, RC_FREED };
typedef struct {
    int kind;
    union {
        struct {
            d3dh_draw_t d;
            IDirect3DBaseTexture8 *pin[4];
            uint8_t pinned[4];
        } draw;
        struct {
            d3dh_target_t t;
            uint32_t flags, color, stencil;
            float z;
        } clear;
        struct {
            d3dh_target_t t;
            int32_t *rects, *points;
            unsigned n;
        } copy;
        struct {
            uint32_t data, bytes;
        } freed;
    } u;
} rcmd_t;

#define RA_CHUNK (1u << 20)
typedef struct ra_chunk {
    struct ra_chunk *next;
    size_t used, cap;
    double data[1];                     /* aligned storage, `cap` bytes */
} ra_chunk_t;

static rcmd_t *g_rc;                    /* this frame's commands */
static unsigned g_rc_n, g_rc_cap;
static ra_chunk_t *g_ra_head, *g_ra_cur;
static size_t g_ra_bytes;               /* allocated this frame */
static uint32_t g_rc_targets[64];       /* Data of targets drawn, cleared or copied into this frame */
static unsigned g_rc_targets_n;
static int g_rc_overflow;               /* more than 64: every texture fetched at the replay */

static void clear_exec(const d3dh_target_t *target, uint32_t xdk_flags, uint32_t color, float z, uint32_t stencil);
static int copy_exec(const d3dh_target_t *dst, const int32_t *rects, const int32_t *points, unsigned n);
static void freed_exec(uint32_t data, uint32_t bytes);

/* `n` bytes from this frame's arena (16-aligned; NULL if out of memory). */
static void *ra_alloc(size_t n)
{
    ra_chunk_t *c = g_ra_cur;

    n = (n + 15) & ~(size_t)15;
    if (c && c->used + n <= c->cap) {
        void *p = (uint8_t *)c->data + c->used;
        c->used += n;
        g_ra_bytes += n;
        return p;
    }
    /* The next kept chunk if it is large enough, else a new one after this. */
    if (c && c->next && c->next->cap >= n) {
        c = c->next;
        c->used = 0;
    } else if (!c && g_ra_head && g_ra_head->cap >= n) {
        c = g_ra_head;
        c->used = 0;
    } else {
        size_t cap = n > RA_CHUNK ? n : RA_CHUNK;
        ra_chunk_t *nc = (ra_chunk_t *)malloc(sizeof(ra_chunk_t) + cap);
        if (!nc)
            return NULL;
        nc->cap = cap;
        nc->used = 0;
        if (c) {
            nc->next = c->next;
            c->next = nc;
        } else {
            nc->next = g_ra_head;
            g_ra_head = nc;
        }
        c = nc;
    }
    g_ra_cur = c;
    c->used = n;
    g_ra_bytes += n;
    return c->data;
}

static void *ra_dup(const void *p, size_t n)
{
    void *q;

    if (!p || !n)
        return NULL;
    q = ra_alloc(n);
    if (q)
        memcpy(q, p, n);
    return q;
}

static rcmd_t *rc_push(int kind)
{
    if (g_rc_n == g_rc_cap) {
        unsigned cap = g_rc_cap ? 2 * g_rc_cap : 1024;
        rcmd_t *n = (rcmd_t *)realloc(g_rc, cap * sizeof(rcmd_t));
        if (!n)
            return NULL;
        g_rc = n;
        g_rc_cap = cap;
    }
    memset(&g_rc[g_rc_n], 0, sizeof(rcmd_t));
    g_rc[g_rc_n].kind = kind;
    return &g_rc[g_rc_n++];
}

static void rc_target(uint32_t data)
{
    unsigned i;

    if (!data)
        return;
    for (i = 0; i < g_rc_targets_n; i++)
        if (g_rc_targets[i] == data)
            return;
    if (g_rc_targets_n == sizeof(g_rc_targets) / sizeof(g_rc_targets[0]))
        g_rc_overflow = 1;
    else
        g_rc_targets[g_rc_targets_n++] = data;
}

/* Whether a texture with this Data may sample a host render target at the
 * replay: one exists now, or the frame draws, clears or copies into one. */
static int rc_maybe_target(uint32_t data)
{
    unsigned i;

    if (g_rc_overflow)
        return 1;
    for (i = 0; i < RT_SLOTS; i++)
        if (g_rt[i].tex && g_rt[i].data == data)
            return 1;
    for (i = 0; i < g_rc_targets_n; i++)
        if (g_rc_targets[i] == data)
            return 1;
    return 0;
}

/* Called with g_lock held. */
static void freed_exec(uint32_t data, uint32_t bytes)
{
    int i;

    for (i = 0; i < RT_SLOTS; i++)
        if (g_rt[i].tex && g_rt[i].data >= data && g_rt[i].data - data < bytes) {
            rt_release(i);
            g_st.rt_released++;
        }
}

void d3dh_memory_freed(uint32_t data, uint32_t bytes)
{
    if (!g_ready)
        return;
    EnterCriticalSection(&g_lock);
    if (g_replay && !g_replay_check) {  /* step 60: in its place among the frame's commands */
        rcmd_t *c = rc_push(RC_FREED);
        if (c) {
            c->u.freed.data = data;
            c->u.freed.bytes = bytes;
        } else {
            freed_exec(data, bytes);
        }
    } else {
        freed_exec(data, bytes);
    }
    LeaveCriticalSection(&g_lock);
}

/* ── Clear ─────────────────────────────────────────────────── */

void d3dh_clear(const d3dh_target_t *target, uint32_t xdk_flags, uint32_t color, float z, uint32_t stencil)
{
    if (!g_ready)
        return;
    EnterCriticalSection(&g_lock);
    if (g_replay) {                     /* step 60 */
        rcmd_t *c = rc_push(RC_CLEAR);
        if (g_replay_check)
            clear_exec(target, xdk_flags, color, z, stencil);
        if (c) {
            c->u.clear.t = *target;
            c->u.clear.flags = xdk_flags;
            c->u.clear.color = color;
            c->u.clear.z = z;
            c->u.clear.stencil = stencil;
            rc_target(target->data);
        }
    } else {
        clear_exec(target, xdk_flags, color, z, stencil);
    }
    LeaveCriticalSection(&g_lock);
}

/* Called with g_lock held. */
static void clear_exec(const d3dh_target_t *target, uint32_t xdk_flags, uint32_t color, float z, uint32_t stencil)
{
    DWORD pc = 0;

    if ((xdk_flags & 0xF0) == 0xF0)
        pc |= D3DCLEAR_TARGET;
    if (xdk_flags & 1)
        pc |= D3DCLEAR_ZBUFFER;
    if (xdk_flags & 2)
        pc |= D3DCLEAR_STENCIL;
    if (bind_target(target) != 0)
        return;
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
        target_drawn();
        g_frame_work = 1;
    }
}

/* ── Textures (S3) ─────────────────────────────────────────── */

/* xbox_d3d8 textures made from guest ones, keyed by the guest header. An
 * entry is reused only while the header's Data, format, size and content
 * signature all match; otherwise it is recreated and uploaded again. The
 * least recently used entry makes room when the table is full. */
#define TEX_CACHE_SLOTS 1024

typedef struct {
    d3dh_tex_t desc;                    /* bits unused once uploaded */
    IDirect3DBaseTexture8 *tex;         /* an IDirect3DTexture8, or an IDirect3DCubeTexture8 (desc.cube) */
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
 * levels back to back; a linear format's rows at the guest pitch). A cube
 * map is six such chains, `face_bytes` apart. */
static int tex_upload(IDirect3DBaseTexture8 *base, const d3dh_tex_t *t)
{
    IDirect3DTexture8 *tex = (IDirect3DTexture8 *)base;
    IDirect3DCubeTexture8 *cube = (IDirect3DCubeTexture8 *)base;
    const uint8_t *src = t->bits;
    uint32_t l, face = 0;

    for (l = 0; l < t->levels; l++) {
        uint32_t w = level_dim(t->width, l), h = level_dim(t->height, l);
        uint32_t n = level_bytes((D3DFORMAT)t->format, w, h, t->pitch);
        D3DLOCKED_RECT lr;
        if (FAILED(t->cube ? cube->lpVtbl->LockRect(cube, (D3DCUBEMAP_FACES)face, l, &lr, NULL, 0)
                           : tex->lpVtbl->LockRect(tex, l, &lr, NULL, 0)))
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
        if (t->cube)
            cube->lpVtbl->UnlockRect(cube, (D3DCUBEMAP_FACES)face, l);
        else
            tex->lpVtbl->UnlockRect(tex, l);
        src += n;
        if (t->cube && l + 1 == t->levels && ++face < 6) {
            src = t->bits + (size_t)face * t->face_bytes;
            l = (uint32_t)-1;           /* the next face's chain */
        }
    }
    return 0;
}

static IDirect3DBaseTexture8 *tex_get(const d3dh_tex_t *t)
{
    host_tex_t *e = NULL, *oldest = NULL;
    uint32_t i, h = (t->key >> 4) * 2654435761u;

    for (i = 0; i < RT_SLOTS; i++)      /* S4: what the host rendered, not guest memory */
        if (g_rt[i].tex && g_rt[i].data == t->data) {
            g_rt[i].last_use = ++g_rt_clock;
            g_st.rt_sampled++;
            if (g_cur_target == (int)i)
                g_st.rt_feedback++;     /* D3D11 unbinds the view: the stage samples black */
            rt_resolve((int)i);         /* step 58: what the multisampled surface holds (with MSAA, even while bound) */
            return (IDirect3DBaseTexture8 *)g_rt[i].tex;
        }
    g_tex_clock++;
    for (i = 0; i < TEX_CACHE_SLOTS; i++) {
        host_tex_t *s = &g_tex[(h + i) & (TEX_CACHE_SLOTS - 1)];
        /* Step 52: a header and its level count. A stage with MIPFILTER
         * NONE gets level 0 alone (d3d_bridge.c), so one header bound to
         * two stages that filter differently is two host textures; keyed
         * by the header alone, each stage replaced the other's every draw
         * (Bamboozled's ball rings: its highlight map on stages 1 and 2). */
        if (s->tex && s->desc.key == t->key && s->desc.levels == t->levels) {
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
                   e->desc.sig != t->sig || e->desc.pal_sig != t->pal_sig || e->desc.cube != t->cube))
        tex_release(e);
    if (!e->tex) {
        IDirect3DBaseTexture8 *tex = NULL;
        uint32_t host_fmt = t->format == XDK_FMT_P8 ? XDK_FMT_A8R8G8B8 : t->format;
        HRESULT hr;
        if (t->format == XDK_FMT_P8 && (!t->palette || !t->pal_entries))
            return NULL;
        if (t->cube)
            hr = g_dev->lpVtbl->CreateCubeTexture(g_dev, t->width, t->levels, 0, (D3DFORMAT)host_fmt,
                                                  D3DPOOL_MANAGED, (IDirect3DCubeTexture8 **)&tex);
        else
            hr = g_dev->lpVtbl->CreateTexture(g_dev, t->width, t->height, t->levels, 0, (D3DFORMAT)host_fmt,
                                              D3DPOOL_MANAGED, (IDirect3DTexture8 **)&tex);
        if (FAILED(hr) || tex_upload(tex, t) != 0) {
            if (tex)
                tex->lpVtbl->Release(tex);
            if (++g_st.tex_upload_fails <= 5)
                fprintf(stderr, "[D3DB] host texture upload failed: header %08X format %02X %ux%u levels %u%s\n",
                        t->key, t->format, t->width, t->height, t->levels, t->cube ? " cube" : "");
            return NULL;
        }
        e->tex = tex;
        e->desc = *t;
        e->desc.bits = NULL;
        e->desc.palette = NULL;
        g_st.tex_uploads++;
        if (t->format == XDK_FMT_P8)
            g_st.p8_uploads++;
        if (t->cube)
            g_st.cube_uploads++;
    }
    e->last_use = g_tex_clock;
    return e->tex;
}

void d3dh_dump_range(unsigned from, unsigned to)
{
    g_dump_from = from;
    g_dump_to = to;
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
static const void *rescale_rhw_map(const void *verts, uint32_t count, uint32_t stride, const D3DVIEWPORT8 *vp,
                                   const xmap_t *m)
{
    static uint8_t *buf;
    static size_t cap;
    size_t n = (size_t)count * stride;
    float sx, sy;
    uint32_t i;

    if (!vp->Width || !vp->Height ||
        (vp->X == 0 && vp->Y == 0 && vp->Width == g_dev_w && vp->Height == g_dev_h && map_identity(m)))
        return verts;
    if (n > cap) {
        uint8_t *nb = (uint8_t *)realloc(buf, n);
        if (!nb)
            return verts;
        buf = nb;
        cap = n;
    }
    memcpy(buf, verts, n);
    sx = (float)g_dev_w / (float)vp->Width;     /* step 54: xbox_d3d8's back buffer, whatever is bound */
    sy = (float)g_dev_h / (float)vp->Height;
    for (i = 0; i < count; i++) {
        float *p = (float *)(buf + (size_t)i * stride);
        p[0] = (m->ox + p[0] * m->sx - (float)vp->X) * sx;     /* 18.56: guest pixels to the host's first */
        p[1] = (m->oy + p[1] * m->sy - (float)vp->Y) * sy;
    }
    return buf;
}

static const void *rescale_rhw(const void *verts, uint32_t count, uint32_t stride, const D3DVIEWPORT8 *vp)
{
    static const xmap_t one = { 1.0f, 1.0f, 0.0f, 0.0f };

    return rescale_rhw_map(verts, count, stride, vp, &one);
}

/* 18.56: whether a pre-transformed draw spans the guest's whole width (then
 * it is stretched with the 3D, else pillarboxed). */
static int rhw_full_width(const void *verts, uint32_t count, uint32_t stride)
{
    float lo = 1e30f, hi = -1e30f;
    uint32_t i;

    for (i = 0; i < count; i++) {
        float x = *(const float *)((const uint8_t *)verts + (size_t)i * stride);
        lo = x < lo ? x : lo;
        hi = x > hi ? x : hi;
    }
    return lo <= 1.0f && hi >= (float)g_gw - 1.0f;
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
    uint32_t cmask[6];                  /* step 60: constant registers read directly, bit n = cn */
    int rel_base;                       /* lowest base read relative to a0.x, -1 none */
} g_vs[VS_SLOTS];
static unsigned g_vs_n;
static float g_vs_consts[192 * 4];
static int g_vs_consts_valid;

/* Step 60: the constant registers a parsed program reads: each source
 * operand an instruction's units use (MOV and ARL read A; ADD A and C; MAD
 * A, B and C; the other MAC ops A and B; the ILU C) that selects a constant
 * reads the instruction's constant index, or, with a0.x relative, some
 * register from that index on (its base kept in `rel_base`, the lowest). */
static void vs_const_reads(const NV2AVshProgram *pg, uint32_t mask[6], int *rel_base)
{
    int i, s;

    memset(mask, 0, 6 * sizeof(uint32_t));
    *rel_base = -1;
    for (i = 0; i < pg->length; i++) {
        const NV2AVshInstruction *in = &pg->insns[i];
        unsigned used;
        switch (in->mac_op) {
        case NV2A_VSH_MAC_NOP: used = 0; break;
        case NV2A_VSH_MAC_MOV: case NV2A_VSH_MAC_ARL: used = 1; break;
        case NV2A_VSH_MAC_ADD: used = 5; break;
        case NV2A_VSH_MAC_MAD: used = 7; break;
        default: used = 3; break;
        }
        if (in->ilu_op != NV2A_VSH_ILU_NOP)
            used |= 4;
        for (s = 0; s < 3; s++) {
            const NV2AVshSrcOperand *src = &in->mac_src[s];
            if (!(used & (1u << s)) || src->reg_type != NV2A_VSH_REG_CONST || src->reg_index < 0 ||
                src->reg_index >= 192)
                continue;
            if (src->rel_addr) {
                if (*rel_base < 0 || src->reg_index < *rel_base)
                    *rel_base = src->reg_index;
            } else {
                mask[src->reg_index >> 5] |= 1u << (src->reg_index & 31);
            }
        }
    }
}

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
    vs_const_reads(&parsed, g_vs[i].cmask, &g_vs[i].rel_base);
    {
        int k, n = 0, lo = -1, hi = -1;
        for (k = 0; k < 192; k++)
            if (g_vs[i].cmask[k >> 5] >> (k & 31) & 1) {
                n++;
                hi = k;
                if (lo < 0)
                    lo = k;
            }
        fprintf(stderr, "[D3DB] host program %08X (%u instructions): reads %d constant registers (c%d-c%d)%s",
                p->key, p->n_insns, n, lo, hi, g_vs[i].rel_base >= 0 ? ", and relative to a0.x from c" : "\n");
        if (g_vs[i].rel_base >= 0)
            fprintf(stderr, "%d\n", g_vs[i].rel_base);
    }
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

int d3dh_vs_consts(const d3dh_vprog_t *p, uint32_t mask[6])
{
    int i, r = -1;

    memset(mask, 0, 6 * sizeof(uint32_t));
    if (!g_ready)
        return -1;
    EnterCriticalSection(&g_lock);
    i = vs_slot(p);
    if (i >= 0) {
        memcpy(mask, g_vs[i].cmask, 6 * sizeof(uint32_t));
        r = g_vs[i].rel_base;
    }
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

/* A draw on the host, now: the body of d3dh_draw_up (step 60: also each
 * recorded draw at its replay). `pinned[s]` set: stage s takes `pin[s]`, the
 * texture fetched when the draw was recorded, instead of a fetch now.
 * Called with g_lock held. */
static int draw_exec(const d3dh_draw_t *d, IDirect3DBaseTexture8 *const *pin, const uint8_t *pinned)
{
    D3DVIEWPORT8 vp;
    xmap_t map;
    HRESULT hr;
    unsigned i, s;
    const void *verts;
    int lit = 0;
    LARGE_INTEGER t0, t1;

    QueryPerformanceCounter(&t0);       /* step 60: the host draw's time */
    if (bind_target(&d->target) != 0)
        return -1;
    vp.X = d->vp_x;
    vp.Y = d->vp_y;
    vp.Width = d->vp_w;
    vp.Height = d->vp_h;
    vp.MinZ = d->vp_minz;
    vp.MaxZ = d->vp_maxz;
    target_map(&d->target, d->pillar, &map);    /* 18.56: guest pixels to the host's */
    map_viewport(&vp, &map);
    g_dev->lpVtbl->SetViewport(g_dev, &vp);
    /* Textures before stage states: xbox_d3d8's SetTexture edits COLOROP.
     * All four are fetched before any is bound (step 52): a fetch that
     * replaces a cache entry unbinds every stage, which took the stages
     * already bound for this draw with it. */
    {
        IDirect3DBaseTexture8 *bound[4];
        for (s = 0; s < 4; s++)
            bound[s] = !d->tex[s] ? NULL : pinned && pinned[s] ? pin[s] : tex_get(d->tex[s]);
        for (s = 0; s < 4; s++)
            g_dev->lpVtbl->SetTexture(g_dev, s, bound[s]);
    }
    for (s = 0; s < 4; s++) {
        /* Step 54, RECOMP_ANISOTROPY: a textured stage the game filters
         * linearly both ways samples anisotropically instead. The bridge
         * sends every stage state with every draw, so off is the game's own. */
        LONG aniso = d->tex[s] ? g_aniso : 1;
        if (aniso > 1) {
            uint32_t mag = 0, min = 0;
            for (i = 0; i < d->n_tss[s]; i++) {
                if (d->tss[s][i].state == D3DTSS_MAGFILTER)
                    mag = d->tss[s][i].value;
                else if (d->tss[s][i].state == D3DTSS_MINFILTER)
                    min = d->tss[s][i].value;
            }
            if (mag != D3DTEXF_LINEAR || min != D3DTEXF_LINEAR)
                aniso = 1;
        }
        for (i = 0; i < d->n_tss[s]; i++) {
            uint32_t v = d->tss[s][i].value;
            if (aniso > 1 && d->tss[s][i].state == D3DTSS_MINFILTER)
                v = D3DTEXF_ANISOTROPIC;
            else if (aniso > 1 && d->tss[s][i].state == D3DTSS_MAXANISOTROPY)
                v = (uint32_t)aniso;
            g_dev->lpVtbl->SetTextureStageState(g_dev, s, (D3DTEXTURESTAGESTATETYPE)d->tss[s][i].state, v);
        }
    }
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
            return -1;
        }
        verts = d->verts;
        g_st.vp_draws++;
    } else {
        g_dev->lpVtbl->SetVertexShader(g_dev, d->fvf);
        verts = d->verts;
        if ((d->fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW) {
            if (g_wide > 0 && screen_target(&d->target)) {      /* 18.56: full width stretches, the rest 4:3 */
                int full = rhw_full_width(d->verts, d->vert_count, d->stride);
                if (full)
                    g_st.rhw_full++;
                else
                    g_st.rhw_pillar++;
                target_map(&d->target, !full, &map);
            }
            verts = rescale_rhw_map(d->verts, d->vert_count, d->stride, &vp, &map);
        }
    }
    if (d->indices) {                   /* xbox_d3d8 uploads vert_count vertices from `verts` */
        hr = g_dev->lpVtbl->DrawIndexedPrimitiveUP(g_dev, (D3DPRIMITIVETYPE)d->prim, 0, d->vert_count,
                                                   d->prim_count, d->indices, D3DFMT_INDEX16, verts, d->stride);
        g_st.indexed_draws++;
    } else {
        hr = g_dev->lpVtbl->DrawPrimitiveUP(g_dev, (D3DPRIMITIVETYPE)d->prim, d->prim_count, verts, d->stride);
    }
    target_drawn();
    g_st.draws++;
    if (d->target.data)
        g_st.rt_draws++;
    if (FAILED(hr) && ++g_st.draw_fails <= 5)
        fprintf(stderr, "[D3DB] host Draw%sPrimitiveUP failed 0x%08lX (prim %u count %u stride %u fvf %X verts %u)\n",
                d->indices ? "Indexed" : "", (unsigned long)hr, d->prim, d->prim_count, d->stride, d->fvf,
                d->vert_count);
    g_frame_work = 1;
    QueryPerformanceCounter(&t1);
    g_st.draw_ms += (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)g_qpf.QuadPart;
    return FAILED(hr) ? -1 : 0;
}

/* Record a draw. Called with g_lock held. Returns 0, or -1 (out of memory:
 * the draw is lost, counted as a failure). */
static int rec_draw(const d3dh_draw_t *d)
{
    rcmd_t *c = rc_push(RC_DRAW);
    d3dh_draw_t *o;
    unsigned s;
    int bad = 0;

    if (!c) {
        g_st.draw_fails++;
        return -1;
    }
    o = &c->u.draw.d;
    *o = *d;
    o->verts = ra_dup(d->verts, (size_t)d->vert_count * d->stride);
    bad |= d->verts && d->vert_count && d->stride && !o->verts;
    o->indices = (const uint16_t *)ra_dup(d->indices, (size_t)d->index_count * sizeof(uint16_t));
    bad |= d->indices && d->index_count && !o->indices;
    o->ff = (const d3dh_ff_t *)ra_dup(d->ff, sizeof(d3dh_ff_t));
    o->rs = (const d3dh_state_t *)ra_dup(d->rs, d->n_rs * sizeof(d3dh_state_t));
    for (s = 0; s < 4; s++)
        o->tss[s] = (const d3dh_state_t *)ra_dup(d->tss[s], d->n_tss[s] * sizeof(d3dh_state_t));
    o->ps = (const d3dh_ps_t *)ra_dup(d->ps, sizeof(d3dh_ps_t));
    if (d->vprog) {
        d3dh_vprog_t *v = (d3dh_vprog_t *)ra_dup(d->vprog, sizeof(d3dh_vprog_t));
        if (v) {
            v->code = (const uint32_t *)ra_dup(d->vprog->code, (size_t)d->vprog->n_insns * 16);
            v->consts = (const float *)ra_dup(d->vprog->consts, 192 * 16);
        }
        o->vprog = v;
        bad |= !v || !v->code || !v->consts;
    }
    for (s = 0; s < 4; s++) {
        if (!d->tex[s])
            continue;
        o->tex[s] = (const d3dh_tex_t *)ra_dup(d->tex[s], sizeof(d3dh_tex_t));
        bad |= !o->tex[s];
    }
    bad |= (d->ff && !o->ff) || (d->ps && !o->ps) || (d->n_rs && !o->rs);
    if (bad) {
        g_rc_n--;
        g_st.draw_fails++;
        return -1;
    }
    /* The textures as they are now; those that may be render targets at the
     * replay are fetched there (fetched here as the old path did, so the
     * cache sees the same sequence). */
    for (s = 0; s < 4; s++) {
        if (!o->tex[s] || rc_maybe_target(o->tex[s]->data))
            continue;
        c->u.draw.pin[s] = tex_get(o->tex[s]);
        c->u.draw.pinned[s] = 1;
        if (c->u.draw.pin[s]) {
            c->u.draw.pin[s]->lpVtbl->AddRef(c->u.draw.pin[s]);
            g_st.replay_pins++;
        }
    }
    rc_target(d->target.data);
    return 0;
}

/* Run this frame's commands `g_replay` times, then forget them. Called with
 * g_lock held, at the present. */
static void replay_run(void)
{
    LARGE_INTEGER t0, t1;
    unsigned p, i, s;

    if (!g_rc_n && !g_ra_bytes)
        return;
    QueryPerformanceCounter(&t0);
    for (p = 0; p < (unsigned)g_replay; p++)
        for (i = 0; i < g_rc_n; i++) {
            const rcmd_t *c = &g_rc[i];
            switch (c->kind) {
            case RC_DRAW:
                draw_exec(&c->u.draw.d, c->u.draw.pin, c->u.draw.pinned);
                break;
            case RC_CLEAR:
                clear_exec(&c->u.clear.t, c->u.clear.flags, c->u.clear.color, c->u.clear.z, c->u.clear.stencil);
                break;
            case RC_COPY:
                copy_exec(&c->u.copy.t, c->u.copy.rects, c->u.copy.points, c->u.copy.n);
                break;
            case RC_FREED:
                freed_exec(c->u.freed.data, c->u.freed.bytes);
                break;
            }
        }
    for (i = 0; i < g_rc_n; i++)
        if (g_rc[i].kind == RC_DRAW)
            for (s = 0; s < 4; s++)
                if (g_rc[i].u.draw.pin[s])
                    g_rc[i].u.draw.pin[s]->lpVtbl->Release(g_rc[i].u.draw.pin[s]);
    QueryPerformanceCounter(&t1);
    g_st.replay_frames++;
    g_st.replay_cmds += g_rc_n;
    g_st.replay_bytes += (double)g_ra_bytes;
    g_st.replay_ms += (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)g_qpf.QuadPart;
    g_rc_n = 0;
    g_ra_cur = NULL;
    g_ra_bytes = 0;
    g_rc_targets_n = 0;
    g_rc_overflow = 0;
}

int d3dh_draw_up(const d3dh_draw_t *d)
{
    int r;

    if (!g_ready)
        return -1;
    EnterCriticalSection(&g_lock);
    if (g_replay_check) {               /* at once, as without replay, and recorded */
        r = draw_exec(d, NULL, NULL);
        rec_draw(d);
    } else if (g_replay) {
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        r = rec_draw(d);
        QueryPerformanceCounter(&t1);
        g_st.rec_ms += (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)g_qpf.QuadPart;
        g_st.rec_draws++;
    } else {
        r = draw_exec(d, NULL, NULL);
    }
    LeaveCriticalSection(&g_lock);
    return r;
}

/* ── CopyRects (step 35) ───────────────────────────────────── */

#define XDK_FMT_LIN_A8B8G8R8 0x3F      /* R8G8B8A8 in xbox_d3d8, the back buffer's DXGI format */

/* The host back buffer's texture, referenced: xbox_d3d8's swap-chain buffer,
 * or since a live size change its stand-in (step 54), resolved from its
 * multisampled surface with MSAA (step 58). NULL if unavailable. */
static ID3D11Texture2D *back_tex(void)
{
    ID3D11Texture2D *t = NULL;

    if (g_bb.tex) {
        if (g_bb.ms && g_bb.dirty) {
            ms_resolve(g_bb.tex, g_bb.surf);
            g_bb.dirty = 0;
        }
        t = ((D3D8Texture *)g_bb.tex)->d3d11_texture;
        ID3D11Texture2D_AddRef(t);
        return t;
    }
    if (FAILED(IDXGISwapChain_GetBuffer(d3d8_GetSwapChain(), 0, &IID_ID3D11Texture2D, (void **)&t)))
        return NULL;
    return t;
}

/* Step 58: the frame as shown, referenced: back_tex, through FXAA or SMAA
 * when one is on (aa.c; the frame as it is if that fails). Called with
 * g_lock held. */
static ID3D11Texture2D *frame_tex(void)
{
    ID3D11Texture2D *src = back_tex(), *out = NULL;

    if (!src || (g_aa != TWOC_AA_FXAA && g_aa != TWOC_AA_SMAA))
        return src;
    if (aa_post(g_aa, src, &out) != 0) {
        g_st.post_aa_fails++;
        return src;
    }
    g_st.post_aa++;
    ID3D11Texture2D_Release(src);
    return out;
}

int d3dh_copy_back(const d3dh_target_t *dst, const int32_t *rects, const int32_t *points, unsigned n)
{
    int ok = 0;

    if (!g_ready)
        return -1;
    EnterCriticalSection(&g_lock);
    if (g_replay) {                     /* step 60: the rects and points copied */
        rcmd_t *c = rc_push(RC_COPY);
        if (g_replay_check)
            ok = copy_exec(dst, rects, points, n);
        if (c) {
            c->u.copy.t = *dst;
            c->u.copy.n = n;
            c->u.copy.rects = n ? (int32_t *)ra_dup(rects, (size_t)n * 16) : NULL;
            c->u.copy.points = n && points ? (int32_t *)ra_dup(points, (size_t)n * 8) : NULL;
            rc_target(dst->data);
            if (n && !c->u.copy.rects) {
                g_rc_n--;
                ok = -1;
            }
        } else {
            ok = -1;
        }
    } else {
        ok = copy_exec(dst, rects, points, n);
    }
    LeaveCriticalSection(&g_lock);
    return ok;
}

/* Called with g_lock held. */
static int copy_exec(const d3dh_target_t *dst, const int32_t *rects, const int32_t *points, unsigned n)
{
    d3dh_target_t t = *dst;
    ID3D11Texture2D *bb = NULL, *dt;
    ID3D11DeviceContext *ctx;
    D3D11_BOX box;
    unsigned k;
    int i, ok = -1;

    /* D3D11 copies only within a format family, and the guest's A8R8G8B8 is
     * B8G8R8A8 there: the copy is kept as R8G8B8A8, which samples the same. */
    t.format = XDK_FMT_LIN_A8B8G8R8;
    i = rt_find(&t, 1);
    if (i >= 0 && (bb = back_tex()) != NULL) {
        static const d3dh_target_t back = { 0, 0, 0, 0 };
        xmap_t ms, md;                  /* 18.56: the guest's rects and points in host pixels */
        target_map(&back, 0, &ms);
        target_map(&t, 0, &md);
        if (!map_identity(&ms) && map_identity(&md) && ++g_st.copy_unscaled <= 5)
            fprintf(stderr, "[D3DB] host CopyRects to Data %08X (%ux%u): the copy isn't scaled to the host's size\n",
                    t.data, t.width, t.height);
        ctx = d3d8_GetD3D11Context();
        dt = ((D3D8Texture *)g_rt[i].tex)->d3d11_texture;
        rt_resolve(i);                  /* step 58: the rects land on what the target holds */
        for (k = 0; k < (n ? n : 1); k++) {
            int32_t l = n ? rects[4 * k] : 0, top = n ? rects[4 * k + 1] : 0;
            int32_t r = n ? rects[4 * k + 2] : (int32_t)g_gw, b = n ? rects[4 * k + 3] : (int32_t)g_gh;
            int32_t x = points ? points[2 * k] : l, y = points ? points[2 * k + 1] : top;
            if (x < 0 || y < 0 || l < 0 || top < 0)
                continue;
            l = map_coord(ms.ox, ms.sx, (uint32_t)l);
            top = map_coord(ms.oy, ms.sy, (uint32_t)top);
            r = r > 0 ? map_coord(ms.ox, ms.sx, (uint32_t)r) : r;
            b = b > 0 ? map_coord(ms.oy, ms.sy, (uint32_t)b) : b;
            x = map_coord(md.ox, md.sx, (uint32_t)x);
            y = map_coord(md.oy, md.sy, (uint32_t)y);
            if (r > (int32_t)g_w)
                r = (int32_t)g_w;
            if (b > (int32_t)g_h)
                b = (int32_t)g_h;
            if (r - l > (int32_t)g_rt[i].hw - x)
                r = l + (int32_t)g_rt[i].hw - x;
            if (b - top > (int32_t)g_rt[i].hh - y)
                b = top + (int32_t)g_rt[i].hh - y;
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
        if (g_rt[i].ms) {               /* step 58: and its multisampled surface from it */
            ms_fill(g_rt[i].ms, g_rt[i].ms_depth, g_rt[i].tex, g_rt[i].hw, g_rt[i].hh);
            g_cur_target = -2;
        }
        g_st.copies++;
        g_frame_work = 1;
        ok = 0;
    } else if (++g_st.copy_fails <= 5) {
        fprintf(stderr, "[D3DB] host CopyRects to Data %08X (%ux%u) failed\n", t.data, t.width, t.height);
    }
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

/* The frame as shown (frame_tex). */
static void dump_back_buffer(const char *path, ID3D11Texture2D *frame)
{
    if (frame)
        dump_texture(frame, &g_staging, path, 0);
}

/* S4: every host render target, as <dump path without .bmp>_rt_<Data>.bmp. */
static void dump_targets(const char *path)
{
    char p[640];
    size_t n = strlen(path);
    int i;

    for (i = 0; i < RT_SLOTS; i++)
        if (g_rt[i].tex) {
            rt_resolve(i);              /* step 58 */
            snprintf(p, sizeof(p), "%.*s_rt_%08X.bmp", (int)(n > 4 ? n - 4 : n), path, g_rt[i].data);
            dump_texture(((D3D8Texture *)g_rt[i].tex)->d3d11_texture, NULL, p, 0);
        }
}

/* S4: upload a guest texture the way a draw would, without drawing, and
 * write its level 0 as a 32-bit BMP (the P8 check). */
int d3dh_texture_check(const d3dh_tex_t *t, const char *path)
{
    IDirect3DBaseTexture8 *tex;
    int ok = -1;

    if (!g_ready || t->cube)
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

/*
 * Vsync pacing: present mid-refresh (18.48).
 *
 * The frame-latency object is signalled at a vblank, so the present that
 * follows the wait was issued 0.2-0.3 ms after one, on the refresh boundary.
 * DWM counts a composed flip's sync interval from the vblank it assigns the
 * present to, and at the boundary that assignment wavers: frames were shown 5
 * refreshes after the one before and the next 3 after it (PresentMon: 20.8
 * then 12.5 ms, animation error -/+4 ms), in runs while the phase stayed
 * there. So after the wait, the present is held until the middle of the
 * refresh: half DWM's period after its last vblank. It costs ~2 ms of latency.
 * Two runs each, menu then Arctic Antics: 5.5-7.3% of frames off the 4-refresh
 * grid before, 2.2% after, and 0-2% in the level. What is left comes in the
 * menu on a 10 Hz rhythm while our presents stay even, so it's DWM's own.
 * RECOMP_D3D_BRIDGE_PRESENT_PHASE=MS sets the point (0 presents at once, as
 * before).
 */
static void present_mid_refresh(void)
{
    static double target_ms = -1;
    static HANDLE timer;
    static LARGE_INTEGER freq;
    DWM_TIMING_INFO ti;
    LARGE_INTEGER now;
    double since, left, period_ms, want, deadline;
    LONGLONG d;

    if (target_ms < 0) {
        const char *e = getenv("RECOMP_D3D_BRIDGE_PRESENT_PHASE");
        target_ms = e && *e ? atof(e) : 1e9;       /* 1e9: half the period, once known */
        QueryPerformanceFrequency(&freq);
        timer = CreateWaitableTimerExW(NULL, NULL, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
                                       TIMER_ALL_ACCESS);
    }
    if (target_ms <= 0)
        return;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    if (FAILED(DwmGetCompositionTimingInfo(NULL, &ti)) || !ti.qpcRefreshPeriod)
        return;
    period_ms = ti.qpcRefreshPeriod * 1000.0 / freq.QuadPart;
    want = target_ms < period_ms ? target_ms : period_ms / 2;
    QueryPerformanceCounter(&now);
    d = ((LONGLONG)now.QuadPart - (LONGLONG)ti.qpcVBlank) % (LONGLONG)ti.qpcRefreshPeriod;
    if (d < 0)
        d += ti.qpcRefreshPeriod;
    since = d * 1000.0 / freq.QuadPart;
    left = want - since;
    if (left < 0 && since > period_ms - 0.7)
        left += period_ms;              /* just before the next vblank: the middle of the next refresh */
    if (left <= 0)
        return;
    deadline = (double)now.QuadPart + left * freq.QuadPart / 1000.0;
    for (;;) {
        QueryPerformanceCounter(&now);
        left = (deadline - (double)now.QuadPart) * 1000.0 / freq.QuadPart;
        if (left <= 0)
            return;
        if (left > 0.8 && timer) {
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)((left - 0.5) * 10000.0);       /* relative, 100 ns */
            if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE))
                WaitForSingleObject(timer, INFINITE);
        } else {
            YieldProcessor();
        }
    }
}

/* S7: wait for this present's slot on the g_frame_hz clock. Outside g_lock,
 * so a loading thread's host calls never queue behind the wait; the game
 * serialises its swaps anyway, and pace_lock keeps the clock consistent if
 * two threads ever swapped at once. */
static void pace(void)
{
    static SRWLOCK pace_lock = SRWLOCK_INIT;
    static LARGE_INTEGER freq;
    static double next;                 /* QPC tick of the next slot, 0 before the first present */
    static HANDLE timer;
    static int timer_tried;
    LARGE_INTEGER now;
    double period, left;

    if (g_latency) {                    /* vsync pacing: wait for room in the queue */
        static int misses;
        DWORD r;

        AcquireSRWLockExclusive(&pace_lock);
        if (!freq.QuadPart)
            QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&now);
        left = (double)now.QuadPart;
        /* A few frames at most. It waited up to a second, and the one time
         * the display didn't release a frame in time (a load, 1 run in 6)
         * the game stood still for 1-2 s. Three misses in a row and the
         * limiter paces for the rest of the run. */
        r = WaitForSingleObjectEx(g_latency, VSYNC_WAIT_MS, TRUE);
        QueryPerformanceCounter(&now);
        g_st.paced++;
        g_st.paced_wait_ms += ((double)now.QuadPart - left) * 1000.0 / (double)freq.QuadPart;
        if (r == WAIT_TIMEOUT) {
            g_st.paced_late++;
            if (++misses >= 3) {
                double hz = d3dh_display_hz();
                g_frame_hz = hz > 0 && g_interval ? hz / g_interval : 60000.0 / 1001.0;
                g_interval = 0;
                g_latency = NULL;
                fprintf(stderr, "[D3DB] host: the display released no frame in %d ms, %d times running;"
                        " the frame limiter paces from here, %.3f Hz\n", VSYNC_WAIT_MS, misses, g_frame_hz);
            }
        } else {
            misses = 0;
            present_mid_refresh();
        }
        ReleaseSRWLockExclusive(&pace_lock);
        return;
    }
    if (g_frame_hz <= 0)
        return;
    AcquireSRWLockExclusive(&pace_lock);
    if (!freq.QuadPart)
        QueryPerformanceFrequency(&freq);
    if (!timer_tried) {
        timer_tried = 1;
        timer = CreateWaitableTimerExW(NULL, NULL, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
                                       TIMER_ALL_ACCESS);
        if (!timer)
            fprintf(stderr, "[D3DB] host: no high-resolution timer (%lu); the limiter sleeps in 1 ms steps\n",
                    GetLastError());
    }
    period = (double)freq.QuadPart / g_frame_hz;
    QueryPerformanceCounter(&now);
    if (next == 0 || (double)now.QuadPart - next > period) {
        if (next != 0)
            g_st.paced_late++;          /* a whole period late: restart the clock from now */
        next = (double)now.QuadPart;
    } else if ((double)now.QuadPart < next) {
        double t0 = (double)now.QuadPart;
        for (;;) {
            left = (next - (double)now.QuadPart) * 1000.0 / (double)freq.QuadPart;   /* ms */
            if (left <= 0)
                break;
            if (left > 1.5) {           /* sleep to ~1 ms before the slot, then spin */
                if (timer) {
                    LARGE_INTEGER due;
                    due.QuadPart = -(LONGLONG)((left - 1.0) * 10000.0);            /* relative, 100 ns */
                    if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE))
                        WaitForSingleObject(timer, INFINITE);
                    else
                        Sleep(1);
                } else {
                    Sleep(1);
                }
            } else {
                YieldProcessor();
            }
            QueryPerformanceCounter(&now);
        }
        g_st.paced_wait_ms += ((double)now.QuadPart - t0) * 1000.0 / (double)freq.QuadPart;
    }
    g_st.paced++;
    next += period;
    ReleaseSRWLockExclusive(&pace_lock);
}

/* The composed display's refresh rate (the primary monitor's), 0 if DWM
 * doesn't say. */
double d3dh_display_hz(void)
{
    DWM_TIMING_INFO ti;

    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    if (FAILED(DwmGetCompositionTimingInfo(NULL, &ti)) || !ti.rateRefresh.uiDenominator)
        return 0;
    return (double)ti.rateRefresh.uiNumerator / ti.rateRefresh.uiDenominator;
}

/* ── The picture into the visible swap chain (step 54) ─────────
 *
 * In a window the swap chain has the host back buffer's size, and a present
 * copies one into the other (as since 18.33). Borderless fullscreen gives it
 * the client's size instead (flip_fit), and the frame is drawn into it
 * letterboxed at its own aspect, linearly filtered, over black (blit): the
 * frame is copied into g_shot (xbox_d3d8's swap-chain buffer can't be
 * sampled) and drawn as one triangle. The blit saves and restores every
 * piece of D3D11 state it sets, since xbox_d3d8 keeps its own bindings. */
static ID3D11VertexShader *g_blit_vs;
static ID3D11PixelShader *g_blit_ps;
static ID3D11SamplerState *g_blit_samp;
static ID3D11Texture2D *g_shot;         /* the frame being shown, sampled by the blit */
static ID3D11ShaderResourceView *g_shot_srv;
static int g_blit_state;                /* 0 not tried, 1 ready, -1 failed */

static const char g_blit_hlsl[] =
    "Texture2D t : register(t0);\n"
    "SamplerState s : register(s0);\n"
    "struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "V vs(uint id : SV_VertexID)\n"
    "{\n"
    "    V o;\n"
    "    o.uv = float2((id << 1) & 2, id & 2);\n"
    "    o.p = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "    return o;\n"
    "}\n"
    "float4 ps(V i) : SV_Target { return float4(t.Sample(s, i.uv).rgb, 1); }\n";

/* Called with g_lock held. */
static int blit_init(void)
{
    ID3D11Device *d = d3d8_GetD3D11Device();
    ID3DBlob *vs = NULL, *ps = NULL, *err = NULL;
    D3D11_SAMPLER_DESC sd;
    HRESULT hr;

    if (g_blit_state)
        return g_blit_state > 0 ? 0 : -1;
    g_blit_state = -1;
    hr = D3DCompile(g_blit_hlsl, sizeof(g_blit_hlsl) - 1, "twoc_blit", NULL, NULL, "vs", "vs_4_0", 0, 0, &vs, &err);
    if (SUCCEEDED(hr))
        hr = D3DCompile(g_blit_hlsl, sizeof(g_blit_hlsl) - 1, "twoc_blit", NULL, NULL, "ps", "ps_4_0", 0, 0, &ps, &err);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateVertexShader(d, ID3D10Blob_GetBufferPointer(vs), ID3D10Blob_GetBufferSize(vs), NULL,
                                             &g_blit_vs);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreatePixelShader(d, ID3D10Blob_GetBufferPointer(ps), ID3D10Blob_GetBufferSize(ps), NULL,
                                            &g_blit_ps);
    if (SUCCEEDED(hr)) {
        memset(&sd, 0, sizeof(sd));
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        hr = ID3D11Device_CreateSamplerState(d, &sd, &g_blit_samp);
    }
    if (FAILED(hr))
        fprintf(stderr, "[D3DB] host blit not available (0x%08lX%s%s): fullscreen shows the frame unscaled\n",
                (unsigned long)hr, err ? ": " : "", err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "");
    else
        g_blit_state = 1;
    if (vs)
        ID3D10Blob_Release(vs);
    if (ps)
        ID3D10Blob_Release(ps);
    if (err)
        ID3D10Blob_Release(err);
    return g_blit_state > 0 ? 0 : -1;
}

/* Copy `src` into g_shot (made again when its size or format changes).
 * Called with g_lock held. */
static int shot_take(ID3D11Texture2D *src)
{
    D3D11_TEXTURE2D_DESC sd, hd;

    ID3D11Texture2D_GetDesc(src, &sd);
    if (g_shot) {
        ID3D11Texture2D_GetDesc(g_shot, &hd);
        if (hd.Width != sd.Width || hd.Height != sd.Height || hd.Format != sd.Format) {
            ID3D11ShaderResourceView_Release(g_shot_srv);
            ID3D11Texture2D_Release(g_shot);
            g_shot_srv = NULL;
            g_shot = NULL;
        }
    }
    if (!g_shot) {
        hd = sd;
        hd.MipLevels = 1;
        hd.ArraySize = 1;
        hd.SampleDesc.Count = 1;
        hd.SampleDesc.Quality = 0;
        hd.Usage = D3D11_USAGE_DEFAULT;
        hd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        hd.CPUAccessFlags = 0;
        hd.MiscFlags = 0;
        if (FAILED(ID3D11Device_CreateTexture2D(d3d8_GetD3D11Device(), &hd, NULL, &g_shot)))
            return -1;
        if (FAILED(ID3D11Device_CreateShaderResourceView(d3d8_GetD3D11Device(), (ID3D11Resource *)g_shot, NULL,
                                                         &g_shot_srv))) {
            ID3D11Texture2D_Release(g_shot);
            g_shot = NULL;
            return -1;
        }
    }
    ID3D11DeviceContext_CopyResource(d3d8_GetD3D11Context(), (ID3D11Resource *)g_shot, (ID3D11Resource *)src);
    return 0;
}

/* Draw g_shot into `dst` (the swap chain's buffer, w x h), centred at its
 * own aspect over black. Called with g_lock held. Returns 0 on success. */
static int blit_draw(ID3D11Texture2D *dst, unsigned w, unsigned h)
{
    ID3D11DeviceContext *ctx = d3d8_GetD3D11Context();
    ID3D11RenderTargetView *rtv = NULL;
    D3D11_TEXTURE2D_DESC sd;
    D3D11_VIEWPORT vp;
    static const FLOAT black[4] = { 0, 0, 0, 1 };
    double k;
    struct {                            /* the state the blit sets, to put back */
        ID3D11RenderTargetView *rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
        ID3D11DepthStencilView *dsv;
        D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        UINT n_vp, sample_mask, stencil_ref;
        ID3D11RasterizerState *rs;
        ID3D11BlendState *bs;
        FLOAT blend_factor[4];
        ID3D11DepthStencilState *dss;
        ID3D11ShaderResourceView *srv;
        ID3D11SamplerState *samp;
        ID3D11VertexShader *vs;
        ID3D11PixelShader *ps;
        ID3D11GeometryShader *gs;
        ID3D11InputLayout *il;
        D3D11_PRIMITIVE_TOPOLOGY topo;
    } old;
    int i;

    if (!g_shot || blit_init() != 0 ||
        FAILED(ID3D11Device_CreateRenderTargetView(d3d8_GetD3D11Device(), (ID3D11Resource *)dst, NULL, &rtv)))
        return -1;
    ID3D11Texture2D_GetDesc(g_shot, &sd);
    k = (double)w / sd.Width < (double)h / sd.Height ? (double)w / sd.Width : (double)h / sd.Height;
    vp.Width = (FLOAT)(int)(sd.Width * k + 0.5);
    vp.Height = (FLOAT)(int)(sd.Height * k + 0.5);
    vp.TopLeftX = (FLOAT)(int)((w - vp.Width) / 2);
    vp.TopLeftY = (FLOAT)(int)((h - vp.Height) / 2);
    vp.MinDepth = 0;
    vp.MaxDepth = 1;

    memset(&old, 0, sizeof(old));
    ID3D11DeviceContext_OMGetRenderTargets(ctx, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, old.rtv, &old.dsv);
    old.n_vp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ID3D11DeviceContext_RSGetViewports(ctx, &old.n_vp, old.vp);
    ID3D11DeviceContext_RSGetState(ctx, &old.rs);
    ID3D11DeviceContext_OMGetBlendState(ctx, &old.bs, old.blend_factor, &old.sample_mask);
    ID3D11DeviceContext_OMGetDepthStencilState(ctx, &old.dss, &old.stencil_ref);
    ID3D11DeviceContext_PSGetShaderResources(ctx, 0, 1, &old.srv);
    ID3D11DeviceContext_PSGetSamplers(ctx, 0, 1, &old.samp);
    ID3D11DeviceContext_VSGetShader(ctx, &old.vs, NULL, NULL);
    ID3D11DeviceContext_PSGetShader(ctx, &old.ps, NULL, NULL);
    ID3D11DeviceContext_GSGetShader(ctx, &old.gs, NULL, NULL);
    ID3D11DeviceContext_IAGetInputLayout(ctx, &old.il);
    ID3D11DeviceContext_IAGetPrimitiveTopology(ctx, &old.topo);

    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, black);
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    ID3D11DeviceContext_RSSetState(ctx, NULL);
    ID3D11DeviceContext_OMSetBlendState(ctx, NULL, NULL, 0xFFFFFFFFu);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx, NULL, 0);
    ID3D11DeviceContext_IASetInputLayout(ctx, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx, g_blit_vs, NULL, 0);
    ID3D11DeviceContext_GSSetShader(ctx, NULL, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, g_blit_ps, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 1, &g_shot_srv);
    ID3D11DeviceContext_PSSetSamplers(ctx, 0, 1, &g_blit_samp);
    ID3D11DeviceContext_Draw(ctx, 3, 0);

    ID3D11DeviceContext_OMSetRenderTargets(ctx, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, old.rtv, old.dsv);
    ID3D11DeviceContext_RSSetViewports(ctx, old.n_vp, old.vp);
    ID3D11DeviceContext_RSSetState(ctx, old.rs);
    ID3D11DeviceContext_OMSetBlendState(ctx, old.bs, old.blend_factor, old.sample_mask);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx, old.dss, old.stencil_ref);
    ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 1, &old.srv);
    ID3D11DeviceContext_PSSetSamplers(ctx, 0, 1, &old.samp);
    ID3D11DeviceContext_VSSetShader(ctx, old.vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, old.ps, NULL, 0);
    ID3D11DeviceContext_GSSetShader(ctx, old.gs, NULL, 0);
    ID3D11DeviceContext_IASetInputLayout(ctx, old.il);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, old.topo);
    for (i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
        if (old.rtv[i])
            ID3D11RenderTargetView_Release(old.rtv[i]);
    if (old.dsv)
        ID3D11DepthStencilView_Release(old.dsv);
    if (old.rs)
        ID3D11RasterizerState_Release(old.rs);
    if (old.bs)
        ID3D11BlendState_Release(old.bs);
    if (old.dss)
        ID3D11DepthStencilState_Release(old.dss);
    if (old.srv)
        ID3D11ShaderResourceView_Release(old.srv);
    if (old.samp)
        ID3D11SamplerState_Release(old.samp);
    if (old.vs)
        ID3D11VertexShader_Release(old.vs);
    if (old.ps)
        ID3D11PixelShader_Release(old.ps);
    if (old.gs)
        ID3D11GeometryShader_Release(old.gs);
    if (old.il)
        ID3D11InputLayout_Release(old.il);
    ID3D11RenderTargetView_Release(rtv);
    return 0;
}

/* Without the blit: the frame's top-left corner, as much as fits. */
static void copy_unscaled(ID3D11Texture2D *dst, ID3D11Texture2D *src)
{
    D3D11_TEXTURE2D_DESC sd;
    D3D11_BOX box;

    ID3D11Texture2D_GetDesc(src, &sd);
    box.left = box.top = box.front = 0;
    box.right = sd.Width < g_buf_w ? sd.Width : g_buf_w;
    box.bottom = sd.Height < g_buf_h ? sd.Height : g_buf_h;
    box.back = 1;
    ID3D11DeviceContext_CopySubresourceRegion(d3d8_GetD3D11Context(), (ID3D11Resource *)dst, 0, 0, 0, 0,
                                              (ID3D11Resource *)src, 0, &box);
}

/* The visible swap chain's size: the client's in borderless fullscreen, the
 * host back buffer's in a window. Called with g_lock held, before the frame
 * is copied in (no buffer of it is held). */
static void flip_fit(void)
{
    unsigned w = g_fs ? (unsigned)g_client_w : g_w, h = g_fs ? (unsigned)g_client_h : g_h;
    HRESULT hr;

    if (!w || !h || (w == g_buf_w && h == g_buf_h))
        return;
    hr = IDXGISwapChain1_ResizeBuffers(g_flip, 0, w, h, DXGI_FORMAT_UNKNOWN, g_flip_flags);
    if (FAILED(hr)) {
        static int told;
        if (!told++)
            fprintf(stderr, "[D3DB] host swap chain not resized to %ux%u: 0x%08lX\n", w, h, (unsigned long)hr);
        return;
    }
    g_buf_w = w;
    g_buf_h = h;
    fprintf(stderr, "[D3DB] host swap chain %ux%u (%s)\n", w, h,
            g_buf_w == g_w && g_buf_h == g_h ? "the frame copied" : "the frame drawn letterboxed");
}

/* ── Live settings and the overlay (step 54, notes 18.57) ───── */

/* The nominal refresh rate of the window's monitor (its display mode, as a
 * rational: 239.76 here), 0 if unknown. DWM's rate (d3dh_display_hz) follows
 * the presents on a variable-refresh display (277.97 Hz once, mid-run), so a
 * pacing chosen during the run uses this one. */
double d3dh_monitor_hz(void)
{
    MONITORINFOEXA mi;
    UINT32 np = 0, nm = 0, i;
    DISPLAYCONFIG_PATH_INFO *paths;
    DISPLAYCONFIG_MODE_INFO *modes;
    double hz = 0;

    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!g_hwnd || !GetMonitorInfoA(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), (MONITORINFO *)&mi) ||
        GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS)
        return 0;
    paths = (DISPLAYCONFIG_PATH_INFO *)calloc(np, sizeof(*paths));
    modes = (DISPLAYCONFIG_MODE_INFO *)calloc(nm, sizeof(*modes));
    if (paths && modes && QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths, &nm, modes, NULL) == ERROR_SUCCESS)
        for (i = 0; i < np && hz == 0; i++) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME sn;
            memset(&sn, 0, sizeof(sn));
            sn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            sn.header.size = sizeof(sn);
            sn.header.adapterId = paths[i].sourceInfo.adapterId;
            sn.header.id = paths[i].sourceInfo.id;
            if (DisplayConfigGetDeviceInfo(&sn.header) == ERROR_SUCCESS) {
                char name[64];
                WideCharToMultiByte(CP_ACP, 0, sn.viewGdiDeviceName, -1, name, sizeof(name), NULL, NULL);
                if (_stricmp(name, mi.szDevice) == 0 && paths[i].targetInfo.refreshRate.Denominator)
                    hz = (double)paths[i].targetInfo.refreshRate.Numerator /
                         paths[i].targetInfo.refreshRate.Denominator;
            }
        }
    free(paths);
    free(modes);
    return hz;
}

void d3dh_set_apply(d3dh_apply_fn fn)
{
    g_apply = fn;
}

static volatile LONG g_ovl_closes;      /* times the overlay has closed (the controller gate) */
static volatile LONG g_ovl_session;     /* from the overlay's first frame until it has closed */

/* Step 57: idle from Esc until the overlay's session has ended, held or
 * running; the close count is read after, so a close is never missed. */
int d3dh_overlay_input(unsigned *closes)
{
    int open = g_ovl_open || g_ovl_session;

    MemoryBarrier();
    *closes = (unsigned)g_ovl_closes;
    return open;
}

/* The visible swap chain again, with `flags` (the frame-latency object or
 * not), at its current size. A flag can't change in ResizeBuffers. Called
 * with g_lock held, from the only presenting thread (the overlay's). */
static int recreate_flip(UINT flags)
{
    IDXGIFactory2 *f = NULL;
    DXGI_SWAP_CHAIN_DESC1 sd;
    HRESULT hr = E_FAIL;
    int attempt;

    if (FAILED(IDXGISwapChain_GetParent(d3d8_GetSwapChain(), &IID_IDXGIFactory2, (void **)&f)))
        return -1;
    IDXGISwapChain1_Release(g_flip);
    g_flip = NULL;
    if (g_latency) {
        CloseHandle(g_latency);
        g_latency = NULL;
    }
    ID3D11DeviceContext_Flush(d3d8_GetD3D11Context());
    memset(&sd, 0, sizeof(sd));
    sd.Width = g_buf_w;
    sd.Height = g_buf_h;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    for (attempt = 0; attempt < 20 && !g_flip; attempt++) {    /* the old one lets go of the window shortly */
        sd.Flags = attempt < 10 ? flags : g_flip_flags;
        hr = IDXGIFactory2_CreateSwapChainForHwnd(f, (IUnknown *)d3d8_GetD3D11Device(), g_hwnd, &sd, NULL, NULL,
                                                  &g_flip);
        if (FAILED(hr))
            Sleep(10);
    }
    if (g_flip) {
        g_flip_flags = sd.Flags;
        if (sd.Flags) {
            IDXGISwapChain2 *sc2 = NULL;
            if (SUCCEEDED(IDXGISwapChain1_QueryInterface(g_flip, &IID_IDXGISwapChain2, (void **)&sc2))) {
                IDXGISwapChain2_SetMaximumFrameLatency(sc2, 2);
                g_latency = IDXGISwapChain2_GetFrameLatencyWaitableObject(sc2);
                IDXGISwapChain2_Release(sc2);
            }
        }
        IDXGIFactory2_MakeWindowAssociation(f, g_hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    } else {
        fprintf(stderr, "[D3DB] host swap chain not made again (0x%08lX): the host stops presenting\n",
                (unsigned long)hr);
        InterlockedExchange(&g_ready, 0);
    }
    IDXGIFactory2_Release(f);
    return g_flip && sd.Flags == flags ? 0 : -1;
}

void d3dh_set_pacing(unsigned interval, double frame_hz)
{
    UINT want = interval && frame_hz <= 0 ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT : 0;

    if (!g_ready)
        return;
    EnterCriticalSection(&g_lock);
    if (want != g_flip_flags && recreate_flip(want) != 0) {
        LeaveCriticalSection(&g_lock);
        fprintf(stderr, "[D3DB] host pacing unchanged: the swap chain couldn't be made for it\n");
        return;
    }
    g_interval = interval;
    g_frame_hz = frame_hz > 0 ? frame_hz : 0;
    LeaveCriticalSection(&g_lock);
    fprintf(stderr, "[D3DB] host pacing now: present interval %u%s, frame limiter %s%.3f Hz\n", g_interval,
            g_latency ? " (vsync paced)" : "", g_frame_hz > 0 ? "" : "off, ", g_frame_hz);
}

/* The back buffer for a w x h host picture (step 54) with g_msaa's samples
 * (step 58): a render target of its own, or none (g_bb.tex NULL) where
 * xbox_d3d8's own back buffer serves, at its size without MSAA. Bound and
 * cleared, and at the picture's current size it keeps the frame it holds
 * (an MSAA change while the overlay holds the game shows that frame next).
 * Called with g_lock held. On failure nothing changes. */
static HRESULT bb_rebuild(unsigned w, unsigned h)
{
    IDirect3DTexture8 *tex = NULL;
    IDirect3DSurface8 *surf = NULL, *depth = NULL;
    unsigned n = g_msaa > 1 ? ms_samples((D3DFORMAT)XDK_FMT_LIN_A8B8G8R8) : 1;
    ID3D11Texture2D *old = NULL, *dst = NULL;
    HRESULT hr = S_OK;

    if (w != g_dev_w || h != g_dev_h || n > 1) {
        hr = g_dev->lpVtbl->CreateTexture(g_dev, w, h, 1, D3DUSAGE_RENDERTARGET, (D3DFORMAT)XDK_FMT_LIN_A8B8G8R8,
                                          D3DPOOL_DEFAULT, &tex);
        if (SUCCEEDED(hr) && n > 1) {
            surf = ms_surface(w, h, (D3DFORMAT)XDK_FMT_LIN_A8B8G8R8, n);
            depth = surf ? ms_surface(w, h, D3DFMT_D24S8, n) : NULL;
            if (!depth)
                hr = E_FAIL;
        } else if (SUCCEEDED(hr)) {
            hr = tex->lpVtbl->GetSurfaceLevel(tex, 0, &surf);
            if (SUCCEEDED(hr))
                hr = g_dev->lpVtbl->CreateDepthStencilSurface(g_dev, w, h, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &depth);
        }
        if (FAILED(hr) || !surf || !depth) {
            if (depth)
                depth->lpVtbl->Release(depth);
            if (surf)
                surf->lpVtbl->Release(surf);
            if (tex)
                tex->lpVtbl->Release(tex);
            return FAILED(hr) ? hr : E_FAIL;
        }
    }
    if (w == g_w && h == g_h)           /* step 58: the frame carries over */
        old = back_tex();
    g_dev->lpVtbl->SetRenderTarget(g_dev, NULL, NULL);
    if (old) {
        if (tex)
            dst = ((D3D8Texture *)tex)->d3d11_texture;
        else if (SUCCEEDED(IDXGISwapChain_GetBuffer(d3d8_GetSwapChain(), 0, &IID_ID3D11Texture2D, (void **)&dst)))
            ID3D11Texture2D_Release(dst);       /* the swap chain keeps it */
        if (dst && dst != old)
            ID3D11DeviceContext_CopyResource(d3d8_GetD3D11Context(), (ID3D11Resource *)dst, (ID3D11Resource *)old);
        ID3D11Texture2D_Release(old);
    }
    if (g_bb.tex) {
        g_bb.depth->lpVtbl->Release(g_bb.depth);
        g_bb.surf->lpVtbl->Release(g_bb.surf);
        g_bb.tex->lpVtbl->Release(g_bb.tex);
    }
    g_bb.tex = tex;                     /* NULL at xbox_d3d8's own size: its back buffer again */
    g_bb.surf = surf;
    g_bb.depth = depth;
    g_bb.ms = n > 1 && tex ? n : 0;
    g_bb.dirty = 0;
    if (g_bb.ms && old)
        ms_fill(g_bb.surf, g_bb.depth, g_bb.tex, w, h);
    else if (g_bb.tex) {
        g_dev->lpVtbl->SetRenderTarget(g_dev, g_bb.surf, g_bb.depth);
        g_dev->lpVtbl->Clear(g_dev, 0, NULL, old ? D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL
                                                 : D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
    }
    g_cur_target = -1;
    return S_OK;
}

/* Step 58: the anti-aliasing choice, at once. FXAA and SMAA need nothing
 * here (the next present filters). A change of MSAA's samples makes the
 * back buffer again and drops the render targets' multisampled surfaces,
 * resolved first; a target gets new ones at its next draw. */
static void set_antialiasing(int aa)
{
    unsigned was = g_msaa;
    HRESULT hr = S_OK;
    int i;

    EnterCriticalSection(&g_lock);
    g_aa = aa;
    g_msaa = twoc_aa_samples(aa);
    if (g_msaa != was) {
        for (i = 0; i < RT_SLOTS; i++)
            if (g_rt[i].tex)
                rt_ms_release(i);
        hr = bb_rebuild(g_w, g_h);
        if (FAILED(hr))
            g_msaa = was;
    }
    g_st.aa_mode = (unsigned)g_aa;
    g_st.ms_samples = g_bb.ms;
    LeaveCriticalSection(&g_lock);
    if (FAILED(hr))
        fprintf(stderr, "[D3DB] host anti-aliasing: %ux MSAA not set up (0x%08lX); as before\n",
                twoc_aa_samples(aa), (unsigned long)hr);
    fprintf(stderr, "[D3DB] host anti-aliasing: %s%s\n", twoc_aa_name(g_aa),
            g_msaa > 1 && g_bb.ms != g_msaa ? (g_bb.ms ? " (fewer samples: the most the device takes)"
                                                       : " (the device takes no MSAA: off)") : "");
}

void d3dh_resize(unsigned host_w, unsigned host_h, double wide)
{
    HRESULT hr;
    int i;

    if (!g_ready || !host_w || !host_h)
        return;
    EnterCriticalSection(&g_lock);
    g_wide = wide;
    if (host_w == g_w && host_h == g_h) {
        LeaveCriticalSection(&g_lock);
        return;
    }
    hr = bb_rebuild(host_w, host_h);
    if (FAILED(hr)) {
        LeaveCriticalSection(&g_lock);
        fprintf(stderr, "[D3DB] host picture not resized to %ux%u (0x%08lX): stays %ux%u\n", host_w, host_h,
                (unsigned long)hr, g_w, g_h);
        return;
    }
    for (i = 0; i < RT_SLOTS; i++)      /* screen-sized targets: made again at the new size at their next use */
        if (g_rt[i].tex && g_rt[i].width == g_gw && g_rt[i].height == g_gh)
            rt_release(i);
    if (g_staging) {                    /* the dumps' copy has the old size */
        ID3D11Texture2D_Release(g_staging);
        g_staging = NULL;
    }
    fprintf(stderr, "[D3DB] host picture %ux%u -> %ux%u (%s)%s\n", g_w, g_h, host_w, host_h,
            wide > 0 ? "widescreen" : "4:3", g_bb.tex ? ", in a render target of its own" : "");
    g_w = host_w;
    g_h = host_h;
    LeaveCriticalSection(&g_lock);
    PostMessageA(g_hwnd, WM_TWOC_FIT, 0, 0);
}

/* The overlay's UI scale: the swap chain's pixels per client pixel, times
 * the monitor's scale when the window is in real pixels. */
static float overlay_scale(void)
{
    typedef UINT(WINAPI * dpi_t)(HWND);
    static dpi_t dpi_for;
    static int tried;
    RECT c;
    float k = 1.0f;

    if (!tried) {
        tried = 1;
        dpi_for = (dpi_t)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow");
    }
    if (GetClientRect(g_hwnd, &c) && c.right > 0)
        k = (float)g_buf_w / (float)c.right;
    if (g_dpi_aware && dpi_for)
        k *= (float)dpi_for(g_hwnd) / 96.0f;
    return k < 0.75f ? 0.75f : k;
}

/* What changed between `was` and `s`, applied now; then `was` = `s`. */
static void overlay_apply(twoc_settings_t *was, const twoc_settings_t *s)
{
    if (!s->fullscreen != !was->fullscreen)
        PostMessageA(g_hwnd, WM_TWOC_FULLSCREEN, s->fullscreen ? 1 : 0, 0);
    if (s->anisotropy != was->anisotropy) {
        InterlockedExchange(&g_aniso, s->anisotropy);
        fprintf(stderr, "[D3DB] host: anisotropic filtering %s%dx\n", s->anisotropy > 1 ? "" : "off, ",
                s->anisotropy);
    }
    if (s->antialiasing != was->antialiasing)
        set_antialiasing(s->antialiasing);      /* step 58 */
    if (s->volume != was->volume)
        twoc_audio_set_volume(s->volume);
    if (s->music != was->music)
        twoc_audio_set_kind(1, s->music);
    if (s->effects != was->effects)
        twoc_audio_set_kind(0, s->effects);
    if (s->hrtf != was->hrtf)
        twoc_audio_set_hrtf(s->hrtf);
    if (s->pause != was->pause)
        fprintf(stderr, "[OVL] pause the game while open: %s\n", s->pause ? "on" : "off");
    if (s->keyboard != was->keyboard || memcmp(&s->controls, &was->controls, sizeof(s->controls)) != 0) {
        controls_set(&s->controls, s->keyboard);    /* step 57: at the game's next poll */
        fprintf(stderr, "[OVL] controls: keyboard %s, controller map %s\n", s->keyboard ? "on" : "off",
                controls_pad_is_default(&s->controls) ? "the default" : "changed");
    }
    if (g_apply && (s->widescreen != was->widescreen || s->wide_w != was->wide_w || s->wide_h != was->wide_h ||
                    strcmp(s->render_size, was->render_size) != 0 || s->pacing != was->pacing))
        g_apply(s);                     /* d3d_bridge.c: the aspect, the render size, the pacing */
    *was = *s;
}

/* One opening of the overlay, from the present after Esc to the one after
 * it closed (step 57: either held, overlay_pause, or with the game running
 * behind it, d3dh_present). g_ovl_session is the controller gate's: the
 * game gets an idle pad from Esc until the session has ended and counted
 * itself in g_ovl_closes. */
static struct {
    int ready;                          /* 0 not tried, 1 the overlay works, -1 it doesn't */
    int have;                           /* s has been read from the switches */
    twoc_settings_t s, was;             /* what the player edits; what is applied */
    const char *note;                   /* the last save's outcome */
    int active, dirty, muted;
    LONG fs_seen;
    LARGE_INTEGER freq, t0, last;
    unsigned frames;
} g_ovs;

/* The present after Esc: the overlay's first frame. No lock held. */
static int ovl_begin(void)
{
    EnterCriticalSection(&g_lock);
    if (!g_ovs.ready)
        g_ovs.ready = ovl_init(d3d8_GetD3D11Device(), d3d8_GetD3D11Context()) == 0 && blit_init() == 0 ? 1 : -1;
    LeaveCriticalSection(&g_lock);
    if (g_ovs.ready < 0) {
        InterlockedExchange(&g_ovl_open, 0);
        fprintf(stderr, "[OVL] the settings overlay can't open here\n");
        return -1;
    }
    if (!g_ovs.have) {
        twoc_settings_current(&g_ovs.s);
        g_ovs.have = 1;
    }
    g_ovs.fs_seen = g_fs;
    g_ovs.s.fullscreen = g_ovs.fs_seen;
    g_ovs.was = g_ovs.s;
    g_ovs.dirty = 0;
    g_ovs.frames = 0;
    QueryPerformanceFrequency(&g_ovs.freq);
    QueryPerformanceCounter(&g_ovs.t0);
    g_ovs.last = g_ovs.t0;
    g_ovs.active = 1;
    InterlockedExchange(&g_ovl_session, 1);
    fprintf(stderr, "[OVL] settings open: %s %u\n",
            g_ovs.s.pause ? "the game waits in its present" : "the game runs on behind it, idle pad, from present",
            g_st.presents);
    return 0;
}

/* The overlay closed (Esc, Resume) and its last frame is shown. */
static void ovl_end(void)
{
    LARGE_INTEGER now;

    if (g_ovs.muted) {
        twoc_audio_mute(0);
        g_ovs.muted = 0;
    }
    g_ovs.active = 0;
    InterlockedIncrement(&g_ovl_closes);        /* before the gate opens: it masks what is held */
    InterlockedExchange(&g_ovl_session, 0);
    if (g_ovs.dirty)
        g_ovs.note = twoc_settings_save(&g_ovs.s) == 0 ? "Saved to crash_twoc.ini." : "crash_twoc.ini couldn't be written.";
    QueryPerformanceCounter(&now);
    fprintf(stderr, "[OVL] settings closed after %.1f s (%u overlay frames)%s\n",
            (double)(now.QuadPart - g_ovs.t0.QuadPart) / (double)g_ovs.freq.QuadPart, g_ovs.frames,
            g_ovs.dirty ? "; settings saved" : "");
}

/* One overlay frame over what `dst` (the visible swap chain's buffer) holds.
 * With g_lock held. Returns ovl_frame's OVL_* bits. */
static int ovl_draw(ID3D11Texture2D *dst, int running)
{
    ID3D11RenderTargetView *rtv = NULL;
    LARGE_INTEGER now;
    ovl_info_t in;
    int r = 0;

    QueryPerformanceCounter(&now);
    if (g_fs != g_ovs.fs_seen) {        /* Alt+Enter while open */
        g_ovs.fs_seen = g_fs;
        g_ovs.s.fullscreen = g_ovs.was.fullscreen = g_ovs.fs_seen;
    }
    memset(&in, 0, sizeof(in));
    in.host_w = g_w;
    in.host_h = g_h;
    in.guest_w = g_gw;
    in.guest_h = g_gh;
    in.display_hz = d3dh_display_hz();
    in.interval = g_interval;
    in.frame_hz = g_frame_hz;
    in.executor_paces = getenv("RECOMP_PB_EXEC") && strcmp(getenv("RECOMP_PB_EXEC"), "0") != 0;
    in.dpi_aware = g_dpi_aware;
    in.running = running;
    in.aa = g_aa;                       /* step 58 */
    in.ms_samples = g_bb.ms;
    in.save_note = g_ovs.dirty ? "Saved to crash_twoc.ini when you resume." : g_ovs.note;
    if (SUCCEEDED(ID3D11Device_CreateRenderTargetView(d3d8_GetD3D11Device(), (ID3D11Resource *)dst, NULL, &rtv)))
        r = ovl_frame(rtv, g_buf_w, g_buf_h, overlay_scale(),
                      (float)((double)(now.QuadPart - g_ovs.last.QuadPart) / (double)g_ovs.freq.QuadPart),
                      &g_ovs.s, &in);
    if (rtv)
        ID3D11RenderTargetView_Release(rtv);
    g_ovs.last = now;
    g_ovs.frames++;
    return r;
}

/* What the frame asked for, with no lock held. */
static void ovl_after(int r)
{
    if (r & OVL_CHANGED) {
        overlay_apply(&g_ovs.was, &g_ovs.s);
        g_ovs.dirty = 1;
    }
    if (r & OVL_CLOSE)
        InterlockedExchange(&g_ovl_open, 0);
}

/* The overlay with "Pause the game" on: hold this thread (the game's, in
 * its Swap) and draw the overlay over the frame it was about to show, until
 * Esc again, Resume, or the pause is turned off (then d3dh_present carries
 * on with the overlay over the running game). Every other thread that
 * presents meanwhile waits on g_pause_lock, so the game stands still: no
 * update, no draw, no controller read. The emulated APU runs on its own
 * threads and keeps playing looped voices, so the game's audio session is
 * muted meanwhile (host_audio.cpp). */
static void overlay_pause(void)
{
    int ok, aa_seen;

    AcquireSRWLockExclusive(&g_pause_lock);
    if (!g_ovl_open || !g_ready || (!g_ovs.active && ovl_begin() != 0) || !g_ovs.s.pause) {
        ReleaseSRWLockExclusive(&g_pause_lock);
        return;
    }
    EnterCriticalSection(&g_lock);
    {   /* the frame the game just finished, held while paused */
        ID3D11Texture2D *src = frame_tex();
        ok = src && shot_take(src) == 0;
        if (src)
            ID3D11Texture2D_Release(src);
    }
    LeaveCriticalSection(&g_lock);
    if (!ok) {
        InterlockedExchange(&g_ovl_open, 0);
        fprintf(stderr, "[OVL] the settings overlay can't hold the game here\n");
        ovl_end();
        ReleaseSRWLockExclusive(&g_pause_lock);
        return;
    }
    if (g_ovs.frames)
        fprintf(stderr, "[OVL] the game waits in its present %u\n", g_st.presents);
    twoc_audio_mute(1);                 /* the APU plays looped voices on: silence while held */
    g_ovs.muted = 1;
    aa_seen = g_aa;
    while (g_ovl_open && g_ready && g_ovs.s.pause) {
        ID3D11Texture2D *dst = NULL;
        int r = 0;

        if (g_latency || g_frame_hz > 0)
            pace();
        else
            Sleep(15);                  /* the executor paces this run, not the host */
        EnterCriticalSection(&g_lock);
        if (g_aa != aa_seen) {          /* step 58: the held frame through the new choice (MSAA shows on resume) */
            ID3D11Texture2D *src = frame_tex();
            if (src) {
                shot_take(src);
                ID3D11Texture2D_Release(src);
            }
            aa_seen = g_aa;
        }
        flip_fit();
        if (SUCCEEDED(IDXGISwapChain1_GetBuffer(g_flip, 0, &IID_ID3D11Texture2D, (void **)&dst)) &&
            blit_draw(dst, g_buf_w, g_buf_h) == 0)
            r = ovl_draw(dst, 0);
        if (dst)
            ID3D11Texture2D_Release(dst);
        IDXGISwapChain1_Present(g_flip, g_interval, 0);
        LeaveCriticalSection(&g_lock);
        ovl_after(r);
    }
    twoc_audio_mute(0);
    g_ovs.muted = 0;
    if (!g_ovl_open || !g_ready)
        ovl_end();
    else
        fprintf(stderr, "[OVL] pause off: the game runs on behind the overlay from present %u\n", g_st.presents);
    ReleaseSRWLockExclusive(&g_pause_lock);
}

/* The replay check's snapshot of where a frame starts: the back buffer's and
 * each render target's colour, taken after a frame (`restore` 0) and put
 * back before that frame's replay (1), so the replay starts from what the
 * frame drawn at once started from. A target made during the frame starts
 * black, as its creation leaves it. Depth and stencil aren't kept (every
 * frame seen clears them before use), nor multisampled surfaces (check
 * without MSAA). Called with g_lock held. */
static void replay_check_snap(int restore, ID3D11Texture2D *bb)
{
    static ID3D11Texture2D *snap[RT_SLOTS + 1];
    static uint32_t snap_data[RT_SLOTS + 1];
    ID3D11DeviceContext *ctx = d3d8_GetD3D11Context();
    ID3D11Device *dev = d3d8_GetD3D11Device();
    D3D11_TEXTURE2D_DESC a, b;
    int i;

    for (i = 0; i <= RT_SLOTS; i++) {
        ID3D11Texture2D *t = i == RT_SLOTS ? bb : g_rt[i].tex ? ((D3D8Texture *)g_rt[i].tex)->d3d11_texture : NULL;
        uint32_t data = i == RT_SLOTS ? 0xFFFFFFFFu : g_rt[i].data;
        if (!restore) {
            if (!t) {
                snap_data[i] = 0;
                continue;
            }
            ID3D11Texture2D_GetDesc(t, &a);
            if (snap[i]) {
                ID3D11Texture2D_GetDesc(snap[i], &b);
                if (b.Width != a.Width || b.Height != a.Height || b.Format != a.Format) {
                    ID3D11Texture2D_Release(snap[i]);
                    snap[i] = NULL;
                }
            }
            if (!snap[i]) {
                b = a;
                b.MipLevels = 1;
                b.ArraySize = 1;
                b.SampleDesc.Count = 1;
                b.SampleDesc.Quality = 0;
                b.Usage = D3D11_USAGE_DEFAULT;
                b.BindFlags = 0;
                b.CPUAccessFlags = 0;
                b.MiscFlags = 0;
                if (FAILED(ID3D11Device_CreateTexture2D(dev, &b, NULL, &snap[i])))
                    snap[i] = NULL;
            }
            snap_data[i] = snap[i] ? data : 0;
            if (snap[i])
                ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)snap[i], 0, 0, 0, 0,
                                                          (ID3D11Resource *)t, 0, NULL);
            continue;
        }
        if (!t)
            continue;
        if (snap[i] && snap_data[i] == data) {
            ID3D11Texture2D_GetDesc(t, &a);
            ID3D11Texture2D_GetDesc(snap[i], &b);
            if (a.Width == b.Width && a.Height == b.Height && a.Format == b.Format) {
                ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)t, 0, 0, 0, 0,
                                                          (ID3D11Resource *)snap[i], 0, NULL);
                continue;
            }
        }
        if (i < RT_SLOTS) {             /* made during the frame: black, as rt_find left it */
            g_dev->lpVtbl->SetRenderTarget(g_dev, g_rt[i].surf, g_rt[i].depth);
            g_dev->lpVtbl->Clear(g_dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
            g_cur_target = -2;
        }
    }
}

/* RECOMP_D3D_BRIDGE_REPLAY_CHECK (step 60): `after` 0 keeps the back buffer
 * as drawn at once, 1 compares the replayed one with it. Called with g_lock
 * held. */
static void replay_check(int after, unsigned swap_no)
{
    static ID3D11Texture2D *kept, *stage[2];
    static D3D11_TEXTURE2D_DESC kd;
    ID3D11DeviceContext *ctx = d3d8_GetD3D11Context();
    ID3D11Device *dev = d3d8_GetD3D11Device();
    ID3D11Texture2D *bb = back_tex();
    D3D11_TEXTURE2D_DESC d;
    D3D11_MAPPED_SUBRESOURCE ma, mb;
    unsigned x, y, k, diff = 0, maxd = 0, x0 = ~0u, y0 = ~0u, x1 = 0, y1 = 0;

    if (!bb)
        return;
    ID3D11Texture2D_GetDesc(bb, &d);
    if (!after) {
        if (!kept || kd.Width != d.Width || kd.Height != d.Height || kd.Format != d.Format) {
            for (k = 0; k < 2; k++)
                if (stage[k]) {
                    ID3D11Texture2D_Release(stage[k]);
                    stage[k] = NULL;
                }
            if (kept)
                ID3D11Texture2D_Release(kept);
            kept = NULL;
            kd = d;
            kd.MipLevels = 1;
            kd.ArraySize = 1;
            kd.SampleDesc.Count = 1;
            kd.SampleDesc.Quality = 0;
            kd.Usage = D3D11_USAGE_DEFAULT;
            kd.BindFlags = 0;
            kd.CPUAccessFlags = 0;
            kd.MiscFlags = 0;
            if (FAILED(ID3D11Device_CreateTexture2D(dev, &kd, NULL, &kept)))
                kept = NULL;
            kd.Usage = D3D11_USAGE_STAGING;
            kd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            for (k = 0; k < 2; k++)
                if (FAILED(ID3D11Device_CreateTexture2D(dev, &kd, NULL, &stage[k])))
                    stage[k] = NULL;
            kd.Usage = D3D11_USAGE_DEFAULT;
            kd.CPUAccessFlags = 0;
        }
        if (kept)
            ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)kept, (ID3D11Resource *)bb);
        replay_check_snap(1, bb);       /* the replay starts where the frame did */
        ID3D11Texture2D_Release(bb);
        return;
    }
    if (!kept || !stage[0] || !stage[1] || kd.Width != d.Width || kd.Height != d.Height) {
        ID3D11Texture2D_Release(bb);
        return;
    }
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)stage[0], (ID3D11Resource *)kept);
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)stage[1], (ID3D11Resource *)bb);
    replay_check_snap(0, bb);           /* where the next frame starts */
    ID3D11Texture2D_Release(bb);
    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)stage[0], 0, D3D11_MAP_READ, 0, &ma)))
        return;
    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)stage[1], 0, D3D11_MAP_READ, 0, &mb))) {
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)stage[0], 0);
        return;
    }
    for (y = 0; y < kd.Height; y++) {
        const uint8_t *pa = (const uint8_t *)ma.pData + (size_t)y * ma.RowPitch;
        const uint8_t *pb = (const uint8_t *)mb.pData + (size_t)y * mb.RowPitch;
        if (memcmp(pa, pb, 4 * (size_t)kd.Width) == 0)
            continue;
        for (x = 0; x < kd.Width; x++) {
            unsigned c, m = 0;
            for (c = 0; c < 3; c++) {   /* colour; alpha isn't shown */
                unsigned dd = pa[4 * x + c] > pb[4 * x + c] ? pa[4 * x + c] - pb[4 * x + c] : pb[4 * x + c] - pa[4 * x + c];
                m = dd > m ? dd : m;
            }
            if (m) {
                diff++;
                maxd = m > maxd ? m : maxd;
                x0 = x < x0 ? x : x0;
                x1 = x > x1 ? x : x1;
                y0 = y < y0 ? y : y0;
                y1 = y > y1 ? y : y1;
            }
        }
    }
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)stage[1], 0);
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)stage[0], 0);
    g_st.replay_checked++;
    if (diff) {
        g_st.replay_check_diff++;
        if (g_st.replay_check_diff <= 20)
            fprintf(stderr, "[D3DB] host replay check: guest swap %u differs in %u pixels (largest %u levels),"
                    " within (%u, %u)-(%u, %u)\n", swap_no, diff, maxd, x0, y0, x1, y1);
        if (g_dump_prefix[0] && g_st.replay_check_diff <= 4) {     /* both pictures, with -DumpDir */
            char path[600];
            snprintf(path, sizeof(path), "%schk%05u_once.bmp", g_dump_prefix, swap_no);
            dump_texture(kept, NULL, path, 0);
            snprintf(path, sizeof(path), "%schk%05u_replayed.bmp", g_dump_prefix, swap_no);
            dump_texture(stage[1], NULL, path, 0);
        }
    }
}

void d3dh_present(double t_s, unsigned swap_no)
{
    ULONGLONG now;
    HRESULT hr;
    int ovl_r = 0;
    ID3D11Texture2D *src;

    if (!g_ready)
        return;
    if (g_replay) {                     /* step 60: the frame's recorded commands, drawn now */
        EnterCriticalSection(&g_lock);
        if (g_replay_check)
            replay_check(0, swap_no);
        replay_run();
        if (g_replay_check)
            replay_check(1, swap_no);
        LeaveCriticalSection(&g_lock);
    }
    if (g_ovl_open && !g_ovs.active) {  /* step 54: Esc */
        AcquireSRWLockExclusive(&g_pause_lock);
        if (g_ovl_open && !g_ovs.active)
            ovl_begin();
        ReleaseSRWLockExclusive(&g_pause_lock);
    }
    if (g_ovl_open && g_ovs.active && g_ovs.s.pause)
        overlay_pause();                /* this frame waits under the overlay */
    pace();
    EnterCriticalSection(&g_lock);
    src = frame_tex();                  /* step 58: resolved, and through FXAA or SMAA if one is on */
    now = GetTickCount64();
    if (g_dump_prefix[0] &&
        ((g_frame_work && g_dump_first < DUMP_FIRST_FRAMES) || now - g_last_dump_ms >= DUMP_PERIOD_MS ||
         (g_dump_every && swap_no % g_dump_every == 0) || (swap_no >= g_dump_from && swap_no <= g_dump_to))) {
        char path[600];
        snprintf(path, sizeof(path), "%s%03u.bmp", g_dump_prefix, g_dump_seq++);
        dump_back_buffer(path, src);
        if (g_dump_rt)
            dump_targets(path);
        if (g_frame_work && g_dump_first < DUMP_FIRST_FRAMES)
            g_dump_first++;
        g_last_dump_ms = now;
        g_st.dumps++;
        fprintf(stderr, "[D3DB] host dump %s at +%.1f s, guest swap %u\n", path, t_s, swap_no);
    }
    flip_fit();                         /* step 54 */
    {   /* xbox_d3d8's back buffer -> the visible flip-model swap chain */
        ID3D11Texture2D *dst = NULL;
        if (src && SUCCEEDED(IDXGISwapChain1_GetBuffer(g_flip, 0, &IID_ID3D11Texture2D, (void **)&dst))) {
            if (g_buf_w == g_w && g_buf_h == g_h)
                ID3D11DeviceContext_CopyResource(d3d8_GetD3D11Context(), (ID3D11Resource *)dst,
                                                 (ID3D11Resource *)src);
            else if (shot_take(src) != 0 || blit_draw(dst, g_buf_w, g_buf_h) != 0)
                copy_unscaled(dst, src);
            if (g_ovl_open && g_ovs.active && !g_ovs.s.pause)
                ovl_r = ovl_draw(dst, 1);   /* step 57: the overlay over the running game */
        }
        if (src)
            ID3D11Texture2D_Release(src);
        if (dst)
            ID3D11Texture2D_Release(dst);
    }
    hr = IDXGISwapChain1_Present(g_flip, g_interval, 0);
    g_st.presents++;
    {   /* How many DWM compositions apart consecutive presents land: the
         * index of the first one after each present, from DWM's timing (its
         * last vblank, its count, the period). DWM composes on demand, so
         * with nothing else changing the count follows our presents: 1 is a
         * frame composed on its own, 0 two presents in one composition (one
         * never shown), more an uneven gap (18.45). */
        static ULONGLONG last_idx;
        DWM_TIMING_INFO ti;
        LARGE_INTEGER q;
        memset(&ti, 0, sizeof(ti));
        ti.cbSize = sizeof(ti);
        QueryPerformanceCounter(&q);
        if (SUCCEEDED(DwmGetCompositionTimingInfo(NULL, &ti)) && ti.qpcRefreshPeriod) {
            LONGLONG since = (LONGLONG)q.QuadPart - (LONGLONG)ti.qpcVBlank;
            static ULONGLONG told_period;
            if (!told_period) {
                LARGE_INTEGER f;
                QueryPerformanceFrequency(&f);
                told_period = ti.qpcRefreshPeriod;
                fprintf(stderr, "[D3DB] host: DWM refresh period %.3f ms (rate %u/%u), at present %u\n",
                        ti.qpcRefreshPeriod * 1000.0 / f.QuadPart, ti.rateRefresh.uiNumerator,
                        ti.rateRefresh.uiDenominator, g_st.presents);
            }
            ULONGLONG idx = ti.cRefresh + (ULONGLONG)((since + (LONGLONG)ti.qpcRefreshPeriod - 1) / (LONGLONG)ti.qpcRefreshPeriod);
            if (last_idx) {
                ULONGLONG d = idx - last_idx;
                g_st.shown[d > 8 ? 8 : d]++;
            }
            last_idx = idx;
        }
    }
    {   /* When frames actually reach the screen (18.48): DXGI's frame
         * statistics name the last present shown and the refresh it was
         * shown at. Refreshes between consecutive shown frames should all be
         * g_interval; PresentMon calls this MsBetweenDisplayChange. With
         * RECOMP_D3D_BRIDGE_DISPLAYLOG=1 each other interval is logged with
         * where the presents fell against DWM's vblank. */
        static UINT last_pc, last_prc;
        static int log_on = -1;
        static double phase[64];        /* ms after DWM's vblank, by present count mod 64 */
        static unsigned work[64][5];    /* draws, texture uploads, target draws, copies, us since the last */
        DXGI_FRAME_STATISTICS fs;
        DWM_TIMING_INFO ti;
        LARGE_INTEGER q, f;

        if (log_on < 0) {
            const char *e = getenv("RECOMP_D3D_BRIDGE_DISPLAYLOG");
            log_on = e && *e && *e != '0';
        }
        memset(&ti, 0, sizeof(ti));
        ti.cbSize = sizeof(ti);
        QueryPerformanceCounter(&q);
        QueryPerformanceFrequency(&f);
        if (log_on && SUCCEEDED(DwmGetCompositionTimingInfo(NULL, &ti)) && ti.qpcRefreshPeriod) {
            LONGLONG since = ((LONGLONG)q.QuadPart - (LONGLONG)ti.qpcVBlank) % (LONGLONG)ti.qpcRefreshPeriod;
            if (since < 0)
                since += ti.qpcRefreshPeriod;
            phase[g_st.presents & 63] = since * 1000.0 / f.QuadPart;
        }
        if (log_on) {                   /* this frame's work, to set beside a late one */
            static unsigned last_draws, last_tex, last_rt, last_copies;
            static LONGLONG last_q;
            unsigned *w = work[g_st.presents & 63];
            w[0] = g_st.draws - last_draws;
            w[1] = g_st.tex_uploads - last_tex;
            w[2] = g_st.rt_draws - last_rt;
            w[3] = g_st.copies - last_copies;
            w[4] = last_q ? (unsigned)((q.QuadPart - last_q) * 1000000 / f.QuadPart) : 0;  /* us since the last present */
            last_draws = g_st.draws;
            last_tex = g_st.tex_uploads;
            last_rt = g_st.rt_draws;
            last_copies = g_st.copies;
            last_q = q.QuadPart;
        }
        if (SUCCEEDED(IDXGISwapChain1_GetFrameStatistics(g_flip, &fs)) && fs.PresentCount) {
            if (fs.PresentCount != last_pc) {
                if (last_pc && fs.PresentCount == last_pc + 1) {
                    UINT d = fs.PresentRefreshCount - last_prc;
                    g_st.displayed[d > 8 ? 8 : d]++;
                    if (log_on && d != g_interval) {
                        unsigned *a = work[(fs.PresentCount - 1) & 63], *b = work[fs.PresentCount & 63];
                        fprintf(stderr, "[D3DB] host display: present %u shown %u refreshes after the one before"
                                " (presents at +%.2f, +%.2f ms after a vblank; %u presents made; work before/this:"
                                " draws %u/%u, uploads %u/%u, target draws %u/%u, copies %u/%u, us since the last %u/%u)\n",
                                fs.PresentCount, d, phase[(fs.PresentCount - 1) & 63], phase[fs.PresentCount & 63],
                                g_st.presents, a[0], b[0], a[1], b[1], a[2], b[2], a[3], b[3], a[4], b[4]);
                    }
                } else if (last_pc) {
                    g_st.display_unmeasured++;
                }
                last_pc = fs.PresentCount;
                last_prc = fs.PresentRefreshCount;
            }
        } else {
            g_st.display_unmeasured++;
        }
    }
    if (FAILED(hr) && ++g_st.present_fails <= 5)
        fprintf(stderr, "[D3DB] host Present failed 0x%08lX\n", (unsigned long)hr);
    else if (hr != S_OK && ++g_st.present_status <= 5)   /* e.g. DXGI_STATUS_OCCLUDED: not shown */
        fprintf(stderr, "[D3DB] host Present returned status 0x%08lX\n", (unsigned long)hr);
    g_frame_work = 0;
    LeaveCriticalSection(&g_lock);
    if (g_ovs.active && (ovl_r || !g_ovl_open)) {
        AcquireSRWLockExclusive(&g_pause_lock);
        if (g_ovs.active) {
            if (ovl_r)                  /* step 57: this present's overlay frame (the pause may just have gone on) */
                ovl_after(ovl_r);
            if (!g_ovl_open || !g_ready)
                ovl_end();              /* closed, whichever way it was last held */
        }
        ReleaseSRWLockExclusive(&g_pause_lock);
    }
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
