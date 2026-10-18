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
 *   - Step 79 (notes 18.82): with Frame rate above 60 and the render thread
 *     on, the replay side (every D3D11, DXGI and xbox_d3d8 call, the plan and
 *     the presents, the overlay) runs on a render thread under g_lock, and
 *     the game threads record under g_rec beside it, handing a frame over at
 *     each Swap ("Step 79: the render thread's loop"). Otherwise one thread
 *     does both in turn, as before, under both locks.
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
 * Step 86 (notes 18.89): F1 opens and closes it instead of Esc, which only
 * closes it (or ends a binding's wait); on a controller Start held ~1 s
 * (recomp_manual.c) or View + Menu.
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
 *
 * Step 61 (notes 18.64): frame interpolation, camera only. With the frame
 * rate at "display" (RECOMP_FRAMERATE=display, the overlay) each frame is
 * recorded (step 60) and, at its present, drawn k times, k the DXGI interval
 * the display paces at (2 at 119.88 Hz, 4 at 239.76 Hz): at t = 1/k .. 1
 * between the frame before and this one, each presented at interval 1, so
 * the game still leaves its Swap at 59.94 Hz. Only the camera moves between
 * sub-frames; the last is the frame as recorded. See "Frame interpolation".
 *
 * Step 62 (notes 18.65): objects too. Draws are matched with the frame
 * before's by their kind and the guest mesh (d3d_bridge.c host_draw), the
 * nearest of a kind by position; a matched object's world matrix and its
 * bones (c124 on) are interpolated, one that stepped over 1.5 units snaps
 * (RECOMP_D3D_BRIDGE_INTERP_OBJECTS: the limit, 0 camera only). See
 * "Step 62: objects".
 *
 * Step 63 (notes 18.66): a mirror image (the hub floor's Crash) and a
 * flattened one (his stencil shadow) are other kinds than the model, and a
 * skinned mesh's partner is the nearest by all its bones; an object that
 * took another kind's place (a menu line changing colour) is drawn as it
 * is, and one whose nearest of its kind was taken snaps if its partner is
 * over half its size away (ic_replaced, ic_pair).
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "d3d8_internal.h"      /* xbox_d3d8, and its D3D11 accessors */
#include <dxgi1_3.h>
#include <dwmapi.h>
#include <d3dcompiler.h>
#pragma comment(lib, "dwmapi.lib")

#include "d3d_host.h"
#include "overlay.h"
#include "aa.h"
#include "ao.h"
#include "pads.h"
#include "vk_probe.h"

#define WIN_CLASS          "TwocD3DBridge"
#define WIN_TITLE           "Crash TWOC - D3D bridge (host)"
#define DUMP_FIRST_FRAMES   8
#define DUMP_PERIOD_MS      10000

static CRITICAL_SECTION g_lock;
/* Step 79 (notes 18.82): the recording lock. The game side's entry points
 * (the recording, the tables it keeps, the handover) take it; the replay
 * side's work takes g_lock. On one thread a game-side entry point takes
 * both, g_rec first, so nothing changes; with a render thread the game
 * thread records under g_rec alone while the render thread replays under
 * g_lock. Order: g_rec before g_lock, never the other way. */
static CRITICAL_SECTION g_rec;

/* ── Step 78: the game side and the replay side (notes 18.81) ─────────
 *
 * Every function here that calls D3D11, DXGI or xbox_d3d8 (or reads what
 * they hold: the render targets, the host textures, the swap chain) is the
 * replay side's, marked REPLAY_SIDE(); the recording and the tables the
 * game side keeps (what the host holds, the programs parsed, the targets'
 * Data, the settings the game's translation reads) are the game side's,
 * GAME_SIDE(). On one thread the game thread is both, one after the other:
 * it records (SIDE_GAME) and at its Swap hands the frame over and replays it
 * (SIDE_REPLAY); a draw made at once, frames not being recorded, is both at
 * once (SIDE_NOW), as are the checks that draw at once. With
 * RECOMP_D3D_BRIDGE_THREADCHECK=1 each marked function checks the side its
 * thread is on and ends the process (a [D3DB] THREADCHECK line, exit code
 * 120) when it doesn't fit: replay-side code while the game side records (it
 * would race a render thread for the device) or game-side code while the
 * replay side replays (it would race the game for its tables). Step 79: with
 * the render thread on (below) the replay side is that thread: replay-side
 * code on any other thread fails too (but for a draw made at once while the
 * game waits for each frame, the sync check), and game-side code on it. */
enum { SIDE_ANY, SIDE_GAME, SIDE_REPLAY, SIDE_NOW };
static __declspec(thread) int t_side;  /* the side this thread is on now */
static int g_tcheck;                    /* RECOMP_D3D_BRIDGE_THREADCHECK */

/* ── Step 79: the render thread (notes 18.82) ──────────────────────
 *
 * With Frame rate: Display (any target above the game's 60) and the render
 * thread on, the replay side runs on a thread of its own: pairing, the
 * replays, the frame plan, the presents and the pace wait, texture uploads,
 * shader creation, render targets, the overlay and its hold, resizing,
 * pacing and anti-aliasing changes, dumps and checks. The game thread
 * records, hands its frame over at the guest's Swap (one slot of two), and
 * goes on to the next: hand-over depth 1, the game waits in its Swap until
 * the render thread has finished replaying the frame before the one it just
 * handed over (the slot it records into next), so it is never more than
 * one frame ahead. Frame rate: Game (60) keeps no thread and the old path,
 * and so does the setting Off. Engaged and left only at the game's Swap with
 * nothing recorded and the thread drained (g_thr), so the replay-side state
 * always has one owner. See "Step 79: the render thread's loop" below. */
static int g_thr;                       /* game side: frames go to the render thread now */
static volatile LONG g_thr_v;           /* the same, for the checks and the overlay on other threads */
static volatile LONG g_rt_mode;         /* the setting: TWOC_RT_OFF, TWOC_RT_ON or TWOC_RT_AUTO */
static int g_rt_sync;                   /* RECOMP_RENDER_THREAD=sync: the game waits for each frame (a check) */
static unsigned g_rt_cores, g_rt_logical;   /* the CPU's physical cores and logical processors */
#define RT_AUTO_CORES 4                 /* Auto: on with this many physical cores or more */
static __declspec(thread) int t_rt;     /* this thread is the render thread */

static void side_fail(const char *fn, int want)
{
    static const char *name[] = { "any", "game", "replay", "now" };

    fprintf(stderr, "[D3DB] THREADCHECK: %s, the %s side's, ran on the %s side (thread %lu%s%s): ending the process\n",
            fn, name[want], name[t_side], GetCurrentThreadId(), t_rt ? ", the render thread" : "",
            g_thr_v && !t_rt ? ", the render thread on" : "");
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 120);
}

static void side_check(const char *fn, int want)
{
    if (want == SIDE_REPLAY ? t_side == SIDE_GAME || (g_thr_v && !t_rt && !(t_side == SIDE_NOW && g_rt_sync))
                            : t_side == SIDE_REPLAY || t_rt)
        side_fail(fn, want);
}
#define REPLAY_SIDE() do { if (g_tcheck) side_check(__FUNCTION__, SIDE_REPLAY); } while (0)
#define GAME_SIDE() do { if (g_tcheck) side_check(__FUNCTION__, SIDE_GAME); } while (0)

/* This thread on side `s` from now; returns the side it was on. */
static int side_set(int s)
{
    int was = t_side;

    t_side = s;
    return was;
}

/* Step 79: a game-side entry point's locks: g_rec, and g_lock with it
 * while the game thread may draw at once (no render thread, or one it waits
 * for each frame: the sync check; `now` 1: a check that draws at once,
 * always). With the render thread on, g_rec alone: it records beside the
 * replay. */
static __declspec(thread) int t_rec_both;

static void rec_lock(int now)
{
    EnterCriticalSection(&g_rec);
    t_rec_both = now || !g_thr || g_rt_sync;
    if (t_rec_both)
        EnterCriticalSection(&g_lock);
}

static void rec_unlock(void)
{
    if (t_rec_both)
        LeaveCriticalSection(&g_lock);
    LeaveCriticalSection(&g_rec);
}

/* Step 79: where the game thread (in its Swap) and the render thread are,
 * for the deadlock watchdog (rt_watchdog): what each is doing, since when
 * (QPC) and for which guest swap. */
enum {
    WD_G_RUN, WD_G_SLOT, WD_G_HOLD, WD_G_SYNC, WD_G_DRAIN,
    WD_R_IDLE, WD_R_TAKE, WD_R_PASS, WD_R_PACE, WD_R_PRESENT, WD_R_OVERLAY, WD_R_TAIL, WD_N
};
static const char *const g_wd_what[WD_N] = {
    "running the game", "waiting for its next slot (the render thread still on the frame before)",
    "waiting while the overlay holds the game", "waiting for the render thread to finish the frame (sync)",
    "waiting for the render thread to drain", "waiting for a frame", "taking a frame (settings, uploads)",
    "replaying under the host's lock", "waiting to present (the pace)", "presenting",
    "holding the game under the overlay", "after the frame's presents (the plan, the overlay)",
};
static struct {
    volatile LONG what, swap;
    volatile LONG64 since;
    volatile DWORD tid;
} g_wd[2];                              /* [0] the game thread, [1] the render thread */
static double g_wd_secs = 5.0;          /* RECOMP_D3D_BRIDGE_RT_WATCHDOG: the watchdog's limit (0 off) */

static void wd_set(int who, int what, unsigned swap)
{
    LARGE_INTEGER q;

    QueryPerformanceCounter(&q);
    if (swap)                           /* 0: the same swap as before */
        g_wd[who].swap = (LONG)swap;
    g_wd[who].since = q.QuadPart;
    g_wd[who].tid = GetCurrentThreadId();
    InterlockedExchange(&g_wd[who].what, what);
}
#define wd_rt(what, swap) do { if (t_rt) wd_set(1, (what), (swap)); } while (0)
static volatile LONG g_ready;
static HWND g_hwnd;                     /* visible: the flip-model swap chain */
static HWND g_hwnd_dev;                 /* hidden: xbox_d3d8's own swap chain */
static IDXGISwapChain1 *g_flip;
static IDXGISwapChainMedia *g_media;    /* step 90: g_flip's, for its presentation mode (NULL: none) */
static int g_media_tried;
static unsigned g_w, g_h, g_interval;   /* g_w, g_h: the host back buffer */
/* Step 90 (notes 18.93): the render scale. The host draws at render_scale
 * percent of the render size (g_w x g_h, the back buffer) and shows it at
 * the render size (g_vw x g_vh: the window's client and its swap chain),
 * stretched by the blit; 100 (the default): the two are the same, as
 * before. Largest side within the device's largest texture. */
static unsigned g_vw, g_vh;
static volatile LONG g_rscale = 100;

static void scaled_size(unsigned vw, unsigned vh, int pct, unsigned *w, unsigned *h)
{
    twoc_render_scaled(vw, vh, pct, w, h);  /* settings.c, step 91: the overlay's too */
}
static unsigned g_gw, g_gh;             /* the guest back buffer (18.56) */
static double g_wide;                   /* 18.56: the host picture's width / height if wider than the guest's, else 0 */
static int g_dpi_aware;                 /* 18.56: the window in real pixels (a render size was asked for) */
static double g_frame_hz;               /* S7: the frame limiter's rate, 0 = off */
static HANDLE g_latency;                /* vsync pacing (18.45): the frame-latency waitable object */
#define VSYNC_WAIT_MS 50                /* the longest a present waits for it (18.46) */
#define VSYNC_RETRY_S 10                /* step 63: after 3 waits missed, the limiter for this long */
static HANDLE g_vs_held;                /* step 63: g_latency while the limiter stands in, else NULL */
static unsigned g_vs_held_interval;
static LONGLONG g_vs_retry;             /* the QPC tick to try vsync pacing again */
static SRWLOCK g_why_lock = SRWLOCK_INIT;
static char g_pace_why[192];            /* step 63: why the pacing is what it is ("" vsync as it should be) */
static IDirect3DDevice8 *g_dev;
static ID3D11Texture2D *g_staging;
static char g_dump_prefix[512];
static unsigned g_dump_seq, g_dump_first;
static unsigned g_dump_every;           /* RECOMP_D3D_BRIDGE_DUMP_EVERY: also every Nth guest swap */
static unsigned g_dump_from, g_dump_to; /* RECOMP_D3D_BRIDGE_DRAWLOG: every swap in this range */
static ULONGLONG g_last_dump_ms;
static int g_frame_work;                /* mirrored work since the last present */
static int g_dump_rt;                   /* RECOMP_D3D_BRIDGE_DUMP_RT: render targets too (S4) */
/* Step 79: the counters, per side: g_st the replay side's, g_stg the game
 * side's (the recording, the texture table, the overlay pass and the HUD).
 * d3dh_stats adds them up. */
static d3dh_stats_t g_st, g_stg;
static LARGE_INTEGER g_qpf;                 /* step 60: QueryPerformanceFrequency */
static int g_replay;                    /* step 60, RECOMP_D3D_BRIDGE_REPLAY: passes at each present, 0 off */
static int g_replay_check;              /* RECOMP_D3D_BRIDGE_REPLAY_CHECK: drawn at once too, and compared */
static int g_bake = 1;                  /* step 87, RECOMP_D3D_BRIDGE_BAKE: baked draws ("Step 87: baked draws"), on */
static volatile LONG g_interp_want;     /* step 61: RECOMP_FRAMERATE=display / the overlay, taken at a frame's end */
static int g_interp;                    /* step 61: frames recorded and drawn as interpolated sub-frames */
static unsigned g_subdump_from, g_subdump_to;   /* RECOMP_D3D_BRIDGE_SUBDUMP=A-B: each sub-frame of those swaps */
static char g_subdump_trigger[MAX_PATH];        /* RECOMP_D3D_BRIDGE_SUBDUMP=@FILE: the next N once FILE (N) appears */
static int g_subdump_cuts;                      /* RECOMP_D3D_BRIDGE_SUBDUMP=cuts: each held frame and the 2 after */
static int g_ovl_defer;                 /* step 65: the overlay pass's calls held to its end (the HUD at the edges) */
/* Step 60/61: host calls recorded, run at the present (step 65: or at the
 * overlay pass's end). */
#define REC_ON() (g_replay || g_interp || g_ovl_defer)
static double g_icut_step = 2.0, g_icut_turn = 30.0, g_icut_match = 0.5;   /* RECOMP_D3D_BRIDGE_INTERP_CUT */
static double g_isnap = 1.5;            /* step 62: RECOMP_D3D_BRIDGE_INTERP_OBJECTS, an object stepping further snaps; 0: objects off */
static int g_iskel = 1;                 /* step 66: RECOMP_D3D_BRIDGE_INTERP_SKELETONS=0, bones one by one as before */
static int g_imatch = 1;                /* step 68: RECOMP_D3D_BRIDGE_INTERP_MATCH=0, step 62's pairing in draw order */
static int g_ipairlog, g_irec_this;     /* step 68: RECOMP_D3D_BRIDGE_PAIRLOG=1, each recorded frame's pairs logged */
static unsigned g_ipair_swap;           /* the swap being paired (the pair log) */
static int g_interp_t0;                 /* RECOMP_D3D_BRIDGE_INTERP_T0: sub-frames at t = 0 .. (k-1)/k (a check) */
static int g_iq_on;                     /* step 61: the visible swap chain's queue is the interpolation's */
static int g_imid;                      /* RECOMP_D3D_BRIDGE_INTERP_MID=1: sub-frames held to mid-refresh too */
static unsigned g_iq_buffers = 4, g_iq_latency = 4;     /* RECOMP_D3D_BRIDGE_INTERP_QUEUE=buffers,latency */
static char g_irec_trigger[MAX_PATH];   /* RECOMP_D3D_BRIDGE_INTERP_REC=FILE: once FILE.go appears, N frames' draws */
static unsigned g_irec_left;
static double g_rp_ms[2], g_rp_cmds[2]; /* step 74: replay passes' time and commands, plain [0] and interpolated [1] */
static unsigned g_rp_n[2];              /* step 74: those passes */
static volatile LONG g_iadapt = 1;      /* step 74: 0 always the display's count; step 75: the setting (RECOMP_FRAMERATE_ADAPTIVE), live */
static LONG g_iadapt_seen = 1;          /* step 75: g_iadapt as ia_frame last applied it */
static unsigned g_ifixed;               /* step 74: RECOMP_D3D_BRIDGE_INTERP_FIXED=K, always K sub-frames (a measurement aid) */
static double g_burn_ms, g_burn_j;      /* step 82: RECOMP_DEV_GAME_BURN_MS=N[,J], N (+-J) ms spun on the game side each Swap (a test aid) */
/* Step 77: the frame-rate plan (fp_plan, notes 18.80). While it paces, game
 * frames are laid on a clock of slots: the display's refreshes under vsync,
 * or the frame limiter's ticks. A game frame spans `span` slots (2.4024 at
 * 144 Hz; an integer on 2, 3 or 4 x 59.94 Hz, as before), and its presents
 * go on a grid of whole slots (g_fc), each drawn at t = its slot's place
 * between the game frame's start and end. */
#define FP_MAX_STEPS 8
static struct {
    int on;                             /* the plan paces (vsync, or the limiter at slot_hz) */
    int limiter;                        /* slots are the limiter's ticks (variable refresh), not refreshes */
    double span;                        /* slots a game frame */
    double slot_hz;                     /* slots a second */
    double disp_hz;                     /* the display's mode rate */
    double game_hz;                     /* the game's rate it was made for (59.94) */
    unsigned interval;                  /* the pacing it set: DXGI interval (vsync) */
    unsigned gen;                       /* bumped at every plan */
    unsigned nsteps;                    /* the rates it can go down to (adaptive): */
    double step[FP_MAX_STEPS];          /* slots between presents; 0: one present a game frame, as recorded */
    double cost_i[FP_MAX_STEPS], cost_p[FP_MAX_STEPS], cost_f[FP_MAX_STEPS];   /* a game frame's interpolated
                                         * passes, plain passes, and pairings at each step (mean) */
    int fr;                             /* the setting it was made for (1: the display's rate, N Hz) */
    double want;                        /* that rate (the display's for 1) */
    int even;                           /* shown evenly: a refresh or whole fraction, or variable refresh */
    int probing;                        /* the limiter at a rate variable refresh would show: measuring it */
    int needs_vrr;                      /* the target isn't a whole fraction: the window mode matters */
    int fs;                             /* fullscreen when made */
    char note[256];                     /* what it does and why, for the overlay */
} g_fp;
static volatile LONG g_fp_replan;       /* made again at the end of the next present (a measurement, a new mode) */
static struct {
    double pos;                         /* the last present's slot */
    double end;                         /* the last game frame's end (slot) */
    unsigned gen;                       /* the plan they count for (0: none yet) */
} g_fc;
#define FP_MAX_PRESENTS 16
typedef struct {
    unsigned n;                         /* presents this game frame */
    double t[FP_MAX_PRESENTS];          /* each one's place between the frame before and this one (1: this one) */
    double t0[FP_MAX_PRESENTS];         /* ... a step earlier (RECOMP_D3D_BRIDGE_INTERP_T0, a check) */
    double slots[FP_MAX_PRESENTS];      /* slots since the present before */
    int interp;                         /* some t < 1: drawn interpolated */
} fp_frame_t;

/* Step 54: borderless fullscreen. g_fs and the client size are the window
 * thread's; g_buf_w x g_buf_h is the visible swap chain's, changed only by a
 * present (flip_fit). */
static volatile LONG g_fs;              /* the window is borderless over its monitor */
static int g_fs_at_start;               /* RECOMP_FULLSCREEN */
static volatile LONG g_client_w, g_client_h;
static unsigned g_buf_w, g_buf_h;
static UINT g_flip_flags;               /* the swap chain's flags, for ResizeBuffers */
#define WM_TWOC_FULLSCREEN (WM_APP + 1) /* wParam 1 on, 0 off, 2 toggle, 3 the deferred switch (step 90) */
#define WM_TWOC_FIT (WM_APP + 2)        /* the host picture's size changed: fit the window to it */

/* Step 90 (notes 18.93): the Legion Go's startup stall. 3 of 12 starts with
 * fullscreen on stopped taking frames ~0.2 s after the first present (the
 * window was made borderless before the device and its swap chain), and the
 * render thread then sat in Present 11-19 s until alt+tab; the switch made
 * mid-run, from a window, never did. So a start with fullscreen on begins in
 * the window and the window thread makes it borderless once FS_DEFER_SHOWN
 * presents have been shown (g_fs_defer; the presenting thread counts them in
 * present_one and posts the switch), the way Alt+Enter does mid-run. The
 * overlay and the ini see fullscreen on throughout (fs_wanted).
 *
 * The desktop reproduced it (RTX, 4K at 240 Hz, run from a console like the
 * package): in 7 of 16 starts with the game's window in front (0 of 16 with
 * the console in front) frames stopped at the switch into fullscreen, the
 * moment DXGI took the swap chain out of DWM's composition onto a hardware
 * plane ("overlay", present_mode), and the splash froze at ~1 frame a second
 * until the run ended; the deferred switch stalled as often (8 of 16). A
 * fullscreen swap chain one line shorter than the window (g_fs_buf_short,
 * stretched by the blit; the window still covers the screen, so the taskbar
 * hides and it looks the same) stays composed: 0 of 32. So that is the
 * default; RECOMP_FULLSCREEN_COMPOSED=0 lets DXGI promote it as before, 1
 * makes the window itself a line short instead (a test).
 *
 * And a recovery that does what alt+tab did, from the window thread (a timer:
 * a blocked Present can't hold it, and it calls no DXGI): when the display
 * releases no frame 3 times running (pace) and then shows none for
 * DR_STALL_MS (in case frames come again by themselves; none did in the
 * desktop's 20 watched episodes), or the presenting thread has been inside Present
 * over DR_PRESENT_MS, step 1 activates the window and puts it
 * on top with SWP_FRAMECHANGED; if no frame is shown within DR_STEP_MS, step
 * 2 leaves fullscreen (it comes back after FS_DEFER_SHOWN shown, as at the
 * start); then step 3 minimises and restores it. Once frames are shown again
 * the presenting thread makes the swap chain again (g_flip_remake), the
 * runs that stalled having stuttered after (notes 18.92). Never while the
 * window is minimised or another program's window is in front (the player
 * put it there); each step is logged. */
#define FS_DEFER_SHOWN 60
#define FS_DEFER_MAX_MS 5000.0          /* no frame statistics at all: switch this long after the first present */
#define DR_PRESENT_MS 1000.0
#define DR_STALL_MS 500.0               /* no frame shown this long after pace's 3 misses: a stall */
#define DR_STEP_MS 500.0
#define DR_EPISODES 4                   /* recoveries a run, at most */
#define DR_TIMER 0x7D90
static volatile LONG g_fs_defer;        /* fullscreen asked for at the start, the switch not made yet */
static LONG g_fs_defer_n = FS_DEFER_SHOWN;   /* RECOMP_FULLSCREEN_DEFER: frames shown first (0: at once, as before) */
static LONG g_fs_short, g_fs_buf_short = 1;    /* RECOMP_FULLSCREEN_COMPOSED: 2 (unset) the swap chain 1 line short, 1 the window, 0 neither */
static volatile LONG g_fs_switching;    /* step 90: the window thread restyling it (the focus pause waits) */
static volatile LONG64 g_fs_switch_q;   /* QPC of the last switch either way */
static volatile LONG g_fs_defer_base;   /* g_shown_seq when the deferred switch was armed */
static int g_fs_defer_from_switch;      /* the last switch to fullscreen was the deferred one (window thread) */
static volatile LONG64 g_present_since; /* QPC entering IDXGISwapChain1_Present, 0 outside it */
static volatile LONG g_stall_seq;       /* bumped by pace's 3 misses */
static volatile LONG g_shown_seq;       /* presents seen shown by the frame statistics */
static volatile LONG g_flip_remake;     /* the window thread asks: the swap chain made again at the next present */
static volatile LONG g_disp_recovering; /* a recovery under way (the focus pause waits) */
static LONGLONG g_first_present_q;      /* QPC of the first present */

static int fs_wanted(void)
{
    return g_fs || g_fs_defer;
}

/* Step 90: "Pause when in the background" (pause_unfocused). The window
 * thread decides (bg_eval): the window minimised or another window in front,
 * once it has had the focus at least once, and not while fullscreen waits
 * for its first frames, is being switched (and 2 s after), or the display
 * recovers from a stall; the game thread then waits at the start of its
 * Swap, the session muted, as the overlay's hold does (bg_hold). */
static volatile LONG g_pause_bg;        /* the setting */
static volatile LONG g_bg_hold;         /* hold the game now */
static int g_ever_active;               /* the window has had the focus (window thread) */
#define BG_AFTER_SWITCH_MS 2000.0

/* Step 54: the settings overlay and live settings (overlay.cpp). */
static unsigned g_dev_w, g_dev_h;       /* xbox_d3d8's own back buffer: its size is fixed */
static struct {                         /* stands in for it after a live size change (d3dh_resize), or with MSAA */
    IDirect3DTexture8 *tex;             /* what is sampled, copied and shown */
    IDirect3DSurface8 *surf, *depth;    /* what is drawn into: tex's level 0, or (step 58) multisampled surfaces */
    unsigned ms;                        /* step 58: surf's samples a pixel, 0 when it is tex's level 0 */
    int dirty;                          /* step 58: drawn into since its last resolve */
    ID3D11ShaderResourceView *dsrv;     /* step 92: depth's view for ambient occlusion (only while it is on) */
} g_bb;
/* Step 92: ambient occlusion ("Step 92: ambient occlusion", below). */
static volatile LONG g_ao_mode;         /* RECOMP_AO / the overlay: TWOC_AO_*, read by both sides */
static volatile LONG g_ao_strength = TWOC_AO_STRENGTH_DEFAULT;  /* RECOMP_AO_STRENGTH, percent */
static int g_ao_depth;                  /* the back buffer's depth is readable (replay side) */
static float g_ao_radius = 0.5f;        /* RECOMP_AO_RADIUS: the game's units (a test aid) */
static int g_ao_half, g_ao_debug;       /* RECOMP_AO_HALF=1 half resolution, RECOMP_AO_DEBUG=1 the occlusion alone */
static int g_ao_denoise = 2;            /* RECOMP_AO_DENOISE: the denoiser's passes (a test aid) */
static volatile LONG g_aniso = 1;       /* RECOMP_ANISOTROPY: 1 leaves the game's filtering */
static int g_aa;                        /* step 58, RECOMP_ANTIALIASING: TWOC_AA_* */
static unsigned g_msaa = 1;             /* step 58: the samples a pixel MSAA asks for, 1 without */
static volatile LONG g_ovl_open;        /* Esc: the next present runs the overlay */
static volatile LONG g_ovl_pauses = 1;  /* step 79: "Pause the game while this menu is open", for the game side */
static volatile LONG g_show_fps;        /* step 82: "Show frame rate" (RECOMP_SHOW_FPS): the counter in the corner */
static volatile LONG g_startmsg = 1;    /* step 86: "Show the startup message" (RECOMP_STARTUP_MESSAGE, unset: on) */
static LONGLONG g_startmsg_t0;          /* its first frame (QPC); 0 before it */
#define STARTMSG_MS 8000.0              /* shown this long from the first frame, */
#define STARTMSG_FADE_MS 1000.0         /* fading out over its last part */
static SRWLOCK g_pause_lock = SRWLOCK_INIT;
static d3dh_apply_fn g_apply_game, g_apply_host;   /* step 78: the bridge's parts of a settings change */
static double g_rwide;                  /* step 78: g_wide as the game side records under it (d3dh_record_wide) */

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
/* Step 78: g_rt's Data (0: none) for the game side, which decides at record
 * time whether a texture may be a render target (rc_maybe_target); kept by
 * the replay side as it makes and releases them (gt_set). */
static volatile LONG g_gt_data[RT_SLOTS];

static void gt_set(int i, uint32_t data)
{
    InterlockedExchange(&g_gt_data[i], (LONG)data);
}
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
    InterlockedExchange(&g_fs_switching, 1);
    if (on) {
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        placed.length = sizeof(placed);
        GetWindowPlacement(hwnd, &placed);
        style = GetWindowLongA(hwnd, GWL_STYLE);
        if (!GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
            InterlockedExchange(&g_fs_switching, 0);
            return;
        }
        InterlockedExchange(&g_fs, 1);
        SetWindowLongA(hwnd, GWL_STYLE, (style & ~(LONG)(WS_OVERLAPPEDWINDOW)) | (LONG)WS_POPUP);
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top - g_fs_short, SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
        fprintf(stderr, "[D3DB] host window: borderless fullscreen, %ldx%ld at %ld,%ld (Alt+Enter: back)%s\n",
                mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top - g_fs_short,
                mi.rcMonitor.left, mi.rcMonitor.top,
                g_fs_short ? " (1 line short of the screen, so DWM keeps composing it: RECOMP_FULLSCREEN_COMPOSED=1)"
                : g_fs_buf_short ? " (its swap chain 1 line short, so DWM keeps composing it)" : "");
    } else {
        InterlockedExchange(&g_fs, 0);
        SetWindowLongA(hwnd, GWL_STYLE, style);
        SetWindowPlacement(hwnd, &placed);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        fprintf(stderr, "[D3DB] host window: windowed\n");
    }
    {
        LARGE_INTEGER q;
        QueryPerformanceCounter(&q);
        InterlockedExchange64(&g_fs_switch_q, q.QuadPart);
    }
    InterlockedExchange(&g_fs_switching, 0);
}

/* ── Step 90: the display's recovery (window thread) ─────────── */

static struct {
    int step;                           /* 0 watching, 1-3 the step last taken, 4 given up this episode */
    int episodes;
    LONG stall_seen, shown_at;          /* g_stall_seq handled; g_shown_seq when the step was taken */
    LONGLONG at;                        /* QPC of the step */
    int told_alone;                     /* "left alone" logged this episode */
    int restore_due;                    /* step 3: restore at the next tick */
    LONGLONG stall_q;                   /* QPC the episode began */
    int after_switch;                   /* episodes begun within 3 s of a deferred switch to fullscreen */
    int watch;                          /* 3 misses seen: is a frame shown within DR_STALL_MS? */
    LONG watch_shown, watch_presents;   /* g_shown_seq and the presents at the misses */
} g_dr;

static double qpc_ms(LONGLONG a, LONGLONG b)
{
    return (double)(b - a) * 1000.0 / (double)g_qpf.QuadPart;
}

/* The window in front belongs to the player's game: ours, its console (or
 * the terminal that owns the console's window, Windows Terminal), or another
 * of this process's windows (or none). */
static int dr_ours_in_front(HWND hwnd)
{
    HWND fg = GetForegroundWindow(), con = GetConsoleWindow();
    DWORD pid = 0;

    if (!fg || fg == hwnd || (con && (fg == con || fg == GetAncestor(con, GA_ROOTOWNER) || fg == GetWindow(con, GW_OWNER))))
        return 1;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

/* Fullscreen again once FS_DEFER_SHOWN more presents have been shown (the
 * presenting thread posts the switch, present_one). */
static void fs_defer_arm(void)
{
    InterlockedExchange(&g_fs_defer_base, g_shown_seq);
    InterlockedExchange(&g_fs_defer, 1);
}

/* Frames are shown again after a stall: the swap chain made again, in the
 * window (a swap chain made on the borderless window is what stalled at the
 * start), and fullscreen again after FS_DEFER_SHOWN shown; unless this is
 * the second stall within 3 s of a deferred switch to fullscreen: then the
 * window stays for the run. */
static void dr_remake(HWND hwnd)
{
    int recent = g_fs_switch_q && g_dr.stall_q >= g_fs_switch_q && qpc_ms(g_fs_switch_q, g_dr.stall_q) < 3000.0;

    if (g_fs || g_fs_defer) {
        if (recent && g_fs_defer_from_switch && ++g_dr.after_switch >= 2) {
            InterlockedExchange(&g_fs_defer, 0);
            set_fullscreen(hwnd, 0);
            fprintf(stderr, "[D3DB] host display: it stalled again within 3 s of going fullscreen: in the window for"
                    " the rest of this run (Alt+Enter to try fullscreen again); the swap chain made again\n");
        } else {
            if (g_fs)
                set_fullscreen(hwnd, 0);
            fs_defer_arm();
            fprintf(stderr, "[D3DB] host display: the swap chain made again in the window, fullscreen again after %d"
                    " frames shown\n", g_fs_defer_n);
        }
    } else {
        fprintf(stderr, "[D3DB] host display: the swap chain made again at the next present\n");
    }
    InterlockedExchange(&g_flip_remake, 1);
}

/* Step 90: the window in the background or not, for the focus pause. */
static void bg_eval(HWND hwnd)
{
    LARGE_INTEGER q;
    HWND fg = GetForegroundWindow();
    int iconic = IsIconic(hwnd), bg = iconic || fg != hwnd, hold, excluded;

    if (fg == hwnd && !iconic)
        g_ever_active = 1;
    QueryPerformanceCounter(&q);
    excluded = g_fs_defer || g_fs_switching || g_disp_recovering ||
               (g_fs_switch_q && qpc_ms(g_fs_switch_q, q.QuadPart) < BG_AFTER_SWITCH_MS);
    hold = g_bg_hold ? g_pause_bg && bg : g_pause_bg && g_ever_active && bg && !excluded;
    if (hold == (int)g_bg_hold)
        return;
    InterlockedExchange(&g_bg_hold, hold);
    fprintf(stderr, "[D3DB] host window %s: %s\n", hold ? (iconic ? "minimised" : "in the background") : "in front again",
            hold ? "the game holds, muted (pause when in the background)" : "the game goes on");
}

static void dr_tick(HWND hwnd)
{
    LARGE_INTEGER q;
    LONGLONG since = g_present_since;
    LONG stall = g_stall_seq, shown = g_shown_seq;
    double in_present;
    int stalled;

    QueryPerformanceCounter(&q);
    in_present = since ? qpc_ms(since, q.QuadPart) : 0.0;
    if (g_dr.restore_due) {             /* step 3's second half */
        g_dr.restore_due = 0;
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        fprintf(stderr, "[D3DB] host display recovery: step 3: the window restored\n");
    }
    if (g_dr.step == 0) {
        char why[128];
        if (stall != g_dr.stall_seen) {
            /* 3 misses: watched first, a recovery only if no frame is shown for DR_STALL_MS after them (on
             * the desktop every such episode was a stall, but a slow display could take frames again). */
            g_dr.stall_seen = stall;
            g_dr.stall_q = q.QuadPart;
            g_dr.watch = 1;
            g_dr.watch_shown = shown;
            g_dr.watch_presents = (LONG)g_st.presents;
            return;
        }
        if (g_dr.watch && shown - g_dr.watch_shown >= 3) {
            fprintf(stderr, "[D3DB] host display: frames shown again %.0f ms after the display's 3 misses (no"
                    " recovery needed)\n", qpc_ms(g_dr.stall_q, q.QuadPart));
            g_dr.watch = 0;
            return;
        }
        if (g_dr.watch && qpc_ms(g_dr.stall_q, q.QuadPart) >= DR_STALL_MS)
            snprintf(why, sizeof(why), "no frame shown for %.0f ms after the display released none in %d ms 3 times"
                     " running (%ld presents meanwhile)", qpc_ms(g_dr.stall_q, q.QuadPart), VSYNC_WAIT_MS,
                     (long)((LONG)g_st.presents - g_dr.watch_presents));
        else if (in_present > DR_PRESENT_MS)
            snprintf(why, sizeof(why), "the presenting thread inside Present for %.0f ms", in_present);
        else
            return;
        g_dr.watch = 0;
        if (!g_dr.stall_q || in_present > DR_PRESENT_MS)
            g_dr.stall_q = q.QuadPart;
        if (IsIconic(hwnd) || !IsWindowVisible(hwnd) || !dr_ours_in_front(hwnd)) {
            if (!g_dr.told_alone++)
                fprintf(stderr, "[D3DB] host display stall (%s) with the window %s: left alone\n", why,
                        IsIconic(hwnd) || !IsWindowVisible(hwnd) ? "minimised" : "behind another program's");
            return;
        }
        if (g_dr.episodes >= DR_EPISODES) {
            if (!g_dr.told_alone++)
                fprintf(stderr, "[D3DB] host display stall (%s): %d recoveries already, left alone\n", why,
                        DR_EPISODES);
            return;
        }
        g_dr.episodes++;
        g_dr.told_alone = 0;
        InterlockedExchange(&g_disp_recovering, 1);
        SetForegroundWindow(hwnd);
        BringWindowToTop(hwnd);
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_dr.step = 1;
        g_dr.shown_at = shown;
        g_dr.at = q.QuadPart;
        fprintf(stderr, "[D3DB] host display recovery %d: %s (%s, presents shown %ld); step 1, as alt+tab: the"
                " window activated and put on top (SWP_FRAMECHANGED)\n", g_dr.episodes, why,
                g_fs ? "fullscreen" : "in a window", (long)shown);
        return;
    }
    if (g_dr.step >= 4) {               /* given up: watch again once frames come */
        if (shown - g_dr.shown_at >= 3 && in_present < 100) {
            fprintf(stderr, "[D3DB] host display: frames shown again %.0f ms after the recovery gave up\n",
                    qpc_ms(g_dr.at, q.QuadPart));
            dr_remake(hwnd);
            g_dr.step = 0;
            g_dr.stall_seen = g_stall_seq;
            InterlockedExchange(&g_disp_recovering, 0);
        }
        return;
    }
    stalled = !(shown - g_dr.shown_at >= 3 && in_present < 100);
    if (!stalled) {
        fprintf(stderr, "[D3DB] host display recovery %d: frames shown again %.0f ms after step %d\n",
                g_dr.episodes, qpc_ms(g_dr.at, q.QuadPart), g_dr.step);
        dr_remake(hwnd);
        g_dr.step = 0;
        g_dr.stall_seen = g_stall_seq;  /* the misses of this episode */
        InterlockedExchange(&g_disp_recovering, 0);
        return;
    }
    if (qpc_ms(g_dr.at, q.QuadPart) < (g_dr.step == 1 ? DR_STEP_MS : 2 * DR_STEP_MS))
        return;
    g_dr.at = q.QuadPart;
    g_dr.shown_at = shown;
    if (g_dr.step == 1) {
        g_dr.step = 2;
        if (g_fs) {
            fs_defer_arm();             /* back once frames are shown in the window, as at the start */
            set_fullscreen(hwnd, 0);
            fprintf(stderr, "[D3DB] host display recovery %d: still no frame shown (%ld shown, %u presents); step 2:"
                    " out of fullscreen (back after %ld frames shown in the window)\n", g_dr.episodes, (long)shown,
                    g_st.presents, (long)g_fs_defer_n);
        } else {
            SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            fprintf(stderr, "[D3DB] host display recovery %d: still no frame shown (%ld shown, %u presents); step 2:"
                    " the window framed again\n", g_dr.episodes, (long)shown, g_st.presents);
        }
    } else if (g_dr.step == 2) {
        g_dr.step = 3;
        ShowWindow(hwnd, SW_MINIMIZE);
        g_dr.restore_due = 1;
        fprintf(stderr, "[D3DB] host display recovery %d: still no frame shown (%ld shown, %u presents); step 3:"
                " the window minimised, then restored\n", g_dr.episodes, (long)shown, g_st.presents);
    } else {
        g_dr.step = 4;
        fprintf(stderr, "[D3DB] host display recovery %d: still no frame shown after step 3 (%s for %.0f ms):"
                " left to the player (alt+tab)\n", g_dr.episodes, in_present > 0 ? "inside Present" : "presenting",
                in_present);
        InterlockedExchange(&g_disp_recovering, 0);
    }
}

/* Step 54: a live change of the host picture's size. In a window, the client
 * becomes the new size, shrunk to 90% of the work area if needed, around the
 * window's centre (in this window's own pixels: real ones if it was made DPI
 * aware, Windows' scaled ones if not). Fullscreen keeps the monitor. */
static void fit_window(HWND hwnd)
{
    RECT w, c, wa;
    LONG cw = (LONG)g_vw, ch = (LONG)g_vh, fw, fh, cx, cy;

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
    case WM_SETFOCUS: case WM_KILLFOCUS: case WM_CHAR:     /* step 91: WM_CHAR, typed numbers */
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
    if (hwnd == g_hwnd && msg == WM_KEYDOWN && (wp == VK_F1 || wp == VK_ESCAPE)) {
        /* Step 86: F1 opens and closes the settings overlay (step 54's Esc before). Esc is back / cancel: with
         * the overlay open it ends a binding's wait (step 57), else closes the overlay; closed, it does
         * nothing. F1 during a binding's wait ends the wait and closes the overlay. */
        if (!(lp & (1 << 30)) && g_ready) {
            int was_capture = g_ovl_open && ovl_cancel_capture();
            if (wp == VK_ESCAPE && was_capture)
                return 0;
            if (!g_ovl_open && wp == VK_F1) {
                ovl_opened();
                InterlockedExchange(&g_ovl_open, 1);
                fprintf(stderr, "[OVL] F1: the settings overlay opens\n");
            } else if (g_ovl_open) {
                InterlockedExchange(&g_ovl_open, 0);
                fprintf(stderr, "[OVL] %s: the settings overlay closes\n", wp == VK_F1 ? "F1" : "Esc");
            }
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
    case WM_DISPLAYCHANGE:              /* step 77: another mode (a refresh rate): the plan made again */
        InterlockedExchange(&g_fp_replan, 1);
        break;
    case WM_SIZE:                       /* the swap chain follows at the next present */
        if (hwnd == g_hwnd && wp != SIZE_MINIMIZED) {
            InterlockedExchange(&g_client_w, (LONG)LOWORD(lp));
            InterlockedExchange(&g_client_h, (LONG)HIWORD(lp));
        }
        if (hwnd == g_hwnd)             /* step 90: minimised or back, for the focus pause */
            bg_eval(hwnd);
        break;
    case WM_SYSKEYDOWN:                 /* Alt+Enter (step 54); the game reads no keys */
        if (wp == VK_RETURN && (lp & (1 << 29)) && !(lp & (1 << 30))) {
            int was_fs = fs_wanted();
            InterlockedExchange(&g_fs_defer, 0);    /* step 90: the player's switch, at once */
            g_fs_defer_from_switch = 0;
            set_fullscreen(hwnd, !was_fs);
            return 0;
        }
        break;
    case WM_SYSCHAR:                    /* no beep for Alt+Enter */
        if (wp == '\r')
            return 0;
        break;
    case WM_TWOC_FULLSCREEN:
        if (wp == 3) {                  /* step 90: the deferred switch, if nothing changed it meanwhile */
            if (InterlockedCompareExchange(&g_fs_defer, 0, 2) == 2) {
                g_fs_defer_from_switch = 1;
                set_fullscreen(hwnd, 1);
            }
            return 0;
        }
        {
            int was_fs = fs_wanted();
            InterlockedExchange(&g_fs_defer, 0);
            g_fs_defer_from_switch = 0;
            set_fullscreen(hwnd, wp == 2 ? !was_fs : (int)wp);
        }
        return 0;
    case WM_TIMER:
        if (wp == DR_TIMER && hwnd == g_hwnd) {     /* step 90 */
            dr_tick(hwnd);
            bg_eval(hwnd);
            return 0;
        }
        break;
    case WM_ACTIVATEAPP:                /* step 90: the focus pause, at once */
        if (hwnd == g_hwnd)
            bg_eval(hwnd);
        break;
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
    r.right = (LONG)g_vw;          /* step 90: the render size, whatever the render scale */
    r.bottom = (LONG)g_vh;
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
    if (g_hwnd && g_fs_at_start && g_fs_defer_n <= 0) {    /* RECOMP_FULLSCREEN_DEFER=0: at once, as before */
        set_fullscreen(g_hwnd, 1);
        SetForegroundWindow(g_hwnd);
        fprintf(stderr, "[D3DB] host window: fullscreen at once at the start (RECOMP_FULLSCREEN_DEFER=0)\n");
    } else if (g_hwnd && g_fs_at_start) {  /* step 54: RECOMP_FULLSCREEN; the player asked for it, so it takes focus */
        fs_defer_arm();                 /* step 90: in the window until the first frames are shown */
        ShowWindow(g_hwnd, SW_SHOW);
        SetForegroundWindow(g_hwnd);
        fprintf(stderr, "[D3DB] host window: fullscreen at the start: in the window until %d frames have been shown,"
                " then borderless fullscreen\n", g_fs_defer_n);
    } else if (g_hwnd) {
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    }
    if (g_hwnd)
        SetTimer(g_hwnd, DR_TIMER, 200, NULL);     /* step 90: the display's recovery */
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
static void set_ao(int mode, int strength);     /* step 92 */

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
    InitializeCriticalSection(&g_rec);  /* step 79 */
    QueryPerformanceFrequency(&g_qpf);
    {   /* step 78 */
        const char *e = getenv("RECOMP_D3D_BRIDGE_THREADCHECK");
        g_tcheck = e && *e && strcmp(e, "0") != 0;
        if (g_tcheck)
            fprintf(stderr, "[D3DB] host THREADCHECK: replay-side code checked against the game side's recording,"
                    " and game-side code against the replay\n");
    }
    {   /* step 79: the render thread (RECOMP_RENDER_THREAD: off, on, auto; sync a check), the CPU, the watchdog */
        const char *e = getenv("RECOMP_RENDER_THREAD"), *w = getenv("RECOMP_D3D_BRIDGE_RT_WATCHDOG"), *o;
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *buf = NULL;
        DWORD len = 0;
        g_rt_sync = e && _stricmp(e, "sync") == 0;
        g_rt_mode = twoc_rt_parse(e);
        g_rt_logical = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &len);
        if (len && (buf = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)malloc(len)) != NULL &&
            GetLogicalProcessorInformationEx(RelationProcessorCore, buf, &len))
            for (o = (const char *)buf; o < (const char *)buf + len;
                 o += ((const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)o)->Size)
                g_rt_cores++;
        free(buf);
        if (!g_rt_cores)
            g_rt_cores = g_rt_logical;
        if (w && *w)
            g_wd_secs = atof(w);
        g_ovl_pauses = !(getenv("RECOMP_OVERLAY_PAUSE") && strcmp(getenv("RECOMP_OVERLAY_PAUSE"), "0") == 0);
        g_show_fps = getenv("RECOMP_SHOW_FPS") && *getenv("RECOMP_SHOW_FPS") && strcmp(getenv("RECOMP_SHOW_FPS"), "0") != 0;
        if (g_show_fps)
            fprintf(stderr, "[D3DB] host frame-rate counter: on (show_fps)\n");
        g_pause_bg = getenv("RECOMP_PAUSE_UNFOCUSED") && *getenv("RECOMP_PAUSE_UNFOCUSED") &&
                     strcmp(getenv("RECOMP_PAUSE_UNFOCUSED"), "0") != 0;   /* step 90 */
        if (g_pause_bg)
            fprintf(stderr, "[D3DB] host: the game holds while its window is in the background (pause_unfocused)\n");
        g_startmsg = !(getenv("RECOMP_STARTUP_MESSAGE") && strcmp(getenv("RECOMP_STARTUP_MESSAGE"), "0") == 0);
        if (!g_startmsg)
            fprintf(stderr, "[D3DB] host startup message: off (startup_message)\n");
        fprintf(stderr, "[D3DB] host render thread: %s%s; this CPU has %u cores, %u logical processors%s\n",
                twoc_rt_name(g_rt_mode), g_rt_sync ? " (sync: the game waits for each frame, a check)" : "",
                g_rt_cores, g_rt_logical,
                g_rt_mode != TWOC_RT_AUTO ? "" : g_rt_cores >= RT_AUTO_CORES ? " (Auto: on with Frame rate: Display)"
                                                                              : " (Auto: off, under 4 cores)");
    }
    {   /* step 60 */
        const char *e = getenv("RECOMP_D3D_BRIDGE_REPLAY");
        g_replay = e && *e ? atoi(e) : 0;
        g_replay = g_replay < 0 ? 0 : g_replay > 8 ? 8 : g_replay;
        e = getenv("RECOMP_D3D_BRIDGE_REPLAY_CHECK");
        g_replay_check = e && *e && strcmp(e, "0") != 0;
        if (g_replay_check) {
            g_replay = g_replay > 1 ? g_replay : 1;     /* step 87: N passes, each from the frame's start */
            fprintf(stderr, "[D3DB] host replay check: each frame drawn at once and again from its recording, the"
                    " two back buffers compared\n");
        }
        if (g_replay)
            fprintf(stderr, "[D3DB] host record and replay: each frame's host draws recorded and drawn at its present"
                    "%s (%d pass%s)\n", g_replay > 1 ? ", again and again as a cost check" : "", g_replay,
                    g_replay > 1 ? "es" : "");
        e = getenv("RECOMP_D3D_BRIDGE_BAKE");           /* step 87: baked draws, 0 | 1 */
        if (e && *e)
            g_bake = strcmp(e, "0") != 0;
        fprintf(stderr, "[D3DB] host baked draws: %s (RECOMP_D3D_BRIDGE_BAKE%s)\n",
                g_bake ? "on: a frame drawn more than once is translated at its first pass, its other passes drawn"
                         " from that" : "off: every pass translated", e && *e ? "" : " unset: on");
        e = getenv("RECOMP_FRAMERATE");                 /* step 61; step 77: display or N Hz */
        if (twoc_framerate_parse(e))
            d3dh_set_framerate(twoc_framerate_parse(e));
        e = getenv("RECOMP_HUD");                       /* step 65: centre (unset) or edges */
        if (e && _stricmp(e, "edges") == 0) {
            const char *mg = getenv("RECOMP_HUD_MARGIN");
            d3dh_set_hud(1, mg ? atoi(mg) : 0);
        }
        e = getenv("RECOMP_D3D_BRIDGE_INTERP_REC");
        if (e && *e)
            snprintf(g_irec_trigger, sizeof(g_irec_trigger), "%s", e);
        e = getenv("RECOMP_D3D_BRIDGE_INTERP_MID");
        g_imid = e && *e && strcmp(e, "0") != 0;
        e = getenv("RECOMP_D3D_BRIDGE_INTERP_QUEUE");
        if (e && *e)
            sscanf(e, "%u,%u", &g_iq_buffers, &g_iq_latency);
        e = getenv("RECOMP_D3D_BRIDGE_INTERP_T0");
        g_interp_t0 = e && *e && strcmp(e, "0") != 0;
        e = getenv("RECOMP_D3D_BRIDGE_INTERP_CUT");      /* "units,degrees,matched fraction" */
        if (e && *e)
            sscanf(e, "%lf,%lf,%lf", &g_icut_step, &g_icut_turn, &g_icut_match);
        e = getenv("RECOMP_D3D_BRIDGE_INTERP_OBJECTS");  /* step 62: "0" camera only (stage 2), or the snap step */
        if (e && *e)
            g_isnap = atof(e);
        e = getenv("RECOMP_D3D_BRIDGE_INTERP_SKELETONS");    /* step 66: "0" bones one by one (a check) */
        g_iskel = !(e && strcmp(e, "0") == 0);
        e = getenv("RECOMP_D3D_BRIDGE_INTERP_MATCH");       /* step 68: "0" pairs in draw order (a check) */
        g_imatch = !(e && strcmp(e, "0") == 0);
        e = getenv("RECOMP_D3D_BRIDGE_PAIRLOG");
        g_ipairlog = e && *e && strcmp(e, "0") != 0;
        e = getenv("RECOMP_FRAMERATE_ADAPTIVE");   /* step 75: "0" always the display's count (settings.c reads */
        g_iadapt = !(e && strcmp(e, "0") == 0);    /* step 74's RECOMP_D3D_BRIDGE_INTERP_ADAPT into it) */
        g_iadapt_seen = g_iadapt;
        e = getenv("RECOMP_D3D_BRIDGE_INTERP_FIXED");      /* step 74: K sub-frames whatever the cost */
        if (e && *e)
            g_ifixed = (unsigned)atoi(e);
        e = getenv("RECOMP_DEV_GAME_BURN_MS");    /* step 82: a slower game side, to show adaptive leaves it */
        if (e && *e && atof(e) > 0) {
            const char *j = strchr(e, ',');
            g_burn_ms = atof(e);
            g_burn_j = j ? fabs(atof(j + 1)) : 0;
            g_burn_j = g_burn_j > g_burn_ms ? g_burn_ms : g_burn_j;
            fprintf(stderr, "[D3DB] test aid: %.2f ms (+-%.2f) spun on the game side at each Swap"
                    " (RECOMP_DEV_GAME_BURN_MS)\n", g_burn_ms, g_burn_j);
        }
        if (!g_iadapt || g_ifixed)
            fprintf(stderr, "[D3DB] host interpolation: sub-frames not adaptive: %s\n",
                    g_ifixed ? "a fixed count (RECOMP_D3D_BRIDGE_INTERP_FIXED)" : "always the display's count"
                    " (adaptive frame rate off)");
        e = getenv("RECOMP_D3D_BRIDGE_SUBDUMP");
        if (e && strcmp(e, "cuts") == 0) {
            g_subdump_cuts = 1;
        } else if (e && *e == '@') {
            snprintf(g_subdump_trigger, sizeof(g_subdump_trigger), "%s", e + 1);
            fprintf(stderr, "[D3DB] host sub-frame dumps on demand: the swaps after %s appears (it holds their"
                    " count)\n", g_subdump_trigger);
        } else if (e && *e) {
            char *x;
            g_subdump_from = (unsigned)strtoul(e, &x, 0);
            g_subdump_to = *x == '-' ? (unsigned)strtoul(x + 1, NULL, 0) : g_subdump_from;
        }
    }
    g_gw = width ? width : 640;
    g_gh = height ? height : 480;
    g_vw = host_w ? host_w : g_gw;
    g_vh = host_h ? host_h : g_gh;
    g_rscale = twoc_render_scale_parse(getenv("RECOMP_RENDER_SCALE"));     /* step 90 */
    scaled_size(g_vw, g_vh, g_rscale, &g_w, &g_h);
    if (g_rscale != 100)
        fprintf(stderr, "[D3DB] host render scale %ld%%: drawn at %ux%u, shown at %ux%u\n", g_rscale, g_w, g_h,
                g_vw, g_vh);
    g_wide = wide;
    g_rwide = wide;
    g_dpi_aware = dpi_aware;
    g_interval = interval;
    g_frame_hz = frame_hz > 0 ? frame_hz : 0;
    g_dev_w = g_w;
    g_dev_h = g_h;
    {
        const char *fs = getenv("RECOMP_FULLSCREEN"), *an = getenv("RECOMP_ANISOTROPY");
        int a = an ? atoi(an) : 1;
        g_fs_at_start = fs && *fs && strcmp(fs, "0") != 0;
        if (getenv("RECOMP_FULLSCREEN_DEFER") && *getenv("RECOMP_FULLSCREEN_DEFER"))     /* step 90 */
            g_fs_defer_n = atoi(getenv("RECOMP_FULLSCREEN_DEFER"));
        g_fs_short = getenv("RECOMP_FULLSCREEN_COMPOSED") && atoi(getenv("RECOMP_FULLSCREEN_COMPOSED")) == 1;
        g_fs_buf_short = !getenv("RECOMP_FULLSCREEN_COMPOSED") || atoi(getenv("RECOMP_FULLSCREEN_COMPOSED")) == 2;
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
            sd.Width = g_vw;   /* step 90: the window shows the render size */
            sd.Height = g_vh;
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
    {   /* step 92 */
        int ao = twoc_ao_parse(getenv("RECOMP_AO"));
        const char *e;
        if ((e = getenv("RECOMP_AO_RADIUS")) && atof(e) > 0)
            g_ao_radius = (float)atof(e);
        g_ao_half = (e = getenv("RECOMP_AO_HALF")) && atoi(e) > 0;
        g_ao_debug = (e = getenv("RECOMP_AO_DEBUG")) ? atoi(e) : 0;
        if ((e = getenv("RECOMP_AO_DENOISE")) && atoi(e) > 0)
            g_ao_denoise = atoi(e);
        if (ao != TWOC_AO_OFF)
            set_ao(ao, twoc_ao_strength_parse(getenv("RECOMP_AO_STRENGTH")));
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
    if (getenv("RECOMP_VULKAN")) {      /* step 86: the Vulkan probe (vk_probe.h), on the D3D11 device's GPU */
        IDXGIDevice *dd = NULL;
        IDXGIAdapter *ad = NULL;
        DXGI_ADAPTER_DESC desc;
        int have = 0;
        if (SUCCEEDED(ID3D11Device_QueryInterface(d3d8_GetD3D11Device(), &IID_IDXGIDevice, (void **)&dd))) {
            if (SUCCEEDED(IDXGIDevice_GetAdapter(dd, &ad))) {
                have = SUCCEEDED(IDXGIAdapter_GetDesc(ad, &desc));
                IDXGIAdapter_Release(ad);
            }
            IDXGIDevice_Release(dd);
        }
        vkp_start(have ? &desc.AdapterLuid : NULL);
    }
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

    REPLAY_SIDE();
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

    REPLAY_SIDE();
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

/* Step 92: a D24S8 depth surface (`samples` a pixel) that a shader can also
 * read, for ambient occlusion: the texture typeless, xbox_d3d8's surface
 * wrapper given its depth view as ms_surface does, and *srv the depth as
 * R24 UNORM. Made only while ambient occlusion is on; the depth drawn into
 * it is the same as in a plain one. */
static IDirect3DSurface8 *ao_depth_surface(unsigned w, unsigned h, unsigned samples, ID3D11ShaderResourceView **srv)
{
    ID3D11Device *d = d3d8_GetD3D11Device();
    D3D11_TEXTURE2D_DESC td;
    D3D11_DEPTH_STENCIL_VIEW_DESC dv;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv;
    ID3D11Texture2D *tex = NULL;
    IDirect3DSurface8 *s;
    D3D8Surface *sf;
    HRESULT hr = E_OUTOFMEMORY;

    REPLAY_SIDE();
    *srv = NULL;
    memset(&td, 0, sizeof(td));
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R24G8_TYPELESS;
    td.SampleDesc.Count = samples ? samples : 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(ID3D11Device_CreateTexture2D(d, &td, NULL, &tex)))
        return NULL;
    s = xbox_d3d8_surface_wrap(tex, w, h, D3DFMT_A8R8G8B8);  /* a colour format: no view made */
    sf = (D3D8Surface *)s;
    if (sf) {
        memset(&dv, 0, sizeof(dv));
        dv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dv.ViewDimension = td.SampleDesc.Count > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
        hr = ID3D11Device_CreateDepthStencilView(d, (ID3D11Resource *)tex, &dv, &sf->dsv);
        sf->format = D3DFMT_D24S8;
        sf->usage = D3DUSAGE_DEPTHSTENCIL;
    }
    if (SUCCEEDED(hr)) {
        memset(&sv, 0, sizeof(sv));
        sv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        if (td.SampleDesc.Count > 1) {
            sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
        } else {
            sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            sv.Texture2D.MipLevels = 1;
        }
        hr = ID3D11Device_CreateShaderResourceView(d, (ID3D11Resource *)tex, &sv, srv);
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

    REPLAY_SIDE();
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
    REPLAY_SIDE();
    g_dev->lpVtbl->SetRenderTarget(g_dev, ms, ms_depth);
    g_dev->lpVtbl->Clear(g_dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
    if (aa_copy(((D3D8Surface *)ms)->rtv, ((D3D8Texture *)tex)->srv, w, h) == 0)
        g_st.ms_fills++;
}

/* Where the last draw or clear went: its surface now holds more than its
 * texture (MSAA only). Called with g_lock held. */
static void target_drawn(void)
{
    REPLAY_SIDE();
    if (g_cur_target == -1 && g_bb.ms)
        g_bb.dirty = 1;
    else if (g_cur_target >= 0 && g_rt[g_cur_target].ms)
        g_rt[g_cur_target].dirty = 1;
}

/* Render target i's texture brought up to date with its multisampled
 * surface. Called with g_lock held. */
static void rt_resolve(int i)
{
    REPLAY_SIDE();
    if (g_rt[i].ms && g_rt[i].dirty) {
        ms_resolve(g_rt[i].tex, g_rt[i].ms);
        g_rt[i].dirty = 0;
    }
}

/* Drop render target i's multisampled surfaces, its texture kept up to date. */
static void rt_ms_release(int i)
{
    REPLAY_SIDE();
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

    REPLAY_SIDE();
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

    REPLAY_SIDE();
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
    gt_set(i, 0);                       /* step 78 */
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

    REPLAY_SIDE();
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
    gt_set(free_i, t->data);            /* step 78 */
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

    REPLAY_SIDE();
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
enum { RC_DRAW, RC_CLEAR, RC_COPY, RC_FREED, RC_AO };     /* step 92: RC_AO, ambient occlusion here */
/* Step 92: what an ambient-occlusion pass is given from the frame: the
 * world's projection and the fog its opaque draws had. */
typedef struct {
    float proj[16];
    float fog_start, fog_end, fog_density;
    int fog_on;                         /* FOGTABLEMODE (D3DFOG_*: 3 linear), 0 no fog */
} ao_mark_t;
/* A recorded draw's stage: fetched at the replay (0, a texture that may be a
 * render target there), held from the recording (PIN_HELD), or (step 79,
 * with the render thread) the render target if there is one at the replay,
 * else held from the recording: the fetch at the replay read guest memory,
 * which the render thread must not while the game runs. */
enum { PIN_FETCH, PIN_HELD, PIN_TARGET };
typedef struct {
    int kind;
    union {
        struct {
            d3dh_draw_t d;
            IDirect3DBaseTexture8 *pin[4];
            uint8_t pinned[4];
            int16_t tslot[4];           /* step 78: pinned stages' entries in the cache (g_tc), -1 none */
            uint8_t cam, cls;           /* step 61: its camera (IC_NO_CAM: none), IC_* */
            uint8_t n_cpos, cpos[4];    /* step 61: program registers holding the camera's position */
            int32_t obj;                /* step 62: its object (g_iobj), -1 none */
            D3D8UpKept kept;            /* step 76: where xbox_d3d8's rings hold its vertices and indices */
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
        ao_mark_t ao;                   /* step 92 */
    } u;
} rcmd_t;

#define RA_CHUNK (1u << 20)
typedef struct ra_chunk {
    struct ra_chunk *next;
    size_t used, cap;
    double data[1];                     /* aligned storage, `cap` bytes */
} ra_chunk_t;

/* Step 78: the live settings queue ("Step 78: live settings, queued", below). */
static struct {
    SRWLOCK lock;
    twoc_settings_t s;                  /* the latest asked for */
    unsigned gen;                       /* changes queued so far */
    struct {
        twoc_settings_t was;            /* what it applied last */
        unsigned gen;                   /* ... up to this change */
    } side[2];                          /* [0] the game side, [1] the host side */
} g_sq = { SRWLOCK_INIT };

/* Step 78 (notes 18.81): a recorded frame lives in a slot, the way a render
 * thread will take it over: the game side records into one (g_rs) while the
 * replay side draws another (g_ps), handed over at the guest's Swap
 * (slot_handover). On one thread, as now, the slot recorded is handed over
 * and replayed inside the same Swap, so the other is always free again
 * before the next frame starts. */
/* Step 78: a texture upload recorded in a frame: entry `slot` of the cache
 * made again from `desc` (its bytes and palette copied into the arena), before
 * stage `stage` of draw command `at` is pinned. */
typedef struct {
    unsigned at;
    uint16_t slot, stage;
    d3dh_tex_t desc;
} texup_t;

typedef struct {
    rcmd_t *rc;                         /* the frame's commands */
    unsigned rc_n, rc_cap;
    ra_chunk_t *ra_head, *ra_cur;       /* its arena */
    size_t ra_bytes;                    /* allocated this frame */
    uint32_t targets[64];               /* Data of targets drawn, cleared or copied into this frame */
    unsigned targets_n;
    int overflow;                       /* more than 64: every texture fetched at the replay */
    texup_t *up;                        /* its texture uploads, in order */
    unsigned up_n, up_cap;
    unsigned seq;                       /* the frame recorded in it (handovers so far) */
    unsigned set_gen;                   /* the game side's settings it was recorded under (g_sq) */
    /* What the replay side's frame plan reads from the game side (replay_frame) */
    unsigned swap_no;                   /* the guest's Swap that handed it over */
    double t_s;                         /* ... and its time (the dumps' log lines) */
    int rec;                            /* frames were being recorded (REC_ON) */
    int interp;                         /* ... for interpolation (g_interp): the frame-rate setting, as taken */
    double game_ms;                     /* the game side's time on it: from its last Swap's return to this one */
    int it;                             /* its interpolation tables (g_itab), -1 none */
    /* Step 79: with the render thread */
    int state;                          /* SL_*: whose it is (g_ho) */
    int hold;                           /* the overlay holds the game at this frame (the game waits in its Swap) */
    twoc_settings_t set;                /* the game side's settings it was recorded under (set_gen), for the */
    unsigned set_copy;                  /* host side's part: applied before it is replayed; the gen copied */
} fslot_t;
#define FSLOTS 2
static fslot_t g_slot[FSLOTS];
static fslot_t *g_rs = &g_slot[0];      /* the game side records into this one */
static fslot_t *g_ps;                   /* the replay side draws this one (NULL: none handed over) */
static unsigned g_slot_seq;             /* handovers so far */
/* Step 79: the hand-over between the game thread and the render thread.
 * Under g_ho: the slots' states, the frame handed over and not yet taken,
 * whether the render thread is busy, the last frame whose hold is over, and
 * the interpolation table set it keeps as the frame before (g_ip, so the
 * game can pick the free one). Order: g_rec, then g_lock, then g_ho. */
enum { SL_FREE, SL_REC, SL_PEND, SL_REPLAY };
static CRITICAL_SECTION g_ho;
static CONDITION_VARIABLE g_ho_rt;      /* the render thread waits on it for a frame */
static CONDITION_VARIABLE g_ho_game;    /* the game thread waits on it for a slot, the hold, the drain */
static fslot_t *g_pend;                 /* handed over, not yet taken */
static int g_rt_busy;                   /* the render thread has a frame (taken, its presents not all done) */
static unsigned g_hold_done;            /* the seq of the last frame the render thread let the game go after */
static fslot_t *g_rprev;                /* game side: the slot handed over last (its targets: rc_maybe_target) */
static LARGE_INTEGER g_rt_take;         /* render thread: when it took the frame it replays (busy from there) */
static void rt_let_go(const fslot_t *f);
/* The slots share their memory: a slot whose frame has been replayed gives
 * its arena chunks and its command array back (slot_free), and the slot
 * recorded into next takes them first. On one thread every frame is then
 * recorded into the memory the replay before it just read, as it was with
 * one arena: with two arenas used in turn, each frame recorded into memory
 * last touched two frames before, and recording and the first pass cost
 * 0.25 ms more each in cast_bug (cache misses over ~18 MB). */
static ra_chunk_t *g_ra_pool;           /* free chunks, the last replayed frame's first */
static rcmd_t *g_rc_pool;               /* a free command array */
static unsigned g_rc_pool_cap;
static SRWLOCK g_pool_lock = SRWLOCK_INIT;

static void clear_exec(const d3dh_target_t *target, uint32_t xdk_flags, uint32_t color, float z, uint32_t stencil);
static int copy_exec(const d3dh_target_t *dst, const int32_t *rects, const int32_t *points, unsigned n);
static void freed_exec(uint32_t data, uint32_t bytes);
static void ic_record(rcmd_t *c);       /* step 61 */

/* `n` bytes from the recording slot's arena (16-aligned; NULL if out of
 * memory). */
static void *ra_alloc(size_t n)
{
    fslot_t *f = g_rs;
    ra_chunk_t *c = f->ra_cur;

    GAME_SIDE();
    n = (n + 15) & ~(size_t)15;
    if (c && c->used + n <= c->cap) {
        void *p = (uint8_t *)c->data + c->used;
        c->used += n;
        f->ra_bytes += n;
        return p;
    }
    /* The next kept chunk if it is large enough, else a new one after this. */
    if (c && c->next && c->next->cap >= n) {
        c = c->next;
        c->used = 0;
    } else if (!c && f->ra_head && f->ra_head->cap >= n) {
        c = f->ra_head;
        c->used = 0;
    } else {
        size_t cap = n > RA_CHUNK ? n : RA_CHUNK;
        ra_chunk_t *nc = NULL, **pp;
        AcquireSRWLockExclusive(&g_pool_lock);  /* step 78: a chunk the last replay gave back */
        for (pp = &g_ra_pool; *pp; pp = &(*pp)->next)
            if ((*pp)->cap >= n) {
                nc = *pp;
                *pp = nc->next;
                break;
            }
        ReleaseSRWLockExclusive(&g_pool_lock);
        if (!nc) {
            nc = (ra_chunk_t *)malloc(sizeof(ra_chunk_t) + cap);
            if (!nc)
                return NULL;
            nc->cap = cap;
        }
        nc->used = 0;
        if (c) {
            nc->next = c->next;
            c->next = nc;
        } else {
            nc->next = f->ra_head;
            f->ra_head = nc;
        }
        c = nc;
    }
    f->ra_cur = c;
    c->used = n;
    f->ra_bytes += n;
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

/* Step 84 (notes 18.87): blocks d3dh_rec_alloc gave the bridge for its next
 * draw (a program draw's constants and vertices, built in the arena), which
 * the recording keeps where they are (ra_keep) instead of copying them.
 * Forgotten at each draw recorded and whenever the arena starts again. */
#define RK_MAX 4
static void *g_rk_p[RK_MAX];
static size_t g_rk_n[RK_MAX];
static unsigned g_rk_next;

static void rk_forget(void)
{
    memset(g_rk_p, 0, sizeof(g_rk_p));
}

void *d3dh_rec_alloc(size_t n)
{
    void *p = NULL;
    int side;

    if (!g_ready || !n)
        return NULL;
    rec_lock(0);
    side = side_set(SIDE_GAME);
    if (REC_ON() && (p = ra_alloc(n)) != NULL) {
        g_rk_p[g_rk_next % RK_MAX] = p;
        g_rk_n[g_rk_next % RK_MAX] = n;
        g_rk_next++;
    }
    side_set(side);
    rec_unlock();
    return p;
}

void d3dh_hold_release(d3dh_hold_t *h)
{
    if (h && InterlockedDecrement((volatile LONG *)&h->refs) == 0)
        free(h);
}

/* Step 84: commands [from, n) of slot f dropped: the vertex blocks their
 * draws hold, released. */
static void rc_unhold(fslot_t *f, unsigned from, unsigned n)
{
    unsigned i;

    for (i = from; i < n; i++)
        if (f->rc[i].kind == RC_DRAW && f->rc[i].u.draw.d.hold) {
            d3dh_hold_release(f->rc[i].u.draw.d.hold);
            f->rc[i].u.draw.d.hold = NULL;
        }
}

/* `p` as the recording keeps it: a block d3dh_rec_alloc gave for this draw
 * as it is, anything else copied into the arena (ra_dup). */
static void *ra_keep(const void *p, size_t n)
{
    unsigned k;

    if (!p || !n)
        return NULL;
    for (k = 0; k < RK_MAX; k++)
        if (g_rk_p[k] == p && n <= g_rk_n[k]) {
            g_rk_p[k] = NULL;
            return (void *)p;
        }
    return ra_dup(p, n);
}

static rcmd_t *rc_push(int kind)
{
    fslot_t *f = g_rs;

    GAME_SIDE();
    if (f->rc_n == f->rc_cap) {
        unsigned cap = f->rc_cap ? 2 * f->rc_cap : 1024;
        rcmd_t *n = (rcmd_t *)realloc(f->rc, cap * sizeof(rcmd_t));
        if (!n)
            return NULL;
        f->rc = n;
        f->rc_cap = cap;
    }
    memset(&f->rc[f->rc_n], 0, sizeof(rcmd_t));
    f->rc[f->rc_n].kind = kind;
    return &f->rc[f->rc_n++];
}

static void rc_target(uint32_t data)
{
    fslot_t *f = g_rs;
    unsigned i;

    GAME_SIDE();
    if (!data)
        return;
    for (i = 0; i < f->targets_n; i++)
        if (f->targets[i] == data)
            return;
    if (f->targets_n == sizeof(f->targets) / sizeof(f->targets[0]))
        f->overflow = 1;
    else
        f->targets[f->targets_n++] = data;
}

/* Whether a texture with this Data may sample a host render target at the
 * replay: one exists now, or the frame draws, clears or copies into one.
 * Step 78: "exists" from the game side's copy of the targets' Data
 * (g_gt_data, kept by the replay side), and a frame handed over but not yet
 * replayed counts with its targets (never, on one thread). Step 79: with the
 * render thread, the frame handed over last (g_rprev), replayed or not: its
 * targets are the game side's until it records into that slot again. */
static int rc_maybe_target(uint32_t data)
{
    fslot_t *f = g_rs, *p = g_thr ? g_rprev : g_ps;
    unsigned i;
    int r = 0;

    GAME_SIDE();
    if (f->overflow)
        return 1;
    for (i = 0; i < RT_SLOTS && !r; i++)
        r = (uint32_t)g_gt_data[i] == data;
    if (r)
        return 1;
    for (i = 0; i < f->targets_n; i++)
        if (f->targets[i] == data)
            return 1;
    if (p && p != f) {
        if (p->overflow)
            return 1;
        for (i = 0; i < p->targets_n; i++)
            if (p->targets[i] == data)
                return 1;
    }
    return 0;
}

/* Called with g_lock held. */
static void freed_exec(uint32_t data, uint32_t bytes)
{
    int i;

    REPLAY_SIDE();
    for (i = 0; i < RT_SLOTS; i++)
        if (g_rt[i].tex && g_rt[i].data >= data && g_rt[i].data - data < bytes) {
            rt_release(i);
            g_st.rt_released++;
        }
}

void d3dh_memory_freed(uint32_t data, uint32_t bytes)
{
    int side;

    if (!g_ready)
        return;
    rec_lock(0);
    side = side_set(SIDE_GAME);         /* step 78 */
    if (REC_ON() && !g_replay_check) {  /* step 60: in its place among the frame's commands */
        rcmd_t *c = rc_push(RC_FREED);
        if (c) {
            c->u.freed.data = data;
            c->u.freed.bytes = bytes;
        } else {
            side_set(SIDE_NOW);
            freed_exec(data, bytes);
        }
    } else {
        side_set(SIDE_NOW);
        freed_exec(data, bytes);
    }
    side_set(side);
    rec_unlock();
}

/* ── Clear ─────────────────────────────────────────────────── */

void d3dh_clear(const d3dh_target_t *target, uint32_t xdk_flags, uint32_t color, float z, uint32_t stencil)
{
    int side;

    if (!g_ready)
        return;
    rec_lock(0);
    side = side_set(SIDE_GAME);         /* step 78 */
    if (REC_ON()) {                     /* step 60 */
        rcmd_t *c = rc_push(RC_CLEAR);
        if (g_replay_check) {
            side_set(SIDE_NOW);
            clear_exec(target, xdk_flags, color, z, stencil);
            side_set(SIDE_GAME);
        }
        if (c) {
            c->u.clear.t = *target;
            c->u.clear.flags = xdk_flags;
            c->u.clear.color = color;
            c->u.clear.z = z;
            c->u.clear.stencil = stencil;
            rc_target(target->data);
        }
    } else {
        side_set(SIDE_NOW);
        clear_exec(target, xdk_flags, color, z, stencil);
    }
    side_set(side);
    rec_unlock();
}

/* Called with g_lock held. */
static void clear_exec(const d3dh_target_t *target, uint32_t xdk_flags, uint32_t color, float z, uint32_t stencil)
{
    DWORD pc = 0;

    REPLAY_SIDE();
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
 * least recently used entry makes room when the table is full.
 *
 * Step 78 (notes 18.81): the cache in two halves, the way a render thread
 * will need it. The game side keeps the table of what the host holds (each
 * entry's description, whether it is held, when it was last used:
 * tex_lookup) and decides at record time, exactly as the cache did, whether
 * a draw's texture is there or has to be uploaded, and into which entry. On
 * a miss it copies the guest bytes (and a P8 palette) into the frame's arena
 * and records the upload (texup_t, tex_snap); the replay side makes and
 * fills the host textures from those copies before the frame's first pass
 * (slot_prepare), in the order the draws asked for them, and pins each
 * draw's textures there. So a recorded frame's textures never read guest
 * memory on the replay side, and a texture the guest rewrites later in the
 * frame still draws as it was, as before. A draw made at once (frames not
 * recorded) looks up and uploads in one go (tex_get). */
#define TEX_CACHE_SLOTS 1024

typedef struct {
    d3dh_tex_t desc;                    /* bits and palette unused */
    unsigned last_use;
    int live;                           /* the host holds it (or will, once its recorded upload is made) */
} gtex_t;

typedef struct {
    gtex_t g[TEX_CACHE_SLOTS];          /* game side: what the host holds */
    unsigned clock;
    IDirect3DBaseTexture8 *h[TEX_CACHE_SLOTS];  /* replay side: an IDirect3DTexture8, or an IDirect3DCubeTexture8 */
    volatile LONG bad[TEX_CACHE_SLOTS];         /* replay side to game side: that upload failed, the entry is empty */
    volatile LONG nbad;
} tex_cache_t;
static tex_cache_t g_tc;                /* the cache */
static tex_cache_t g_tcc;               /* the old path apart: REPLAY_CHECK's draws made at once, TEXCHECK */
static tex_cache_t *g_tc_now = &g_tc;   /* the cache a draw made at once uses */

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

/* Replay side: entry i's host texture released. */
static void tex_release(tex_cache_t *tc, unsigned i)
{
    int s;

    REPLAY_SIDE();
    if (!tc->h[i])
        return;
    for (s = 0; s < 4; s++)             /* xbox_d3d8 keeps the bound pointer */
        g_dev->lpVtbl->SetTexture(g_dev, s, NULL);
    tc->h[i]->lpVtbl->Release(tc->h[i]);
    tc->h[i] = NULL;
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

    REPLAY_SIDE();
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

/* Game side: the entry for `t` in `tc`, used now. Returns its index, with
 * *up set when it has to be (re)made and filled from `t` (the table already
 * holds `t`), or -1 when it can't be: a P8 texture without its palette, no
 * texture, as before. */
static int tex_lookup(tex_cache_t *tc, const d3dh_tex_t *t, int *up)
{
    gtex_t *e = NULL, *oldest = NULL;
    uint32_t i, h = (t->key >> 4) * 2654435761u;

    GAME_SIDE();
    *up = 0;
    if (tc->nbad && InterlockedExchange(&tc->nbad, 0))     /* uploads the replay side couldn't make */
        for (i = 0; i < TEX_CACHE_SLOTS; i++)
            if (tc->bad[i] && InterlockedExchange(&tc->bad[i], 0))
                tc->g[i].live = 0;
    tc->clock++;
    for (i = 0; i < TEX_CACHE_SLOTS; i++) {
        gtex_t *s = &tc->g[(h + i) & (TEX_CACHE_SLOTS - 1)];
        /* Step 52: a header and its level count. A stage with MIPFILTER
         * NONE gets level 0 alone (d3d_bridge.c), so one header bound to
         * two stages that filter differently is two host textures; keyed
         * by the header alone, each stage replaced the other's every draw
         * (Bamboozled's ball rings: its highlight map on stages 1 and 2). */
        if (s->live && s->desc.key == t->key && s->desc.levels == t->levels) {
            e = s;
            break;
        }
        if (!s->live) {
            if (!e)
                e = s;
            break;
        }
        if (!oldest || s->last_use < oldest->last_use)
            oldest = s;
    }
    if (!e) {                           /* full along this probe run: evict */
        e = oldest;
        e->live = 0;
        g_stg.tex_evictions++;
    }
    if (e->live && (e->desc.data != t->data || e->desc.format != t->format || e->desc.width != t->width ||
                    e->desc.height != t->height || e->desc.levels != t->levels || e->desc.pitch != t->pitch ||
                    e->desc.sig != t->sig || e->desc.pal_sig != t->pal_sig || e->desc.cube != t->cube))
        e->live = 0;
    if (!e->live) {
        if (t->format == XDK_FMT_P8 && (!t->palette || !t->pal_entries))
            return -1;
        e->desc = *t;
        e->desc.bits = NULL;
        e->desc.palette = NULL;
        e->live = 1;
        *up = 1;
    }
    e->last_use = tc->clock;
    return (int)(e - tc->g);
}

/* Replay side: entry i made again and filled from `t` (its bytes in the
 * arena for a recorded frame, in guest memory for a draw made at once).
 * Returns the texture, or NULL (the entry empty; the game side is told). */
static IDirect3DBaseTexture8 *tex_make(tex_cache_t *tc, unsigned i, const d3dh_tex_t *t)
{
    IDirect3DBaseTexture8 *tex = NULL;
    uint32_t host_fmt = t->format == XDK_FMT_P8 ? XDK_FMT_A8R8G8B8 : t->format;
    HRESULT hr;

    REPLAY_SIDE();
    tex_release(tc, i);
    if (t->cube)
        hr = g_dev->lpVtbl->CreateCubeTexture(g_dev, t->width, t->levels, 0, (D3DFORMAT)host_fmt,
                                              D3DPOOL_MANAGED, (IDirect3DCubeTexture8 **)&tex);
    else
        hr = g_dev->lpVtbl->CreateTexture(g_dev, t->width, t->height, t->levels, 0, (D3DFORMAT)host_fmt,
                                          D3DPOOL_MANAGED, (IDirect3DTexture8 **)&tex);
    if (FAILED(hr) || !t->bits || tex_upload(tex, t) != 0) {
        if (tex)
            tex->lpVtbl->Release(tex);
        if (++g_st.tex_upload_fails <= 5)
            fprintf(stderr, "[D3DB] host texture upload failed: header %08X format %02X %ux%u levels %u%s\n",
                    t->key, t->format, t->width, t->height, t->levels, t->cube ? " cube" : "");
        InterlockedExchange(&tc->bad[i], 1);
        InterlockedExchange(&tc->nbad, 1);
        return NULL;
    }
    tc->h[i] = tex;
    g_st.tex_uploads++;
    if (t->format == XDK_FMT_P8)
        g_st.p8_uploads++;
    if (t->cube)
        g_st.cube_uploads++;
    return tex;
}

/* The host render target with the texture's Data, NULL if none. */
static IDirect3DBaseTexture8 *tex_target(const d3dh_tex_t *t)
{
    int i;

    REPLAY_SIDE();
    for (i = 0; i < RT_SLOTS; i++)      /* S4: what the host rendered, not guest memory */
        if (g_rt[i].tex && g_rt[i].data == t->data) {
            g_rt[i].last_use = ++g_rt_clock;
            g_st.rt_sampled++;
            if (g_cur_target == i)
                g_st.rt_feedback++;     /* D3D11 unbinds the view: the stage samples black */
            rt_resolve(i);              /* step 58: what the multisampled surface holds (with MSAA, even while bound) */
            return (IDirect3DBaseTexture8 *)g_rt[i].tex;
        }
    return NULL;
}

/* A draw's texture now (a draw made at once, or fetched at its replay):
 * the host render target with its Data, else from the cache `tc`, made and
 * filled from guest memory now if it has to be. */
static IDirect3DBaseTexture8 *tex_get(tex_cache_t *tc, const d3dh_tex_t *t)
{
    IDirect3DBaseTexture8 *rt;
    int i, up;

    REPLAY_SIDE();
    if ((rt = tex_target(t)) != NULL)
        return rt;
    if (g_ps) {                         /* step 78: a recorded draw's, fetched at its replay as a render target */
        if (++g_st.tex_at_replay <= 5)  /* that isn't one there: guest memory read at the replay, as before */
            fprintf(stderr, "[D3DB] host texture %08X (Data %08X) fetched at the replay, not a render target there:"
                    " uploaded from guest memory now\n", t->key, t->data);
    }
    i = tex_lookup(tc, t, &up);
    if (i < 0)
        return NULL;
    return up ? tex_make(tc, (unsigned)i, t) : tc->h[i];
}

/* Game side, recording: entry i's upload from `t`, its bytes copied into
 * the frame's arena, before stage `stage` of the draw being recorded is
 * pinned. Returns 0, or -1 (out of memory: the entry left empty). */
static int tex_snap(unsigned i, unsigned stage, const d3dh_tex_t *t)
{
    fslot_t *f = g_rs;
    texup_t *u;

    GAME_SIDE();
    if (f->up_n == f->up_cap) {
        unsigned cap = f->up_cap ? 2 * f->up_cap : 256;
        texup_t *n = (texup_t *)realloc(f->up, cap * sizeof(texup_t));
        if (!n)
            return -1;
        f->up = n;
        f->up_cap = cap;
    }
    u = &f->up[f->up_n];
    u->at = f->rc_n - 1;
    u->slot = (uint16_t)i;
    u->stage = (uint16_t)stage;
    u->desc = *t;
    u->desc.bits = (const uint8_t *)ra_dup(t->bits, t->bytes);
    u->desc.palette = t->palette ? (const uint32_t *)ra_dup(t->palette, 4 * (size_t)t->pal_entries) : NULL;
    if (!u->desc.bits || (t->palette && !u->desc.palette))
        return -1;
    f->up_n++;
    g_stg.tex_snaps++;
    g_stg.tex_snap_bytes += (double)t->bytes;
    return 0;
}

/* Replay side, before the first pass over commands [from, rc_n) of slot
 * `f`: their recorded uploads made, in order, and each draw's textures
 * pinned where it was recorded (an upload before its stage's pin). */
static void slot_prepare(fslot_t *f, unsigned from)
{
    unsigned i, s, u = 0;

    REPLAY_SIDE();
    while (u < f->up_n && f->up[u].at < from)
        u++;
    for (i = from; i < f->rc_n; i++) {
        rcmd_t *c = &f->rc[i];
        for (s = 0; s < 4; s++) {
            for (; u < f->up_n && f->up[u].at == i && f->up[u].stage == s; u++)
                tex_make(&g_tc, f->up[u].slot, &f->up[u].desc);
            if (c->kind == RC_DRAW && c->u.draw.pinned[s] && c->u.draw.tslot[s] >= 0) {
                IDirect3DBaseTexture8 *t = g_tc.h[c->u.draw.tslot[s]];
                c->u.draw.pin[s] = t;
                if (t) {
                    t->lpVtbl->AddRef(t);
                    g_st.replay_pins++;
                }
            }
        }
    }
    f->up_n = 0;
}

void d3dh_dump_range(unsigned from, unsigned to)
{
    g_dump_from = from;
    g_dump_to = to;
}

/* Game side (step 78): the table's entries that hold it no longer match,
 * so their next draw uploads them again. */
void d3dh_texture_touch(uint32_t data)
{
    uint32_t i;
    int k;

    if (!g_ready)
        return;
    rec_lock(0);
    GAME_SIDE();
    for (k = 0; k < 2; k++) {
        tex_cache_t *tc = k ? &g_tcc : &g_tc;
        for (i = 0; i < TEX_CACHE_SLOTS; i++)
            if (tc->g[i].live && data >= tc->g[i].desc.data && data - tc->g[i].desc.data < tc->g[i].desc.bytes)
                tc->g[i].desc.sig ^= 0x80000000u;   /* no longer matches: re-upload at its next draw */
    }
    rec_unlock();
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

    REPLAY_SIDE();
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

    REPLAY_SIDE();
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
 * xbox_d3d8 then uploads at its draw.
 *
 * Step 78 (notes 18.81): the table in two halves, the way a render thread
 * will need it. The game side parses a program the first time it sees it
 * (vs_slot: the registers it reads, which the bridge's translation and the
 * recording need) and never changes an entry after; the replay side creates
 * its host shader at the program's first draw there (vs_make, g_vsh). */
#define VS_SLOTS 64
static struct {
    uint32_t key;
    uint16_t inputs;
    uint32_t cmask[6];                  /* step 60: constant registers read directly, bit n = cn */
    int rel_base;                       /* lowest base read relative to a0.x, -1 none */
    int rel_bones;                      /* step 62: reads relative to a0.x from c124 (XDK c28): bones */
    uint8_t cpos_n, cpos[192];          /* step 84: ic_record's candidates for the camera's position */
} g_vs[VS_SLOTS];                       /* game side; an entry is fixed once counted in g_vs_n */
static volatile LONG g_vs_n;
static struct {
    DWORD handle;                       /* 0: couldn't be created */
    int made;                           /* created, or tried */
} g_vsh[VS_SLOTS];                      /* replay side, by g_vs's index */
static float g_vs_consts[192 * 4];
static int g_vs_consts_valid;

/* Step 60: the constant registers a parsed program reads: each source
 * operand an instruction's units use (MOV and ARL read A; ADD A and C; MAD
 * A, B and C; the other MAC ops A and B; the ILU C) that selects a constant
 * reads the instruction's constant index, or, with a0.x relative, some
 * register from that index on (its base kept in `rel_base`, the lowest;
 * `bones` set when one base is c124, step 62). */
static void vs_const_reads(const NV2AVshProgram *pg, uint32_t mask[6], int *rel_base, int *bones)
{
    int i, s;

    memset(mask, 0, 6 * sizeof(uint32_t));
    *rel_base = -1;
    *bones = 0;
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
                if (src->reg_index == 124)
                    *bones = 1;
            } else {
                mask[src->reg_index >> 5] |= 1u << (src->reg_index & 31);
            }
        }
    }
}

/* A program's entry, -1 if it hasn't been parsed. */
static int vs_find(uint32_t key)
{
    unsigned i, n = (unsigned)g_vs_n;

    for (i = 0; i < n; i++)
        if (g_vs[i].key == key)
            return (int)i;
    return -1;
}

/* Game side: the program's entry, parsed the first time. Called with
 * g_lock held. */
static int vs_slot(const d3dh_vprog_t *p)
{
    static NV2AVshProgram parsed;       /* ~30 KB: not on the stack */
    int f = vs_find(p->key);
    unsigned i = (unsigned)g_vs_n;

    GAME_SIDE();
    if (f >= 0)
        return f;
    if (i == VS_SLOTS)
        return -1;
    g_vs[i].key = p->key;
    d3d8_vsh_parse((const DWORD *)p->code, (int)p->n_insns, &parsed);
    g_vs[i].inputs = parsed.inputs_read;
    vs_const_reads(&parsed, g_vs[i].cmask, &g_vs[i].rel_base, &g_vs[i].rel_bones);
    {                                   /* step 84: the registers it reads, in order, but the transform c96-c99 */
        int r;
        g_vs[i].cpos_n = 0;
        for (r = 0; r < 192; r++)
            if (((g_vs[i].cmask[r >> 5] >> (r & 31) & 1) || (g_vs[i].rel_base >= 0 && r >= g_vs[i].rel_base)) &&
                !(r >= 96 && r < 100))
                g_vs[i].cpos[g_vs[i].cpos_n++] = (uint8_t)r;
    }
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
            fprintf(stderr, "%d%s\n", g_vs[i].rel_base, g_vs[i].rel_bones && g_vs[i].rel_base != 124 ? " and c124" : "");
    }
    MemoryBarrier();                    /* the entry whole before it is counted */
    InterlockedIncrement(&g_vs_n);
    return (int)i;
}

/* Replay side: entry i's host shader, created from `p`'s microcode at its
 * first draw. Returns its handle, 0 if it couldn't be. */
static DWORD vs_make(int i, const d3dh_vprog_t *p)
{
    REPLAY_SIDE();
    if (!g_vsh[i].made) {
        g_vsh[i].made = 1;
        if (FAILED(d3d8_vsh_create_shader((const DWORD *)p->code, (int)p->n_insns, &g_vsh[i].handle))) {
            g_vsh[i].handle = 0;
            g_st.vs_fails++;
        } else {
            g_st.vs_created++;
        }
    }
    return g_vsh[i].handle;
}

uint16_t d3dh_vs_inputs(const d3dh_vprog_t *p)
{
    int i, side;
    uint16_t r;

    if (!g_ready)
        return 0;
    rec_lock(0);
    side = side_set(SIDE_GAME);         /* step 78 */
    i = vs_slot(p);
    side_set(side);
    r = i < 0 ? 0 : g_vs[i].inputs;
    rec_unlock();
    return r;
}

int d3dh_vs_consts(const d3dh_vprog_t *p, uint32_t mask[6])
{
    int i, r = -1, side;

    memset(mask, 0, 6 * sizeof(uint32_t));
    if (!g_ready)
        return -1;
    rec_lock(0);
    side = side_set(SIDE_GAME);         /* step 78 */
    i = vs_slot(p);
    side_set(side);
    if (i >= 0) {
        memcpy(mask, g_vs[i].cmask, 6 * sizeof(uint32_t));
        r = g_vs[i].rel_base;
    }
    rec_unlock();
    return r;
}

/* Does register k differ from the last upload? */
static int vs_const_differs(const float *consts, int k)
{
    const uint64_t *a = (const uint64_t *)&g_vs_consts[4 * k], *b = (const uint64_t *)&consts[4 * k];
    return a[0] != b[0] || a[1] != b[1];
}

/* Bind the program and upload the constants that changed. Step 76 (notes
 * 18.79): only the registers the program reads are compared: those its
 * microcode reads directly (step 60's masks) and c58/c59, which xbox_d3d8's
 * translation reads to undo the viewport mapping; one it doesn't read may
 * keep a value another program left. A program that reads relative to a0.x
 * (a0 may be negative) has all 192 compared. The span uploaded, lowest to
 * highest that differs, is copied whole, so g_vs_consts stays what xbox_d3d8
 * holds. Returns 0, or -1 if the host has no shader for it. Called with
 * g_lock held. */
static int vs_bind(const d3dh_vprog_t *p)
{
    int i = vs_find(p->key), lo = -1, hi = -1, k, w;
    DWORD h;

    REPLAY_SIDE();
    if (i < 0 && !t_rt)                 /* step 78: one the game side never parsed (a probe's): at once */
        i = vs_slot(p);                 /* (step 79: never on the render thread: the game side's table) */
    if (i < 0 || !(h = vs_make(i, p)))
        return -1;
    if (!g_vs_consts_valid) {
        lo = 0;
        hi = 191;
    } else {
        int all = g_vs[i].rel_base >= 0;
        for (w = 0; w < 6; w++) {
            uint32_t m = all ? 0xFFFFFFFFu : g_vs[i].cmask[w];
            if (w == 58 / 32)                           /* c58, c59: the translation's viewport */
                m |= 3u << (58 % 32);
            while (m) {
                unsigned long b;
                _BitScanForward(&b, m);
                m &= m - 1;
                k = 32 * w + (int)b;
                if (vs_const_differs(p->consts, k)) {
                    if (lo < 0)
                        lo = k;
                    hi = k;
                }
            }
        }
    }
    if (lo >= 0) {
        memcpy(&g_vs_consts[4 * lo], &p->consts[4 * lo], 16 * (size_t)(hi - lo + 1));
        g_dev->lpVtbl->SetVertexShaderConstant(g_dev, lo, &p->consts[4 * lo], (DWORD)(hi - lo + 1));
        g_vs_consts_valid = 1;
        g_st.vs_const_uploads++;
    }
    g_dev->lpVtbl->SetVertexShader(g_dev, h);
    return 0;
}

/* A draw on the host, now: the body of d3dh_draw_up (step 60: also each
 * recorded draw at its replay). `pinned[s]` set: stage s takes `pin[s]`, the
 * texture fetched when the draw was recorded, instead of a fetch now.
 * `kept` (step 76, a recorded draw's): its vertices and indices are uploaded
 * at its first pass and drawn from there by the next ones, while xbox_d3d8's
 * rings still hold them. Called with g_lock held. */
static int draw_exec(const d3dh_draw_t *d, IDirect3DBaseTexture8 *const *pin, const uint8_t *pinned,
                     D3D8UpKept *kept)
{
    D3DVIEWPORT8 vp;
    xmap_t map;
    HRESULT hr;
    unsigned i, s;
    const void *verts;
    int lit = 0;
    LARGE_INTEGER t0, t1;

    REPLAY_SIDE();
    if (d->hud_dx != 0.0f) {            /* step 65: a HUD element at its screen edge: x_clip += dx w */
        static float k4[192 * 4];
        d3dh_draw_t m = *d;
        d3dh_ff_t ff;
        d3dh_vprog_t vp;
        int r;
        if (d->ff) {
            ff = *d->ff;
            for (r = 0; r < 4; r++)
                ff.proj[4 * r] += d->hud_dx * ff.proj[4 * r + 3];
            m.ff = &ff;
        }
        if (d->vprog) {                 /* c96 is x_clip's row of (W V P) transposed, c99 w's */
            vp = *d->vprog;
            memcpy(k4, d->vprog->consts, sizeof(k4));
            for (r = 0; r < 4; r++)
                k4[4 * 96 + r] += d->hud_dx * k4[4 * 99 + r];
            vp.consts = k4;
            m.vprog = &vp;
        }
        m.hud_dx = 0.0f;
        return draw_exec(&m, pin, pinned, kept);
    }
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
            if (!d->tex[s])
                bound[s] = NULL;
            else if (pinned && pinned[s] == PIN_TARGET) {   /* step 79: the target, else the record-time copy */
                bound[s] = tex_target(d->tex[s]);
                if (!bound[s] && pin[s] && ++g_st.tex_at_copy <= 5)
                    fprintf(stderr, "[D3DB] host texture %08X (Data %08X) not a render target at the replay: drawn"
                            " from its copy made when it was recorded\n", d->tex[s]->key, d->tex[s]->data);
                if (!bound[s])
                    bound[s] = pin[s];
            } else
                bound[s] = pinned && pinned[s] ? pin[s] : tex_get(g_tc_now, d->tex[s]);
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
                /* Step 66: and so does the game's own projection of 3D (a
                 * display-list flush's, not the overlay's): the warp portal's
                 * lightning, pillarboxed, sat a quarter of its distance from
                 * the middle off its emitters. */
                int full = d->world_list || rhw_full_width(d->verts, d->vert_count, d->stride);
                if (full)
                    g_st.rhw_full++;
                else
                    g_st.rhw_pillar++;
                target_map(&d->target, !full, &map);
            }
            verts = rescale_rhw_map(d->verts, d->vert_count, d->stride, &vp, &map);
        }
    }
    if (verts != d->verts)              /* made for this pass (rescale_rhw): uploaded each time */
        kept = NULL;
    if (d->indices) {                   /* xbox_d3d8 uploads vert_count vertices from `verts` */
        hr = kept ? d3d8_DrawIndexedPrimitiveUPKept((D3DPRIMITIVETYPE)d->prim, d->vert_count, d->prim_count,
                                                    d->indices, D3DFMT_INDEX16, verts, d->stride, kept)
                  : g_dev->lpVtbl->DrawIndexedPrimitiveUP(g_dev, (D3DPRIMITIVETYPE)d->prim, 0, d->vert_count,
                                                          d->prim_count, d->indices, D3DFMT_INDEX16, verts,
                                                          d->stride);
        g_st.indexed_draws++;
    } else {
        hr = kept ? d3d8_DrawPrimitiveUPKept((D3DPRIMITIVETYPE)d->prim, d->prim_count, verts, d->stride, kept)
                  : g_dev->lpVtbl->DrawPrimitiveUP(g_dev, (D3DPRIMITIVETYPE)d->prim, d->prim_count, verts, d->stride);
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

/* ── Step 87: baked draws (notes 18.90) ───────────────────────
 *
 * A frame drawn more than once (its sub-frames with Frame rate above 60, or
 * RECOMP_D3D_BRIDGE_REPLAY=N) had every pass translate every draw again: its
 * render and stage states, the fixed-function matrices and lights, the pixel
 * shader and its constants set one by one through xbox_d3d8, which then
 * worked out the D3D11 objects, samplers and constants from them at the draw
 * (combiner state parsed and looked up, blend / depth / raster and sampler
 * descriptions built and compared, the fixed-function constants built). Now
 * the first pass draws as before and each draw is captured as it is made
 * (xbox_d3d8's D3D8Baked: the shaders, input layout, blend, depth-stencil and
 * rasterizer objects, the stencil reference, the samplers, the viewport, the
 * constant buffers bound and the fixed-function and pixel constants as
 * uploaded, where the rings hold its vertices and indices), and the passes
 * after it draw from the capture: the target bound and the textures fetched
 * as before (a render target is made, resolved and bound by its draw), the
 * program's constants given as the pass has them (camera, world, bones
 * interpolated; the rest as recorded), a fixed-function draw's matrices given
 * again when the pass moves them (or the capture was made at moved ones),
 * and only the objects that differ from what is bound set. xbox_d3d8 puts a
 * captured program's 192 registers into a ring of their own (D3D11.1
 * constant-buffer offsets), so a later pass binds the capture's block where
 * the registers weren't moved and writes only the draws whose were; and its
 * vertex, index and constant rings start over at a capturing frame's start
 * when the frame may not fit (one that started over in the middle took the
 * frame's captures before it with it: ~6% of later passes' draws in
 * cast_bug drawn the ordinary way). A draw whose
 * capture no longer holds (a cache let an object go, a ring started over,
 * nothing captured) is drawn the ordinary way. The same pixels: every bound
 * object and constant the shaders read is the one the ordinary way gives.
 * RECOMP_D3D_BRIDGE_BAKE=0 off, 1 on; unset: on (step 87's checks: REPLAY_CHECK
 * with baked passes and the sub-frame check found no differing frame; the
 * 25-level sweep in check mode none but in western, whose count the check
 * itself gives with the baked draws off too: notes 18.90).
 *
 * The baked form is what a second backend would take too: a pipeline (its
 * key: vertex shader, input layout, pixel shader, blend, depth-stencil and
 * rasterizer descriptions, topology, the target's format) with its dynamic
 * state (viewport, stencil reference), its resources (four textures and
 * samplers), its constants (the program's 192 registers, which the
 * interpolation rewrites in part, or the fixed-function blocks, and the
 * pixel block) and its vertex and index ranges (notes 18.90). */
typedef struct {
    D3D8Baked b;
    uint8_t moved;                      /* its constants or matrices were moved when it was captured (a */
                                        /* sub-frame before t = 1): a pass at others gives them again */
} bake_t;
static bake_t *g_bk;                    /* replay side: the frame's, by command */
static unsigned g_bk_cap;
enum { BK_OFF, BK_CAPTURE, BK_BAKED };
static int g_bk_mode;                   /* this pass's */
static unsigned g_bk_plan, g_bk_pass;   /* the frame's passes, those done */

/* A recorded draw drawn from its capture: `d` as this pass has it (a copy
 * with moved matrices or constants when `moved`). Called with g_lock held. */
static int draw_baked(const rcmd_t *c, const d3dh_draw_t *d, int moved)
{
    static float k4[192 * 4];
    const bake_t *bk = &g_bk[c - g_ps->rc];
    const uint8_t *pinned = c->u.draw.pinned;
    IDirect3DBaseTexture8 *const *pin = c->u.draw.pin;
    ID3D11ShaderResourceView *srv[4];
    const D3DMATRIX *m3[3], *const *ffm = NULL;
    const float *k = NULL;
    d3dh_ff_t ff;
    LARGE_INTEGER t0, t1;
    unsigned s;
    int r;

    REPLAY_SIDE();
    if (!bk->b.gen)
        goto plain;
    QueryPerformanceCounter(&t0);
    if (bind_target(&d->target) != 0)
        return -1;
    for (s = 0; s < 4; s++) {           /* as draw_exec fetches them */
        IDirect3DBaseTexture8 *t;
        if (!d->tex[s])
            t = NULL;
        else if (pinned[s] == PIN_TARGET) {
            t = tex_target(d->tex[s]);
            if (!t)
                t = pin[s];
        } else
            t = pinned[s] ? pin[s] : tex_get(g_tc_now, d->tex[s]);
        srv[s] = t ? d3d8_base_srv(t) : NULL;
        if (t && !srv[s])               /* SetTexture leaves the stage's view as it was: not baked */
            goto plain;
    }
    if (d->vprog) {
        k = d->vprog->consts;
        if (d->hud_dx != 0.0f) {        /* step 65: as draw_exec moves it */
            memcpy(k4, k, sizeof(k4));
            for (s = 0; s < 4; s++)
                k4[4 * 96 + s] += d->hud_dx * k4[4 * 99 + s];
            k = k4;
            moved = 1;
        }
        if (bk->moved)                  /* the ring's block holds the first sub-frame's registers */
            moved = 1;
    } else if (d->ff && (moved || bk->moved)) {
        ff = *d->ff;
        if (d->hud_dx != 0.0f)
            for (s = 0; s < 4; s++)
                ff.proj[4 * s] += d->hud_dx * ff.proj[4 * s + 3];
        m3[0] = (const D3DMATRIX *)ff.world;
        m3[1] = (const D3DMATRIX *)ff.view;
        m3[2] = (const D3DMATRIX *)ff.proj;
        ffm = m3;
    }
    if (d3d8_DrawBaked(&bk->b, srv, k, moved, ffm) != S_OK)
        goto plain;
    target_drawn();
    g_st.draws++;
    g_st.baked_draws++;
    if (d->indices)
        g_st.indexed_draws++;
    if (d->vprog)
        g_st.vp_draws++;
    if (d->target.data)
        g_st.rt_draws++;
    g_frame_work = 1;
    QueryPerformanceCounter(&t1);
    g_st.draw_ms += (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)g_qpf.QuadPart;
    return 0;
plain:
    g_st.bake_plain++;
    r = draw_exec(d, pin, pinned, (D3D8UpKept *)&c->u.draw.kept);
    return r;
}

/* A recorded draw in this pass: drawn the ordinary way (captured, at a
 * frame's first pass) or from its capture. `moved`: `d` is a copy whose
 * matrices or constants the pass moved. */
static int draw_cmd(const rcmd_t *c, const d3dh_draw_t *d, int moved)
{
    int r;

    if (g_bk_mode == BK_BAKED)
        return draw_baked(c, d, moved);
    if (g_bk_mode == BK_OFF)
        return draw_exec(d, c->u.draw.pin, c->u.draw.pinned, (D3D8UpKept *)&c->u.draw.kept);
    g_bk[c - g_ps->rc].moved = (uint8_t)moved;
    d3d8_BakeCapture(&g_bk[c - g_ps->rc].b);
    r = draw_exec(d, c->u.draw.pin, c->u.draw.pinned, (D3D8UpKept *)&c->u.draw.kept);
    d3d8_BakeCapture(NULL);
    if (g_bk[c - g_ps->rc].b.gen)
        g_st.bake_captured++;
    return r;
}

/* Record a draw. Called with g_lock held. Returns 0, or -1 (out of memory:
 * the draw is lost, counted as a failure). */
static int rec_draw(const d3dh_draw_t *d)
{
    rcmd_t *c = rc_push(RC_DRAW);
    d3dh_draw_t *o;
    unsigned s;
    int bad = 0;

    GAME_SIDE();
    if (!c) {
        g_stg.draw_fails++;
        return -1;
    }
    o = &c->u.draw.d;
    *o = *d;
    o->hold = NULL;
    if (d->hold && d->verts)            /* step 84: the bridge's cached vertices, by reference */
        o->verts = d->verts;
    else
        o->verts = ra_keep(d->verts, (size_t)d->vert_count * d->stride);     /* step 84: ra_keep */
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
            v->consts = (const float *)ra_keep(d->vprog->consts, 192 * 16);
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
    rk_forget();                        /* step 84: this draw's blocks are its own now */
    if (bad) {
        g_rs->rc_n--;
        g_stg.draw_fails++;
        return -1;
    }
    if (d->hold && d->verts) {
        InterlockedIncrement((volatile LONG *)&d->hold->refs);
        o->hold = d->hold;
    }
    /* The textures as they are now; those that may be render targets at the
     * replay are fetched there. Step 78: looked up in the game side's table
     * in the order the old path fetched them, so the cache sees the same
     * sequence; a miss copies the guest bytes for the replay side's upload,
     * and the replay side pins them (slot_prepare). */
    for (s = 0; s < 4; s++) {
        int i, up;
        c->u.draw.tslot[s] = -1;
        if (!o->tex[s])
            continue;
        if (!rc_maybe_target(o->tex[s]->data))
            c->u.draw.pinned[s] = PIN_HELD;
        else if (g_thr)                 /* step 79: the target at the replay, else a copy made now (not guest */
            c->u.draw.pinned[s] = PIN_TARGET;   /* memory read on the render thread) */
        else
            continue;
        i = tex_lookup(&g_tc, o->tex[s], &up);
        if (i < 0)
            continue;
        if (up && tex_snap((unsigned)i, s, o->tex[s]) != 0) {
            g_tc.g[i].live = 0;         /* out of memory: drawn without it */
            if (++g_stg.tex_upload_fails <= 5)
                fprintf(stderr, "[D3DB] host texture %08X not copied for its upload (out of memory)\n",
                        o->tex[s]->key);
            continue;
        }
        c->u.draw.tslot[s] = (int16_t)i;
    }
    rc_target(d->target.data);
    c->u.draw.cam = 0xFF;
    c->u.draw.obj = -1;
    if (g_interp) {                     /* step 61 */
        o->camera = d->camera ? (const float *)ra_dup(d->camera, 32 * sizeof(float)) : NULL;
        ic_record(c);
    }
    return 0;
}

static void ic_draw(const rcmd_t *c);   /* step 61 */
static void ao_exec(const ao_mark_t *m);    /* step 92 */
static HRESULT bb_rebuild(unsigned w, unsigned h);    /* step 92 */
static ID3D11Texture2D *back_tex(void); /* step 87, the replay check's later passes */
static void replay_check_snap(int restore, ID3D11Texture2D *bb);

/* One pass over this frame's commands: as recorded, or (step 61) with
 * `interp` set, the draws under this sub-frame's cameras. Called with g_lock
 * held, at the present. */
static void replay_pass(int interp)
{
    LARGE_INTEGER t0, t1;
    unsigned i;

    REPLAY_SIDE();
    QueryPerformanceCounter(&t0);
    g_bk_mode = BK_OFF;                 /* step 87: the first of passes captured, the rest drawn from that */
    if (g_bake && g_bk_plan > 1) {
        if (g_bk_pass == 0) {
            if (g_bk_cap < g_ps->rc_n) {
                bake_t *n = (bake_t *)realloc(g_bk, (size_t)g_ps->rc_n * sizeof(bake_t));
                if (n) {
                    g_bk = n;
                    g_bk_cap = g_ps->rc_n;
                }
            }
            if (g_bk_cap >= g_ps->rc_n) {
                d3d8_BakeReset();
                g_bk_mode = BK_CAPTURE;
            }
        } else if (g_bk_cap >= g_ps->rc_n) {
            g_bk_mode = BK_BAKED;
        }
    }
    for (i = 0; i < g_ps->rc_n; i++) {
        const rcmd_t *c = &g_ps->rc[i];
        switch (c->kind) {
        case RC_DRAW:
            if (interp)
                ic_draw(c);
            else
                draw_cmd(c, &c->u.draw.d, 0);
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
        case RC_AO:                     /* step 92: each pass, so each sub-frame under its own camera */
            ao_exec(&c->u.ao);
            break;
        }
    }
    g_bk_mode = BK_OFF;
    g_bk_pass++;
    QueryPerformanceCounter(&t1);
    g_st.replay_ms += (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)g_qpf.QuadPart;
    g_rp_ms[interp != 0] += (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)g_qpf.QuadPart;    /* step 74 */
    g_rp_cmds[interp != 0] += g_ps->rc_n;
    g_rp_n[interp != 0]++;
}

/* Step 78: slot f's frame replayed: its chunks and command array given back
 * for the slot recorded next, the slot free. */
static void slot_free(fslot_t *f)
{
    rcmd_t *rc;
    unsigned cap;

    REPLAY_SIDE();
    AcquireSRWLockExclusive(&g_pool_lock);
    if (f->ra_head) {
        ra_chunk_t *t = f->ra_head;
        while (t->next)
            t = t->next;
        t->next = g_ra_pool;
        g_ra_pool = f->ra_head;
        f->ra_head = f->ra_cur = NULL;
    }
    rc = f->rc;
    cap = f->rc_cap;
    f->rc = g_rc_pool;
    f->rc_cap = g_rc_pool_cap;
    g_rc_pool = rc;
    g_rc_pool_cap = cap;
    ReleaseSRWLockExclusive(&g_pool_lock);
    if (g_ps == f)
        g_ps = NULL;
    if (t_rt) {                         /* step 79: the game may record into it */
        EnterCriticalSection(&g_ho);
        f->state = SL_FREE;
        LeaveCriticalSection(&g_ho);
        WakeAllConditionVariable(&g_ho_game);
    }
}

/* The frame's commands are done with: release what they held, forget them. */
static void replay_done(void)
{
    unsigned i, s;

    REPLAY_SIDE();
    for (i = 0; i < g_ps->rc_n; i++)
        if (g_ps->rc[i].kind == RC_DRAW)
            for (s = 0; s < 4; s++)
                if (g_ps->rc[i].u.draw.pin[s])
                    g_ps->rc[i].u.draw.pin[s]->lpVtbl->Release(g_ps->rc[i].u.draw.pin[s]);
    rc_unhold(g_ps, 0, g_ps->rc_n);     /* step 84 */
    g_bk_plan = g_bk_pass = 0;          /* step 87: its captures go with it */
    g_st.replay_frames++;
    g_st.replay_cmds += g_ps->rc_n;
    g_st.replay_bytes += (double)g_ps->ra_bytes;
    g_ps->rc_n = 0;
    g_ps->ra_cur = NULL;
    g_ps->ra_bytes = 0;
    if (!t_rt) {                        /* step 79: with the render thread the game still reads them (g_rprev) */
        g_ps->targets_n = 0;            /* until it records into the slot again, and empties them then */
        g_ps->overflow = 0;
    }
    g_ps->up_n = 0;
    slot_free(g_ps);                    /* step 78: the slot free for the game side again */
}

/* Run this frame's commands `g_replay` times (once when only interpolation
 * records), then forget them. Called with g_lock held, at the present. */
static void replay_run(void)
{
    int p;

    REPLAY_SIDE();
    if (!g_ps)
        return;
    if (!g_ps->rc_n && !g_ps->ra_bytes) {
        slot_free(g_ps);
        return;
    }
    for (p = 0; p < (g_replay > 1 ? g_replay : 1); p++) {
        if (p && g_replay_check) {      /* step 87: the check's later passes start where the frame did too */
            ID3D11Texture2D *bb = back_tex();
            if (bb) {
                replay_check_snap(1, bb);
                ID3D11Texture2D_Release(bb);
            }
        }
        replay_pass(0);
    }
    replay_done();
}

/* ── Step 61: frame interpolation, camera only (notes 18.64) ─────
 *
 * Every draw carries the game's camera as it was made (d3d_bridge.c
 * host_draw): its world matrix Cam (rows right, up, forward, position;
 * 0x242620) and its projection P (0x242398). A frame's distinct cameras go in
 * a table, and each draw is classed when it is recorded:
 *   - IC_FF: a fixed-function 3D draw into the screen whose view is the
 *     camera's inverse (V Cam = I); IC_FF_PROJ when its projection is P too.
 *   - IC_VP: a program draw into the screen that reads c96-c99, which hold
 *     (W V P) transposed (XDK c0-c3), whose world W = (WVP) P^-1 Cam is affine:
 *     it was made with this camera. The registers it reads that hold the
 *     camera's position are noted: among those it reads directly, or from
 *     its a0.x-relative base on (Drain Damage's Wa-Wa programs, c103).
 *     Only one equal to the camera's position (x, y, z) counts, so a
 *     table entry doesn't match by chance.
 *   - IC_NONE: the rest (pre-transformed 2D, render-target passes with their
 *     own camera, copies and clears are replayed as recorded).
 * At the present each camera of the frame that 3D draws used is paired with
 * one of the frame before's: an identical one (it didn't move: nothing to
 * do), else, most used first, the nearest one left. The frame's main camera
 * (the one most draws used)
 * decides whether to interpolate: a step over 2 units or a turn over 30
 * degrees, or under half the frame's draws matched by kind in the frame
 * before (program or FVF, pixel shader, textures, target, sizes), is a cut,
 * and the frame is shown k times as it is. Otherwise sub-frame s of k draws
 * at t = s/k with Cam(t) (position lerped, rotation slerped, row lengths
 * lerped) and P(t) (lerped): a program draw's c96-c99 become
 * (WVP P^-1 Cam Cam(t)^-1 P(t))^T and its camera-position registers Cam(t)'s
 * position; a fixed-function draw's view becomes V Cam Cam(t)^-1 and its
 * projection P(t) where it was P. The last sub-frame (t = 1) is the frame as
 * recorded. Objects move between their two places too (step 62, below).
 *
 * The k presents go at DXGI interval 1 from inside the game's Swap, so the
 * game still runs 59.94 updates a second. They go as soon as the queue has
 * room, not each held to its own mid-refresh (pace), and the queue is 4
 * buffers and 4 frames deep meanwhile (ic_queue): the game's own work for
 * its next frame then overlaps the sub-frames already queued (16.7 ms at
 * most, where the game's interval-4 frames queued up to 33 ms).
 *
 * Checks (none needed to play): RECOMP_D3D_BRIDGE_SUBDUMP=A-B, =@FILE (the
 * next N swaps once FILE holding N appears; "framerate 0|1" in it switches
 * the setting as the overlay does) or =cuts (each held frame and the 2
 * after) writes every sub-frame as <dump>subSSSSS_sofk.bmp;
 * RECOMP_D3D_BRIDGE_INTERP_T0=1 draws sub-frame 1 at t = 0 (this frame's
 * draws under the frame before's camera: still scenery must match the frame
 * before); RECOMP_D3D_BRIDGE_INTERP_REC=FILE appends N frames' draws as
 * interpolation sees them once FILE.go holding N appears;
 * RECOMP_D3D_BRIDGE_INTERP_CUT=units,degrees,fraction sets the cut rule,
 * RECOMP_D3D_BRIDGE_INTERP_OBJECTS=units the object snap (0: camera only),
 * RECOMP_D3D_BRIDGE_INTERP_QUEUE=buffers,latency the queue and
 * RECOMP_D3D_BRIDGE_INTERP_MID=1 holds sub-frames to mid-refresh again. */
enum { IC_NONE, IC_VP, IC_FF, IC_FF_PROJ };
#define IC_CAMS 16
#define IC_NO_CAM 0xFF
typedef struct {
    float m[32];                        /* Cam, then P, as the guest had them */
    double inv_p[16];
    double q[4], len[3];                /* Cam's rotation and row lengths, when rot_ok */
    int p_ok, rot_ok;
    unsigned draws;                     /* draws classed with it */
} icam_t;
/* Step 78 (notes 18.81): three sets of these tables, used in turn, as a
 * render thread will need them: the frame being recorded (g_ir), the frame
 * being replayed (g_ic) and the frame before it (g_ip). Handing a frame
 * over (slot_handover) makes its set the replayed one and the third set,
 * free since the frame before last was replayed, the recorded one. */
#define ITABS 3
static icam_t g_icam[ITABS][IC_CAMS];
static unsigned g_icam_n[ITABS];
static int g_ir = 0, g_ic = 1, g_ip = 2;    /* recorded, replayed, the frame before (g_iprev_ok) */
static int g_iprev_ok;                  /* the frame before was recorded for this */
static uint32_t *g_ikey[ITABS];         /* each draw's kind, for matching (the frame before's sorted) */
static unsigned g_ikey_n[ITABS], g_ikey_cap[ITABS];
static int g_ipair[IC_CAMS];            /* this frame's camera -> the frame before's, -1 none */
static struct {
    int on;
    float c[16], vfix[16], dp[16], pos[3];
} g_isub[IC_CAMS];                      /* this sub-frame's corrections, by camera */
static double g_it;                     /* this sub-frame's t */
static unsigned g_imc = IC_NO_CAM;      /* step 65: the frame's main camera (most 3D draws); only its draws move */

/* Step 62: objects. A 3D draw interpolated with its camera (IC_VP, IC_FF*)
 * whose mesh the bridge knows (d3dh_draw_t.mesh) is an object: its key (its
 * kind, a few render states and the mesh), its world W (row vectors: a
 * program draw's factored (WVP) P^-1 Cam, a fixed-function draw's world
 * matrix) and, for a program that reads relative to a0.x from c124 (XDK
 * c28, the skinned meshes), the bones there: 3 registers each, the rows of a
 * 3x4 world-space matrix (a bone with a register the program also reads
 * directly is not one: left alone). At the present each object is paired with the
 * frame before's nearest unpaired one of its key (by W's origin plus the
 * first bone's); one further than g_isnap (1.5 units: a teleport, a respawn;
 * step 68: and as far off where its own last step would have taken it)
 * or with none stays where this frame has it (camera-only, as in stage 2).
 * A paired object that moved is drawn at W(t) and its bones at B(t), each
 * interpolated as a rotation (slerp), its axes' lengths and its origin
 * (lerp), or element by element when it isn't a rotation and scale; a bone
 * that stepped further than 4 g_isnap is left as it is (a limb swings
 * further than an object steps; a register the program doesn't read may
 * hold anything). */
#define IC_BONE_BASE 124
#define IC_BONE_REGS 66                 /* 22 bones */
typedef struct {
    uint32_t key;
    int32_t prev;                       /* its partner in the frame before (index there), -1 none */
    int32_t bones;                      /* its bones' offset in g_ibone[], -1 none */
    int32_t grp;                        /* step 66: its skeleton (g_igrp), -1 none: bones one by one */
    uint32_t used;                      /* (in the frame before) taken by a partner */
    uint32_t bmask;                     /* the bones that may move (bit b: c124 + 3b) */
    uint32_t own;                       /* step 67: bmask holds only the bones uploaded for it */
    uint32_t kind, mesh;                /* step 68: its key's parts (the pair log) */
    uint32_t lkey;                      /* step 68: its key with its textures by their shape only */
    int32_t hv;                         /* step 68: vel known (it had a partner) */
    double vel[3];                      /* step 68: its step from its partner (its skeleton's), the motion to expect next */
    int32_t lbest;                      /* step 68, the pair log: its partner, the decision, the step, off its motion, */
    const char *lwhat;                  /* the nearest of its key */
    float lstep, loff, lnear;
    double pos[3];                      /* W's origin, plus its first bone's */
    double w[16];
} iobj_t;
static iobj_t *g_iobj[ITABS];
static unsigned g_iobj_n[ITABS], g_iobj_cap[ITABS];
static float *g_ibone[ITABS];           /* IC_BONE_REGS float4s an object */
static unsigned g_ibone_n[ITABS], g_ibone_cap[ITABS];
static unsigned *g_iord;                /* the frame before's objects, by key */
static unsigned g_iord_cap;
static const iobj_t *g_iord_of;

static void m4_mul(const double *a, const double *b, double *o)
{
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            o[4 * i + j] = a[4 * i] * b[j] + a[4 * i + 1] * b[4 + j] + a[4 * i + 2] * b[8 + j] + a[4 * i + 3] * b[12 + j];
}

static int m4_inv(const double *m, double *o)
{
    double a[4][8];
    int i, j, k, p;

    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            a[i][j] = m[4 * i + j];
            a[i][4 + j] = i == j;
        }
    for (k = 0; k < 4; k++) {
        p = k;
        for (i = k + 1; i < 4; i++)
            if (fabs(a[i][k]) > fabs(a[p][k]))
                p = i;
        if (fabs(a[p][k]) < 1e-12)
            return -1;
        if (p != k)
            for (j = 0; j < 8; j++) {
                double t = a[k][j];
                a[k][j] = a[p][j];
                a[p][j] = t;
            }
        for (j = 7; j >= k; j--)
            a[k][j] /= a[k][k];
        for (i = 0; i < 4; i++)
            if (i != k && a[i][k] != 0.0) {
                double f = a[i][k];
                for (j = k; j < 8; j++)
                    a[i][j] -= f * a[k][j];
            }
    }
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            o[4 * i + j] = a[i][4 + j];
    return 0;
}

static void m4_from(const float *f, double *d)
{
    int i;
    for (i = 0; i < 16; i++)
        d[i] = f[i];
}

/* A 3x3 rotation (rows) as a quaternion (x, y, z, w), and back. */
static void q_from_m3(const double *m, double *q)
{
    double tr = m[0] + m[4] + m[8], s;

    if (tr > 0) {
        s = sqrt(tr + 1.0) * 2;
        q[3] = 0.25 * s; q[0] = (m[7] - m[5]) / s; q[1] = (m[2] - m[6]) / s; q[2] = (m[3] - m[1]) / s;
    } else if (m[0] > m[4] && m[0] > m[8]) {
        s = sqrt(1.0 + m[0] - m[4] - m[8]) * 2;
        q[3] = (m[7] - m[5]) / s; q[0] = 0.25 * s; q[1] = (m[1] + m[3]) / s; q[2] = (m[2] + m[6]) / s;
    } else if (m[4] > m[8]) {
        s = sqrt(1.0 + m[4] - m[0] - m[8]) * 2;
        q[3] = (m[2] - m[6]) / s; q[0] = (m[1] + m[3]) / s; q[1] = 0.25 * s; q[2] = (m[5] + m[7]) / s;
    } else {
        s = sqrt(1.0 + m[8] - m[0] - m[4]) * 2;
        q[3] = (m[3] - m[1]) / s; q[0] = (m[2] + m[6]) / s; q[1] = (m[5] + m[7]) / s; q[2] = 0.25 * s;
    }
}

static void m3_from_q(const double *q, double *m)
{
    double x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = 1 - 2 * (y * y + z * z); m[1] = 2 * (x * y - z * w);     m[2] = 2 * (x * z + y * w);
    m[3] = 2 * (x * y + z * w);     m[4] = 1 - 2 * (x * x + z * z); m[5] = 2 * (y * z - x * w);
    m[6] = 2 * (x * z - y * w);     m[7] = 2 * (y * z + x * w);     m[8] = 1 - 2 * (x * x + y * y);
}

static void q_slerp(const double *qa0, const double *qb, double t, double *q)
{
    double qa[4], dot, th, sa, sb, n;
    int j;

    memcpy(qa, qa0, sizeof(qa));
    dot = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
    if (dot < 0) {
        for (j = 0; j < 4; j++)
            qa[j] = -qa[j];
        dot = -dot;
    }
    if (dot > 0.9995) {
        sa = 1 - t;
        sb = t;
    } else {
        th = acos(dot);
        sa = sin((1 - t) * th) / sin(th);
        sb = sin(t * th) / sin(th);
    }
    for (j = 0; j < 4; j++)
        q[j] = sa * qa[j] + sb * qb[j];
    n = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (j = 0; j < 4; j++)
        q[j] /= n;
}

/* Step 62: an affine transform as its axes (rows of r, with their lengths)
 * and its origin t. */
typedef struct {
    double r[9], t[3];
} aff_t;

/* Its rotation and axis lengths, when its axes are orthogonal and
 * right-handed; 0 if not. */
static int aff_rot(const aff_t *a, double *q, double *len)
{
    double r[9], det;
    int i, j;

    for (i = 0; i < 3; i++) {
        double l = sqrt(a->r[3 * i] * a->r[3 * i] + a->r[3 * i + 1] * a->r[3 * i + 1] + a->r[3 * i + 2] * a->r[3 * i + 2]);
        if (l < 1e-6)
            return 0;
        len[i] = l;
        for (j = 0; j < 3; j++)
            r[3 * i + j] = a->r[3 * i + j] / l;
    }
    for (i = 0; i < 3; i++)
        for (j = i + 1; j < 3; j++)
            if (fabs(r[3 * i] * r[3 * j] + r[3 * i + 1] * r[3 * j + 1] + r[3 * i + 2] * r[3 * j + 2]) > 1e-2)
                return 0;
    det = r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) + r[2] * (r[3] * r[7] - r[4] * r[6]);
    if (det < 0.98)
        return 0;
    q_from_m3(r, q);
    return 1;
}

static void aff_lerp(const aff_t *a, const aff_t *b, double t, aff_t *o)
{
    double qa[4], qb[4], la[3], lb[3], q[4], r[9];
    int i, j;

    if (aff_rot(a, qa, la) && aff_rot(b, qb, lb)) {
        q_slerp(qa, qb, t, q);
        m3_from_q(q, r);
        for (i = 0; i < 3; i++)
            for (j = 0; j < 3; j++)
                o->r[3 * i + j] = r[3 * i + j] * (la[i] + (lb[i] - la[i]) * t);
    } else {
        for (i = 0; i < 9; i++)
            o->r[i] = a->r[i] + (b->r[i] - a->r[i]) * t;
    }
    for (i = 0; i < 3; i++)
        o->t[i] = a->t[i] + (b->t[i] - a->t[i]) * t;
}

/* A camera's derived values: P^-1, and its rotation and row lengths when
 * its rows are orthogonal and right-handed (else it is lerped whole). */
static void icam_prep(icam_t *c)
{
    double p[16], r[9], det;
    int i, j;

    m4_from(c->m + 16, p);
    c->p_ok = m4_inv(p, c->inv_p) == 0;
    c->rot_ok = 0;
    for (i = 0; i < 3; i++) {
        double l = sqrt((double)c->m[4 * i] * c->m[4 * i] + (double)c->m[4 * i + 1] * c->m[4 * i + 1] +
                        (double)c->m[4 * i + 2] * c->m[4 * i + 2]);
        if (l < 1e-6)
            return;
        c->len[i] = l;
        for (j = 0; j < 3; j++)
            r[3 * i + j] = c->m[4 * i + j] / l;
    }
    for (i = 0; i < 3; i++)
        for (j = i + 1; j < 3; j++)
            if (fabs(r[3 * i] * r[3 * j] + r[3 * i + 1] * r[3 * j + 1] + r[3 * i + 2] * r[3 * j + 2]) > 1e-2)
                return;
    det = r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) + r[2] * (r[3] * r[7] - r[4] * r[6]);
    if (det < 0.98)
        return;
    q_from_m3(r, c->q);
    c->rot_ok = 1;
}

static uint32_t fnv(uint32_t h, uint32_t v)
{
    int i;
    for (i = 0; i < 4; i++, v >>= 8)
        h = (h ^ (v & 0xFF)) * 16777619u;
    return h;
}

/* A draw's kind, for matching it with the frame before's. `loose`: its
 * textures by their shape (format, size, levels) instead of which they are
 * (step 68: a flipbook's frames are a texture each). */
static uint32_t ic_key(const d3dh_draw_t *d, int loose)
{
    /* Step 84 (notes 18.87): the pixel shader's part, up to 56 states byte
     * by byte (~0.3 us a draw, twice a draw), kept for the last two shaders
     * met with the hash before them: the same value, from memory. Game side
     * only. */
    static struct {
        int ok;
        uint32_t h0, h1;
        unsigned n;
        d3dh_state_t rs[56];
    } memo[2];
    static unsigned next;
    uint32_t h = 2166136261u;
    unsigned s, i;

    GAME_SIDE();
    h = fnv(h, d->vprog ? d->vprog->key : d->fvf | 0x80000000u);
    if (d->ps && d->ps->token) {
        unsigned n = d->ps->n_rs < 56 ? d->ps->n_rs : 56, k;
        for (k = 0; k < 2; k++)
            if (memo[k].ok && memo[k].h0 == h && memo[k].n == d->ps->n_rs &&
                memcmp(memo[k].rs, d->ps->rs, n * sizeof(d3dh_state_t)) == 0)
                break;
        if (k < 2) {
            h = memo[k].h1;
        } else {
            k = next++ & 1;
            memo[k].ok = d->ps->n_rs <= 56;
            memo[k].h0 = h;
            memo[k].n = d->ps->n_rs;
            memcpy(memo[k].rs, d->ps->rs, n * sizeof(d3dh_state_t));
            for (i = 0; i < d->ps->n_rs; i++)
                h = fnv(fnv(h, d->ps->rs[i].state), d->ps->rs[i].value);
            memo[k].h1 = h;
        }
    }
    for (s = 0; s < 4; s++)
        if (!loose || !d->tex[s])
            h = fnv(h, d->tex[s] ? d->tex[s]->key : 0);
        else
            h = fnv(fnv(fnv(h, d->tex[s]->format | d->tex[s]->levels << 8 | d->tex[s]->cube << 12),
                        d->tex[s]->width), d->tex[s]->height);
    h = fnv(h, d->target.data);
    h = fnv(h, d->prim);
    h = fnv(h, d->vert_count);
    return fnv(h, d->index_count);
}

/* Step 63: a 3x3's handedness: 1 right, -1 mirrored, 0 flat (Crash's
 * stencil shadow flattens his bones onto the ground: a determinant of +0 or
 * -0 from frame to frame). */
static int ic_hand(const double *m)
{
    double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                 m[2] * (m[3] * m[7] - m[4] * m[6]), l = 1;
    int i;

    for (i = 0; i < 3; i++)
        l *= sqrt(m[3 * i] * m[3 * i] + m[3 * i + 1] * m[3 * i + 1] + m[3 * i + 2] * m[3 * i + 2]);
    return det > 1e-3 * l ? 1 : det < -1e-3 * l ? -1 : 0;
}

/* Step 62: a draw's object, with its world `w` and its program constants
 * when it has bones (else NULL). */
static void ic_obj(rcmd_t *c, uint32_t key, const double *w, const float *k4, const uint32_t *cmask,
                   const uint32_t *fresh)
{
    /* The render states kept in the key (below): 14, 19, 20, 22, 52, 55, 56, 168 (ZWRITE, blend, CULL, STENCIL*,
     * COLORWRITE). */
    const d3dh_draw_t *d = &c->u.draw.d;
    unsigned cur = (unsigned)g_ir, i, j;
    uint32_t lk;
    iobj_t *o;

    GAME_SIDE();
    if (!d->mesh || g_isnap <= 0)
        return;
    if (g_iobj_n[cur] == g_iobj_cap[cur]) {
        unsigned cap = g_iobj_cap[cur] ? 2 * g_iobj_cap[cur] : 1024;
        iobj_t *n = (iobj_t *)realloc(g_iobj[cur], cap * sizeof(iobj_t));
        if (!n)
            return;
        g_iobj[cur] = n;
        g_iobj_cap[cur] = cap;
    }
    if (k4 && g_ibone_n[cur] + IC_BONE_REGS * 4 > g_ibone_cap[cur]) {
        unsigned cap = g_ibone_cap[cur] ? 2 * g_ibone_cap[cur] : 64 * IC_BONE_REGS * 4;
        float *n = (float *)realloc(g_ibone[cur], cap * sizeof(float));
        if (!n)
            return;
        g_ibone[cur] = n;
        g_ibone_cap[cur] = cap;
    }
    o = &g_iobj[cur][g_iobj_n[cur]];
    o->kind = key;
    o->mesh = d->mesh;
    lk = ic_key(d, 1);                  /* step 68: the loose key, through the same steps */
    key = fnv(key, d->mesh);
    lk = fnv(lk, d->mesh);
    for (i = 0; i < d->n_rs; i++)       /* a pass that only marks stencil is another object than the model's */
        switch (d->rs[i].state) {       /* step 84: keep[]'s states, as a switch */
        case 14: case 19: case 20: case 22: case 52: case 55: case 56: case 168:
            key = fnv(fnv(key, d->rs[i].state), d->rs[i].value);
            lk = fnv(fnv(lk, d->rs[i].state), d->rs[i].value);
            break;
        }
    /* Step 63: the handedness of W and of the first bone. A reflection (the
     * hub floor's Crash) draws the same mesh with mirrored bones, and its
     * first bone sits a few hundredths of a unit from the model's: by
     * position alone the model took its reflection's place in the frame
     * before, and the matrices lerped through the mirror plane. */
    {
        double m[9];
        for (i = 0; i < 3; i++)
            for (j = 0; j < 3; j++)
                m[3 * i + j] = w[4 * i + j];
        key = fnv(key, (uint32_t)(ic_hand(m) + 1));
        lk = fnv(lk, (uint32_t)(ic_hand(m) + 1));
        if (k4) {
            for (i = 0; i < 9; i++)
                m[i] = k4[4 * (IC_BONE_BASE + i / 3) + i % 3];
            key = fnv(key, (uint32_t)(ic_hand(m) + 1));
            lk = fnv(lk, (uint32_t)(ic_hand(m) + 1));
        }
    }
    o->key = key;
    o->lkey = lk;
    o->hv = 0;
    o->prev = -1;
    o->used = 0;
    o->bones = -1;
    o->grp = -1;
    o->bmask = 0;
    o->own = 0;
    memcpy(o->w, w, sizeof(o->w));
    for (i = 0; i < 3; i++)
        o->pos[i] = w[12 + i];
    if (k4) {
        o->bones = (int32_t)g_ibone_n[cur];
        memcpy(g_ibone[cur] + o->bones, k4 + 4 * IC_BONE_BASE, IC_BONE_REGS * 4 * sizeof(float));
        g_ibone_n[cur] += IC_BONE_REGS * 4;
        for (i = 0; i < 3; i++)
            o->pos[i] += k4[4 * (IC_BONE_BASE + i) + 3];
        for (i = 0; i < IC_BONE_REGS / 3; i++) {
            unsigned r = IC_BONE_BASE + 3 * i;
            if (!((cmask[r >> 5] >> (r & 31) | cmask[(r + 1) >> 5] >> ((r + 1) & 31) |
                   cmask[(r + 2) >> 5] >> ((r + 2) & 31)) & 1))
                o->bmask |= 1u << i;
        }
        /* Step 67: a skinned draw uploads its own bones just before it; its
         * other bone registers hold earlier draws' (another character's,
         * often), and two draws holding the same leftover made one
         * skeleton of every character in a level (Banzai Bonsai: 80
         * objects), whose frame G was one character's: the rest swung
         * about it in between. Only its own bones pair and group it. */
        {
            uint32_t m = 0;
            for (i = 0; i < IC_BONE_REGS / 3; i++) {
                unsigned r = IC_BONE_BASE + 3 * i;
                if ((fresh[r >> 5] >> (r & 31) | fresh[(r + 1) >> 5] >> ((r + 1) & 31) |
                     fresh[(r + 2) >> 5] >> ((r + 2) & 31)) & 1)
                    m |= 1u << i;
            }
            if (o->bmask & m) {
                o->bmask &= m;
                o->own = 1;
            }
        }
    }
    c->u.draw.obj = (int32_t)g_iobj_n[cur]++;
}

/* Record time: the draw's camera in this frame's table, its class, and its
 * kind. Called with g_lock held. */
static void ic_record(rcmd_t *c)
{
    const d3dh_draw_t *d = &c->u.draw.d;
    unsigned ci, cur = (unsigned)g_ir;
    uint32_t key = ic_key(d, 0);
    icam_t *cam;

    GAME_SIDE();
    if (g_ikey_n[cur] == g_ikey_cap[cur]) {
        unsigned cap = g_ikey_cap[cur] ? 2 * g_ikey_cap[cur] : 2048;
        uint32_t *n = (uint32_t *)realloc(g_ikey[cur], cap * sizeof(uint32_t));
        if (n) {
            g_ikey[cur] = n;
            g_ikey_cap[cur] = cap;
        }
    }
    if (g_ikey_n[cur] < g_ikey_cap[cur])
        g_ikey[cur][g_ikey_n[cur]++] = key;
    if (!d->camera || d->overlay)       /* step 65: the HUD, title cards and pause menu: as recorded */
        return;
    for (ci = g_icam_n[cur]; ci-- > 0;)
        if (memcmp(g_icam[cur][ci].m, d->camera, sizeof(g_icam[cur][ci].m)) == 0)
            break;
    if (ci == (unsigned)-1) {
        if (g_icam_n[cur] == IC_CAMS)
            return;
        ci = g_icam_n[cur]++;
        memcpy(g_icam[cur][ci].m, d->camera, sizeof(g_icam[cur][ci].m));
        g_icam[cur][ci].draws = 0;
        icam_prep(&g_icam[cur][ci]);
    }
    c->u.draw.cam = (uint8_t)ci;
    cam = &g_icam[cur][ci];
    if (!cam->p_ok || !screen_target(&d->target))
        return;
    if (d->vprog) {
        int slot = vs_slot(d->vprog), r, k;
        const float *k4 = d->vprog->consts;
        double wvp[16], cm[16], t[16], w[16];
        if (slot < 0 || (g_vs[slot].cmask[3] & 0xF) != 0xF)
            return;
        for (r = 0; r < 4; r++)
            for (k = 0; k < 4; k++)
                wvp[4 * k + r] = k4[4 * (96 + r) + k];          /* c(96+r) is column r of WVP */
        m4_from(cam->m, cm);
        m4_mul(wvp, cam->inv_p, t);
        m4_mul(t, cm, w);
        for (r = 0; r < 4; r++) {
            double big = fabs(w[4 * r]) > fabs(w[4 * r + 1]) ? fabs(w[4 * r]) : fabs(w[4 * r + 1]);
            big = fabs(w[4 * r + 2]) > big ? fabs(w[4 * r + 2]) : big;
            if (fabs(w[4 * r + 3] - (r == 3)) > 2e-3 * (1.0 + big))
                return;                 /* not made with this camera */
        }
        c->u.draw.cls = IC_VP;
        {   /* Step 84: the registers it reads (the slot's list, as the old scan of all 192 took them) and the
             * camera's position with its tolerance once a draw, not at each register. */
            double cx[3], thr[3];
            unsigned j;
            for (k = 0; k < 3; k++) {
                cx[k] = cam->m[12 + k];
                thr[k] = 1e-3 * (1.0 + fabs(cam->m[12 + k]));
            }
            for (j = 0; j < g_vs[slot].cpos_n && c->u.draw.n_cpos < 4; j++) {
                r = g_vs[slot].cpos[j];
                for (k = 0; k < 3; k++)
                    if (fabs((double)k4[4 * r + k] - cx[k]) > thr[k])
                        break;
                if (k == 3)
                    c->u.draw.cpos[c->u.draw.n_cpos++] = (uint8_t)r;
            }
        }
        cam->draws++;
        ic_obj(c, key, w, g_vs[slot].rel_bones ? k4 : NULL, g_vs[slot].cmask, d->vprog->fresh);  /* step 62 */
    } else if (d->ff && (d->fvf & D3DFVF_POSITION_MASK) != D3DFVF_XYZRHW) {
        double v[16], cm[16], vc[16];
        int i, j;
        m4_from(d->ff->view, v);
        m4_from(cam->m, cm);
        m4_mul(v, cm, vc);
        for (i = 0; i < 4; i++)
            for (j = 0; j < 4; j++)
                if (fabs(vc[4 * i + j] - (i == j)) > 2e-3 * (i == 3 ? 1.0 + fabs(cam->m[12 + (j < 3 ? j : 0)]) : 1.0))
                    return;
        c->u.draw.cls = IC_FF_PROJ;
        for (i = 0; i < 16; i++)
            if (fabs((double)d->ff->proj[i] - cam->m[16 + i]) > 1e-4 * (1.0 + fabs(cam->m[16 + i])))
                c->u.draw.cls = IC_FF;
        cam->draws++;
        m4_from(d->ff->world, v);       /* step 62: an affine world is an object's */
        if (v[3] == 0 && v[7] == 0 && v[11] == 0 && v[15] == 1)
            ic_obj(c, key, v, NULL, NULL, NULL);
    }
}

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

/* Step 77: the frame-rate plan paces now: its pacing is in effect (not
 * the limiter standing in for a stalled vsync, step 63). */
static int fp_active(void)
{
    if (!g_fp.on)
        return 0;
    if (g_fp.limiter)
        return !g_latency && g_frame_hz > 0 && fabs(g_frame_hz - g_fp.slot_hz) < 1e-6;
    return g_latency && g_frame_hz <= 0 && g_interval == g_fp.interval;
}

/* Frames can be interpolated: the plan paces, with more than one slot a
 * game frame (step 77; before, a display paced at DXGI interval 2, 3 or 4). */
static int ic_possible(void)
{
    return fp_active() && g_fp.nsteps > 1;
}

/* A step's frames a second, and frames each game frame. */
static double fp_step_fps(unsigned k)
{
    return g_fp.step[k] > 0 ? g_fp.slot_hz / g_fp.step[k] : g_fp.slot_hz / g_fp.span;
}

static double fp_step_per(unsigned k)
{
    return g_fp.step[k] > 0 ? g_fp.span / g_fp.step[k] : 1.0;
}

/* Step 63: why ic_possible() is 0, into `buf`. */
static void ic_why(char *buf, size_t cap)
{
    char note[sizeof(g_pace_why)];

    AcquireSRWLockShared(&g_why_lock);
    memcpy(note, g_pace_why, sizeof(note));
    ReleaseSRWLockShared(&g_why_lock);
    if (g_fp.on && fp_active())         /* step 77 */
        snprintf(buf, cap, "the display's %.2f Hz is no faster than the game's %.2f fps", g_fp.slot_hz,
                 g_fp.game_hz);
    else if (g_fp.on && g_frame_hz > 0 && !g_fp.limiter)
        snprintf(buf, cap, "the frame limiter stands in for the display's vsync (%.2f fps)%s%s", g_frame_hz,
                 note[0] ? ": " : "", note);
    else if (g_frame_hz > 0)
        snprintf(buf, cap, "the frame limiter paces (%.2f fps)%s%s", g_frame_hz, note[0] ? ": " : "", note);
    else if (g_latency && g_interval < 2)
        snprintf(buf, cap, "the display paces one refresh a frame: needs 2, 3 or 4 x 59.94 Hz%s%s",
                 note[0] ? "; " : "", note);
    else if (g_latency)
        snprintf(buf, cap, "the display paces at interval %u: needs 2, 3 or 4", g_interval);
    else
        snprintf(buf, cap, "no vsync pacing (DXGI interval %u, no limiter)%s%s", g_interval, note[0] ? ": " : "",
                 note);
}

/* Step 63: a log line each time interpolation stops or starts being
 * possible while the setting is on. */
static void ic_log_possible(int k)
{
    static char last[256] = "?";
    char now[256];

    if (k)
        snprintf(now, sizeof(now), "on: %.4g frames each game frame, %.2f fps%s", g_fp.span / g_fp.step[0],
                 g_fp.slot_hz / g_fp.step[0], g_fp.limiter ? " (the frame limiter, variable refresh)" : "");
    else
        ic_why(now, sizeof(now));
    if (strcmp(now, last) != 0) {
        memcpy(last, now, sizeof(last));
        if (k)
            fprintf(stderr, "[D3DB] host interpolation %s (display %.3f Hz)\n", now, d3dh_monitor_hz());
        else
            fprintf(stderr, "[D3DB] host interpolation off, one frame each game frame: %s\n", now);
    }
}

/* ── Step 77: time-based sub-frames (notes 18.80) ──────────────
 * Before, a game frame got a whole number k of sub-frames at t = 1/k .. 1,
 * k = the display's rate / 59.94 (2-4), so a display that isn't a multiple
 * (144 Hz) had no interpolation at all. Now the game frames lie on a clock
 * of slots (a refresh each under vsync; with variable refresh the
 * limiter's ticks), game frame n ending at slot S(n) = n x span (span =
 * slots a second / 59.94: 2.4024 at 144 Hz), and the presents go on a grid
 * of whole slots `step` apart that runs on from one game frame to the next.
 * Each present at slot r inside game frame n (S(n-1) < r <= S(n)) is drawn
 * at t = (r - S(n-1)) / span, its own place in time between the two game
 * frames, so motion is even however many presents a game frame gets (2 or
 * 3 at 144 Hz, 2.4 on average). The game is still held in its Swap until
 * its last present, so it runs 59.94 updates a second against the display.
 * On a whole multiple the span is a whole number (within 0.5%, as the
 * pacing always took it) and this is the old k sub-frames exactly: slots
 * 1/k apart from a game frame's start, t = j/k, the last at t = 1, DXGI
 * interval `step` (1 at 4 of 4; 2 at 2 of 4; 4 at 1). A frame not
 * interpolated (a cut, the first, the overlay) is one present to the
 * game frame's last slot on the grid, at that many refreshes. */

/* Each step's mean passes and pairings a game frame (ia_cost), by laying
 * 1200 game frames on its grid. */
static void fp_costs(void)
{
    unsigned i, n;

    for (i = 0; i < g_fp.nsteps; i++) {
        double g = g_fp.step[i], pos = 0, end = 0, ni = 0, np = 0, nf = 0;
        for (n = 0; n < 1200; n++) {
            double prev = end, r;
            int any = 0;
            end += g_fp.span;
            if (fabs(end - floor(end + 0.5)) < 1e-9)
                end = floor(end + 0.5);
            if (g <= 0) {               /* one present, as recorded */
                np++;
                pos = g_fp.limiter ? end : floor(end + 1e-6);
                continue;
            }
            for (r = pos + g; r <= end + 1e-6; r += g) {
                if ((r - prev) / g_fp.span > 1 - 1e-9)
                    np++;
                else {
                    ni++;
                    any = 1;
                }
                pos = r;
            }
            nf += any;
        }
        g_fp.cost_i[i] = ni / 1200;
        g_fp.cost_p[i] = np / 1200;
        g_fp.cost_f[i] = nf / 1200;
    }
}

/* This game frame's presents at step `k` (its slots, its t), the clock
 * moved on past them. */
static void fp_frame(fp_frame_t *f, unsigned k)
{
    double g = g_fp.step[k], end, r, prev;

    REPLAY_SIDE();
    if (g_fc.gen != g_fp.gen) {
        g_fc.pos = g_fc.end = 0;
        g_fc.gen = g_fp.gen;
    }
    end = g_fc.end + g_fp.span;
    if (fabs(end - floor(end + 0.5)) < 1e-9)
        end = floor(end + 0.5);         /* a whole span stays whole */
    f->n = 0;
    f->interp = 0;
    prev = g_fc.pos;
    if (g <= 0) {
        r = g_fp.limiter ? end : floor(end + 1e-6);
        f->t[0] = f->t0[0] = 1.0;
        f->slots[0] = r - prev;
        f->n = 1;
        prev = r;
    } else {
        for (r = g_fc.pos + g; r <= end + 1e-6 && f->n < FP_MAX_PRESENTS; r += g) {
            double t = (r - g_fc.end) / g_fp.span;
            if (t > 1 - 1e-9)
                t = 1.0;
            else
                f->interp = 1;
            f->t[f->n] = t;
            f->t0[f->n] = (r - g - g_fc.end) / g_fp.span;
            f->slots[f->n] = r - prev;
            f->n++;
            prev = r;
        }
    }
    g_fc.pos = prev;
    g_fc.end = end;
    if (g_fc.pos >= 1048576.0) {        /* by whole slots, so a whole clock stays exact */
        double b = floor(g_fc.pos);
        g_fc.pos -= b;
        g_fc.end -= b;
    }
}

/* ── Step 77: variable refresh (notes 18.80) ────────────────────
 * A target that isn't the display's refresh or a whole fraction of it
 * (90 fps at 240 Hz) is only shown evenly by a display running variable
 * refresh (G-SYNC, FreeSync, VESA Adaptive-Sync), which shows each frame
 * when it comes. Windows has no user-mode call that says whether that is on
 * (the driver's VariableRefresh capability is kernel-side, and DXGI's
 * tearing support says what the system allows, not the display), so:
 *  - the EDID (the monitor's registry copy, found by the display path's
 *    device name): its range limits' vertical range with the "continuous
 *    frequency" bit, or AMD's FreeSync data block, say the display claims
 *    a range (the user's MSI MAG321UX OLED: 48-240 Hz, both);
 *  - NVIDIA's driver answers outright (nvapi64.dll, NvAPI_Disp_GetVRRInfo,
 *    ids from NVIDIA's MIT nvapi headers): possible and on, or not;
 *  - and the frames decide: a plan that needs variable refresh presents
 *    from the limiter at its rate, and DXGI's frame statistics give the
 *    time each frame reached the screen. On a fixed refresh they fall on
 *    its grid (90 fps at 240 Hz: 8.3 and 12.5 ms apart), with variable
 *    refresh where they were presented (11.1). 120 measured frames decide
 *    for the window mode (windowed, fullscreen) for the rest of the run;
 *    not even, the plan falls back to an even rate.
 * RECOMP_VRR=0 never assumes it, =1 always (no measuring), =measure
 * measures whatever the driver says (test aids). */
#define VRR_SAMPLES 120
static struct {
    HMONITOR mon;                       /* what these were read for (NULL: not yet) */
    int edid_min, edid_max, continuous; /* the EDID's vertical range (Hz, 0: none), its continuous-frequency bit */
    int fs_min, fs_max;                 /* AMD's FreeSync block (0: none) */
    int nv;                             /* NVIDIA: 1 possible and on, 0 not, -1 no answer (not NVIDIA) */
    int measured[2];                    /* windowed [0], fullscreen [1]: -1 not yet, 0 fixed refresh, 1 variable */
    unsigned follow, grid, seen;        /* the measuring under way: as presented, on the grid, frames seen */
    char result[160];                   /* the last measurement, for the log and the overlay */
} g_vrr = { NULL, 0, 0, 0, 0, 0, -1, { -1, -1 } };

/* The EDID and NVIDIA's answer for monitor `mon`. */
static void vrr_read(HMONITOR mon)
{
    MONITORINFOEXA mi;
    UINT32 np = 0, nm = 0, i;
    DISPLAYCONFIG_PATH_INFO *paths = NULL;
    DISPLAYCONFIG_MODE_INFO *modes = NULL;
    unsigned char edid[1024];
    DWORD esz = 0;

    g_vrr.mon = mon;
    g_vrr.edid_min = g_vrr.edid_max = g_vrr.continuous = g_vrr.fs_min = g_vrr.fs_max = 0;
    g_vrr.nv = -1;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(mon, (MONITORINFO *)&mi))
        return;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) == ERROR_SUCCESS) {
        paths = (DISPLAYCONFIG_PATH_INFO *)calloc(np, sizeof(*paths));
        modes = (DISPLAYCONFIG_MODE_INFO *)calloc(nm, sizeof(*modes));
    }
    if (paths && modes && QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths, &nm, modes, NULL) == ERROR_SUCCESS)
        for (i = 0; i < np && !esz; i++) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME sn;
            DISPLAYCONFIG_TARGET_DEVICE_NAME tn;
            char name[64], path[300], key[400], *part[4] = { NULL }, *p;
            int n = 0;
            HKEY k;
            memset(&sn, 0, sizeof(sn));
            sn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            sn.header.size = sizeof(sn);
            sn.header.adapterId = paths[i].sourceInfo.adapterId;
            sn.header.id = paths[i].sourceInfo.id;
            if (DisplayConfigGetDeviceInfo(&sn.header) != ERROR_SUCCESS)
                continue;
            WideCharToMultiByte(CP_ACP, 0, sn.viewGdiDeviceName, -1, name, sizeof(name), NULL, NULL);
            if (_stricmp(name, mi.szDevice) != 0)
                continue;
            memset(&tn, 0, sizeof(tn));
            tn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
            tn.header.size = sizeof(tn);
            tn.header.adapterId = paths[i].targetInfo.adapterId;
            tn.header.id = paths[i].targetInfo.id;
            if (DisplayConfigGetDeviceInfo(&tn.header) != ERROR_SUCCESS)
                break;
            /* \\?\DISPLAY#MSI3DD3#5&2db65555&0&UID4353#{guid} -> Enum\DISPLAY\MSI3DD3\5&2db65555&0&UID4353 */
            WideCharToMultiByte(CP_ACP, 0, tn.monitorDevicePath, -1, path, sizeof(path), NULL, NULL);
            p = strncmp(path, "\\\\?\\", 4) == 0 ? path + 4 : path;
            for (part[n++] = p; *p && n < 4; p++)
                if (*p == '#') {
                    *p = 0;
                    part[n++] = p + 1;
                }
            if (n < 3)
                break;
            snprintf(key, sizeof(key), "SYSTEM\\CurrentControlSet\\Enum\\%s\\%s\\%s\\Device Parameters", part[0],
                     part[1], part[2]);
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &k) == ERROR_SUCCESS) {
                esz = sizeof(edid);
                if (RegQueryValueExA(k, "EDID", NULL, NULL, edid, &esz) != ERROR_SUCCESS)
                    esz = 0;
                RegCloseKey(k);
            }
            break;
        }
    free(paths);
    free(modes);
    if (esz >= 128) {
        unsigned d, x;
        g_vrr.continuous = edid[24] & 1;
        for (d = 54; d < 126; d += 18)
            if (!edid[d] && !edid[d + 1] && edid[d + 3] == 0xFD) {   /* display range limits */
                g_vrr.edid_min = edid[d + 5] + ((edid[d + 4] & 1) ? 255 : 0);
                g_vrr.edid_max = edid[d + 6] + ((edid[d + 4] & 2) ? 255 : 0);
            }
        for (x = 1; (x + 1) * 128 <= esz; x++) {    /* CTA-861 extensions: AMD's vendor block (OUI 00001A) */
            const unsigned char *b = edid + x * 128;
            unsigned p;
            if (b[0] != 2)
                continue;
            for (p = 4; p < b[2] && p < 127;) {
                unsigned tag = b[p] >> 5, len = b[p] & 31;
                if (tag == 3 && len >= 8 && p + 8 < 128 && b[p + 1] == 0x1A && !b[p + 2] && !b[p + 3]) {
                    g_vrr.fs_min = b[p + 6];
                    g_vrr.fs_max = b[p + 7];
                }
                p += 1 + len;
            }
        }
    }
    {   /* NVIDIA */
        static HMODULE nv;
        static int tried;
        typedef void *(__cdecl *qi_t)(unsigned);
        typedef int(__cdecl *init_t)(void);
        typedef int(__cdecl *byname_t)(const char *, unsigned *);
        typedef int(__cdecl *vrr_t)(unsigned, void *);
        static vrr_t vrr;
        static byname_t byname;
        unsigned id = 0, info[6];
        if (!tried) {
            qi_t qi;
            init_t init;
            tried = 1;
            nv = LoadLibraryA("nvapi64.dll");
            qi = nv ? (qi_t)GetProcAddress(nv, "nvapi_QueryInterface") : NULL;
            init = qi ? (init_t)qi(0x0150e828) : NULL;                  /* NvAPI_Initialize */
            byname = qi ? (byname_t)qi(0xae457190) : NULL;              /* NvAPI_DISP_GetDisplayIdByDisplayName */
            vrr = qi ? (vrr_t)qi(0xdf8fda57) : NULL;                    /* NvAPI_Disp_GetVRRInfo (R525+) */
            if (!init || init() != 0)
                vrr = NULL;
        }
        memset(info, 0, sizeof(info));
        info[0] = (unsigned)sizeof(info) | (1u << 16);                  /* NV_GET_VRR_INFO_VER1 */
        if (vrr && byname && byname(mi.szDevice, &id) == 0 && vrr(id, info) == 0)
            g_vrr.nv = (info[1] & 2) && (info[1] & 5) ? 1 : 0;          /* possible, and enabled or requested */
    }
    fprintf(stderr, "[D3DB] display %s: EDID vertical range %d-%d Hz%s, FreeSync block %d-%d Hz; NVIDIA variable"
            " refresh %s\n", mi.szDevice, g_vrr.edid_min, g_vrr.edid_max, g_vrr.continuous ? " (continuous)" : "",
            g_vrr.fs_min, g_vrr.fs_max, g_vrr.nv < 0 ? "(no answer: not NVIDIA)" : g_vrr.nv ? "on" : "off");
}

/* Whether a plan may count on variable refresh on the window's monitor for
 * `hz` frames a second: 1 yes, 2 perhaps (measure it), 0 no, with why in
 * `why`. A measurement made in this window mode decides. */
static int vrr_claim(double hz, char *why, size_t cap)
{
    const char *e = getenv("RECOMP_VRR");
    HMONITOR mon;
    int m, range;

    if (e && strcmp(e, "0") == 0) {
        snprintf(why, cap, "RECOMP_VRR=0");
        return 0;
    }
    if (e && strcmp(e, "1") == 0) {
        snprintf(why, cap, "RECOMP_VRR=1, not measured");
        return 1;
    }
    if (g_hwnd)
        mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
    else {
        POINT o = { 0, 0 };
        mon = MonitorFromPoint(o, MONITOR_DEFAULTTOPRIMARY);
    }
    if (mon != g_vrr.mon) {
        vrr_read(mon);
        g_vrr.measured[0] = g_vrr.measured[1] = -1;
    }
    m = g_vrr.measured[g_fs ? 1 : 0];
    if (e && strcmp(e, "measure") == 0 && m < 0) {
        snprintf(why, cap, "RECOMP_VRR=measure");
        return 2;                       /* a check: measured whatever the driver and the display say */
    }
    if (g_vrr.nv == 0 && m < 0) {
        snprintf(why, cap, "NVIDIA's driver has variable refresh off for this display");
        return 0;
    }
    if (m == 0) {
        snprintf(why, cap, "%s", g_vrr.result);
        return 0;
    }
    if (m == 1) {
        snprintf(why, cap, "%s", g_vrr.result);
        return 1;
    }
    range = (g_vrr.fs_min > 0 && g_vrr.fs_min <= hz && g_vrr.fs_max >= hz) ||
            (g_vrr.continuous && g_vrr.edid_min > 0 && g_vrr.edid_min <= hz && g_vrr.edid_max >= hz);
    if (g_vrr.nv == 1 || range) {
        snprintf(why, cap, "%s", g_vrr.nv == 1 ? "NVIDIA's driver has variable refresh on" :
                 "the display reports a variable refresh range");
        return 2;
    }
    snprintf(why, cap, "the display reports no variable refresh range");
    return 0;
}

/* A frame shown `gap_ms` after the one before, presented `want_ms` after
 * it, while a plan measures variable refresh (DXGI's frame statistics, in
 * present_one). Called with g_lock held. */
static void vrr_sample(double gap_ms, double want_ms, double refresh_ms)
{
    double grid;
    int fs = g_fs ? 1 : 0;

    if (!g_fp.on || !g_fp.limiter || !g_fp.probing || g_vrr.measured[fs] >= 0 || refresh_ms <= 0)
        return;
    g_vrr.seen++;
    grid = refresh_ms * floor(want_ms / refresh_ms + 0.5);
    if (fabs(want_ms - grid) > 0.3) {   /* a gap the grid can't make: the sample tells */
        if (fabs(gap_ms - want_ms) < fabs(gap_ms - refresh_ms * floor(gap_ms / refresh_ms + 0.5)))
            g_vrr.follow++;
        else
            g_vrr.grid++;
    }
    if (g_vrr.follow + g_vrr.grid >= VRR_SAMPLES || g_vrr.seen >= 8 * VRR_SAMPLES) {
        unsigned n = g_vrr.follow + g_vrr.grid;
        g_vrr.measured[fs] = n >= VRR_SAMPLES / 2 && g_vrr.follow * 10 >= n * 8;
        if (n >= VRR_SAMPLES / 2)
            snprintf(g_vrr.result, sizeof(g_vrr.result), "measured %s: %u of %u frames shown %s",
                     fs ? "fullscreen" : "in a window", g_vrr.measured[fs] ? g_vrr.follow : g_vrr.grid, n,
                     g_vrr.measured[fs] ? "as presented" : "on the refresh grid");
        else
            snprintf(g_vrr.result, sizeof(g_vrr.result), "not measurable %s (no frame statistics)",
                     fs ? "fullscreen" : "in a window");
        fprintf(stderr, "[D3DB] display: variable refresh %s: %s (%.2f fps presented, refresh %.3f ms)\n",
                g_vrr.measured[fs] ? "on" : "not seen", g_vrr.result, g_fp.slot_hz, refresh_ms);
        g_vrr.follow = g_vrr.grid = g_vrr.seen = 0;
        InterlockedExchange(&g_fp_replan, 1);
    }
}

/* Step 77: the frame-rate plan for setting `fr` (1: the display's own
 * rate), for a game presenting at `game_hz` under vsync pacing (the pacing
 * setting Auto or Vsync): the display's mode rate, game frames `span`
 * refreshes long, a present every refresh at the top step, and below it the
 * old counts (whole multiples: each divisor of the span) or one present a
 * game frame. Sets *interval and *hz for d3dh_create / d3dh_set_pacing and
 * returns 1; 0 (fr 0, no rate known): no plan, the caller's own pacing. */
int d3dh_frame_plan(int fr, double game_hz, unsigned *interval, double *hz)
{
    double r = fr ? d3dh_monitor_hz() : 0, span, want;
    unsigned k, n = 0, m, m0 = 0;
    int ready = g_ready, claim;
    char why[160];

    REPLAY_SIDE();
    if (r <= 0 && fr)
        r = d3dh_display_hz();
    if (ready)
        EnterCriticalSection(&g_lock);
    if (!fr || r <= 0 || game_hz <= 0) {
        if (g_fp.on)
            fprintf(stderr, "[D3DB] host frame rate plan: none (%s)\n", fr ? "the display's rate isn't known" :
                    "the game's 59.94");
        g_fp.on = 0;
        if (ready)
            LeaveCriticalSection(&g_lock);
        return 0;
    }
    want = fr == 1 ? r : fr;
    n = g_fp.gen + 1;
    memset(&g_fp, 0, sizeof(g_fp));
    g_fp.gen = n;
    n = 0;
    g_fp.fr = fr;
    g_fp.fs = g_fs ? 1 : 0;
    g_fp.disp_hz = r;
    if (want > r * 1.005) {             /* above the display: its own rate */
        snprintf(g_fp.note, sizeof(g_fp.note), "%d fps is above this %.2f Hz display: %.2f fps, a frame each "
                 "refresh (even).", fr, r, r);
        want = r;
        m0 = 1;
    }
    for (m = 1; !m0 && m <= 16; m++)    /* the refresh or a whole fraction of it: even */
        if (fabs(r / m - want) <= 0.005 * want)
            m0 = m;
    if (m0 && !g_fp.note[0]) {
        if (m0 == 1)
            snprintf(g_fp.note, sizeof(g_fp.note), "Even: a frame each refresh of this %.2f Hz display.", r);
        else
            snprintf(g_fp.note, sizeof(g_fp.note), "Even: %.2f fps, each frame held %u refreshes of this %.2f Hz "
                     "display.", r / m0, m0, r);
    }
    if (!m0) {                          /* only variable refresh shows it evenly */
        g_fp.needs_vrr = 1;
        claim = vrr_claim(want, why, sizeof(why));
        if (claim) {
            g_fp.limiter = 1;
            g_fp.probing = claim == 2;
            if (claim == 1)
                snprintf(g_fp.note, sizeof(g_fp.note), "Even: %d fps by variable refresh (%s).", fr, why);
            else
                snprintf(g_fp.note, sizeof(g_fp.note), "Checking whether this %.2f Hz display shows %d fps evenly "
                         "(variable refresh: %s)...", r, fr, why);
        } else {                        /* the highest even rate not above the target, else the lowest above the game's */
            for (m = 1; !m0 && m <= 16; m++)
                if (r / m <= want * 1.005 && r / m >= game_hz * 0.995)
                    m0 = m;
            for (m = 16; !m0 && m >= 1; m--)
                if (r / m >= game_hz * 0.995)
                    m0 = m;
            m0 = m0 ? m0 : 1;
            snprintf(g_fp.note, sizeof(g_fp.note), "%d fps isn't even on this %.2f Hz display (no variable refresh: "
                     "%s): %.2f fps instead, each frame held %u refresh%s.", fr, r, why, r / m0, m0,
                     m0 > 1 ? "es" : "");
        }
    }
    g_fp.want = g_fp.limiter ? want : r / m0;
    g_fp.even = !g_fp.limiter || !g_fp.probing;
    g_fp.slot_hz = g_fp.limiter ? want : r;
    span = g_fp.slot_hz / game_hz;
    k = (unsigned)(span + 0.5);
    if (k >= 1 && fabs(span - k) <= 0.005 * span)
        span = k;                       /* a whole multiple, as the pacing always took it */
    else
        k = 0;
    g_fp.on = 1;
    g_fp.span = span;
    g_fp.game_hz = game_hz;
    g_fp.interval = g_fp.limiter ? 1 : k >= 1 && k <= 4 ? k : 1;
    /* The target's own step, then (item 3) each slower whole number of
     * slots still faster than the game, then one present a game frame. A
     * step is kept only if it costs less than the one above it (passes, an
     * interpolated one counted as two plain). At 240 Hz the ladder is 240,
     * 120, 80 and 60 fps (80: a present every 3 refreshes, 1 interpolated
     * pass and 1/3 plain a game frame against 120's 1 and 1); at 144 Hz
     * 144, 72 and 60. */
    for (m = g_fp.limiter ? 1 : m0; m < span - 1e-6 && n < FP_MAX_STEPS - 1; m++)
        g_fp.step[n++] = m;
    g_fp.step[n++] = 0;                 /* one a game frame */
    g_fp.nsteps = n;
    fp_costs();
    for (m = n = 1; m < g_fp.nsteps; m++) {
        double c = 2 * g_fp.cost_i[m] + g_fp.cost_p[m], above = 2 * g_fp.cost_i[n - 1] + g_fp.cost_p[n - 1];
        if (g_fp.step[m] > 0 && c > above - 0.01)
            continue;
        g_fp.step[n] = g_fp.step[m];
        g_fp.cost_i[n] = g_fp.cost_i[m];
        g_fp.cost_p[n] = g_fp.cost_p[m];
        g_fp.cost_f[n] = g_fp.cost_f[m];
        n++;
    }
    g_fp.nsteps = n;
    for (n = 0; n < 8; n++)
        g_st.interp_step_fps[n] = n < g_fp.nsteps ? fp_step_fps(n) : 0;
    *interval = g_fp.interval;
    *hz = g_fp.limiter ? g_fp.slot_hz : 0;
    if (ready)
        LeaveCriticalSection(&g_lock);
    fprintf(stderr, "[D3DB] host frame rate plan: %s at %.3f fps on the display's %.3f Hz, %.4f %s a game frame"
            " (%.3f fps), %s %u: %s\n", fr == 1 ? "the display's rate" : "the setting", fp_step_fps(0), r, span,
            g_fp.limiter ? "limiter ticks" : "refreshes", game_hz, g_fp.limiter ? "the limiter, vsync interval" :
            "vsync interval", g_fp.interval, g_fp.note);
    return 1;
}

/* ── Step 74: adaptive sub-frames (notes 18.77) ───────────────
 * Each sub-frame replays the whole frame, and above ~1,000 draws (cast_bug,
 * avalanch) four of them didn't fit a game frame: 37-56 fps. How many
 * sub-frames a frame gets now follows how the frames before ran: the time
 * between the game's presents (dt), the part of it outside the pacing wait
 * (busy: the game, the recording, the replays and the presents), and what a
 * replayed command costs, plain and interpolated (per command, so a new
 * scene's count of draws is priced at once).
 * Down: a window of IA_WIN measured frames, mostly interpolated, whose mean
 * dt is over a frame's time T by IA_LATE, while busy (over IA_BUSY of T: the
 * work, not a wait elsewhere) and the passes dropped would save at least
 * half the overrun (not a load): the next count that divides the display's
 * (4 -> 2 -> 1, 3 -> 1). Up: IA_UP frames on time at a count held at least
 * `hold` s, whose mean busy time plus the next count's extra passes (x
 * IA_PASS_X; the pairing too, going up from 1) fits in IA_FIT of T. Step 77
 * (item 3): the counts are the frame-rate plan's steps, and a change goes
 * as far as fits: down to the fastest lower step whose predicted busy time
 * fits IA_FIT of T (none: the slowest), up to the fastest that fits (144 ->
 * 72 -> 60 fps at 144 Hz; 240 -> 120, 80 or straight to 60 at 240). A drop
 * within IA_FAIL s of a rise doubles the hold (IA_HOLD0 to IA_HOLD_MAX); a
 * rise kept IA_KEPT s sets it back. A frame's k sub-frames are presented
 * at the display's interval / k, so every game frame still spans the same
 * refreshes and a change shows nothing on screen. The queue counts presents,
 * though, so at fewer sub-frames it holds more time (at 1, as many game
 * frames as without interpolation's queue would at 4), and after a rise the
 * game waits while it drains: the first IA_SETTLE frames after a change
 * aren't judged. (Presenting each of the k sub-frames 4 / k times at
 * interval 1 kept the queue's time, but back-to-back presents wait for the
 * GPU: 0.7 ms each, 54.6 fps at 1 in cast_bug against 60 this way.) Each
 * change is logged.
 * RECOMP_D3D_BRIDGE_INTERP_ADAPT=0 keeps the display's count (the "always 4"
 * test switch), RECOMP_D3D_BRIDGE_INTERP_FIXED=K always K.
 * Step 75: that switch is now the setting "Adaptive frame rate" (Esc,
 * Display; `adaptive_framerate`, RECOMP_FRAMERATE_ADAPTIVE), on unless set
 * to 0, applied live (d3dh_set_interp_adaptive): a change goes back to the
 * display's count and judges afresh from there. */
#define IA_WIN 30
#define IA_UP 120
#define IA_LATE 1.03
#define IA_BUSY 0.85
#define IA_SETTLE 15
#define IA_FIT 0.85
#define IA_PASS_X 1.15
#define IA_HOLD0 2.0
#define IA_HOLD_MAX 32.0
#define IA_FAIL 5.0
#define IA_KEPT 20.0
typedef struct {
    unsigned n;                         /* frames */
    double dt, busy, cmds;              /* their sums: ms apart, ms busy, commands a pass */
    double game, replay;                /* step 79: of busy, the game side's and the replay side's */
} ia_sum_t;
/* Step 77: the counts are the frame-rate plan's steps (g_fp.step: 4, 2 and
 * 1 sub-frames at 240 Hz as before; 144 and one frame each game frame at 144
 * Hz), `k` an index into them, 0 the plan's own rate. */
static struct {
    unsigned k;                         /* the step allowed now (0: the plan's rate) */
    unsigned gen;                       /* the plan (g_fp.gen) it was chosen under; 0: not started */
    double t_ms;                        /* a game frame's time on this display */
    unsigned kused;                     /* presents the frame being measured got; 0: not measured */
    double game_ms, replay_ms;          /* step 78: its game side's time (the slot's), its replay side's busy time */
    LONGLONG rp0;                       /* step 78, QPC: its replay began (ia_frame) */
    int paired;                         /* ... and it was interpolated */
    LONGLONG last;                      /* QPC: that frame's present began; 0: none */
    double pace_ms;                     /* its time in pace() */
    double rp_ms0[2], rp_cmds0[2], pair0;   /* g_rp_ms, g_rp_cmds, g_st.pair_ms at its start */
    unsigned rp_n0[2];                  /* g_rp_n at its start */
    double c_pass[2];                   /* ms a command, plain and interpolated passes (0: not known yet) */
    double c_pair;                      /* ms a command pairing (going up from 1) */
    ia_sum_t w;                         /* the down window */
    unsigned wi;                        /* of it, frames interpolated */
    unsigned settle;                    /* frames after a change not judged */
    ia_sum_t u;                         /* the up stretch */
    LONGLONG changed, rose;             /* QPC: the last change, the last rise (0: none) */
    double hold;                        /* seconds at a count before a rise */
    double fps;                         /* step 82: the step's rate now, under its plan (for the restart's line) */
} g_ia;
static int g_ia_thr;                    /* step 79: measured on the render thread */

/* Step 79: a frame's mean busy time (`s`'s) with `d` ms more of replays. On
 * one thread the replays add to the game's time; with the render thread
 * the two sides run side by side, and busy is the larger of the game side's
 * time and the render thread's, the replays only on the latter. */
static double ia_busy(const ia_sum_t *s, double d)
{
    double g = s->game / s->n, r = s->replay / s->n + d;

    return g_ia_thr ? (g > r ? g : r) : s->busy / s->n + d;
}

/* The step to go to from `k` (item 3), up (fewer slots apart, more frames)
 * or down: the fastest on that side whose frame, as `s` measured it with
 * `cmds` commands, fits IA_FIT of a game frame (going up, its extra passes x
 * IA_PASS_X); none fits: down, the slowest; up, `k` itself. */
static double ia_cost(unsigned from, unsigned to, double cmds);
static unsigned ia_next(unsigned k, int up, const ia_sum_t *s, double cmds)
{
    unsigned j;

    if (up) {
        for (j = 0; j < k; j++)
            if (ia_busy(s, ia_cost(k, j, cmds) * IA_PASS_X) <= g_ia.t_ms * IA_FIT)
                return j;
        return k;
    }
    for (j = k + 1; j < g_fp.nsteps; j++)
        if (ia_busy(s, ia_cost(k, j, cmds)) <= g_ia.t_ms * IA_FIT)
            return j;
    return g_fp.nsteps ? g_fp.nsteps - 1 : k;
}

/* The step this frame may have (an index into g_fp.step). */
static unsigned ia_k(void)
{
    unsigned i;

    if (g_ifixed) {                     /* the step with that many presents each game frame */
        for (i = 0; i < g_fp.nsteps; i++)
            if (fabs(fp_step_per(i) - g_ifixed) < 1e-6)
                return i;
        return 0;
    }
    if (!g_iadapt || g_ia.gen != g_fp.gen)
        return 0;
    return g_ia.k;
}

/* What step `from` -> `to` adds to a frame of `cmds` commands (ms;
 * negative going down): its interpolated and plain passes and its pairing
 * (step 77; on the old counts, (to - from) interpolated passes, and the
 * pairing to or from 1). */
static double ia_cost(unsigned from, unsigned to, double cmds)
{
    double ci = g_ia.c_pass[1] > 0 ? g_ia.c_pass[1] : 2.0 * g_ia.c_pass[0];    /* not seen yet: twice a plain one */

    return cmds * ((g_fp.cost_i[to] - g_fp.cost_i[from]) * ci + (g_fp.cost_p[to] - g_fp.cost_p[from]) * g_ia.c_pass[0] +
                   (g_fp.cost_f[to] - g_fp.cost_f[from]) * g_ia.c_pair);
}

static void ia_change(unsigned k, LONGLONG q, unsigned swap_no, const char *why)
{
    fprintf(stderr, "[D3DB] host interpolation: %.2f fps, %.4g frames each game frame, from guest swap %u (was %.2f):"
            " %s\n", fp_step_fps(k), fp_step_per(k), swap_no, fp_step_fps(g_ia.k), why);
    g_st.interp_adapt_changes++;
    if (k < g_ia.k)
        g_ia.rose = q;
    g_ia.k = k;
    g_ia.fps = fp_step_fps(k);
    g_ia.changed = q;
    g_ia.settle = IA_SETTLE;
    memset(&g_ia.w, 0, sizeof(g_ia.w));
    memset(&g_ia.u, 0, sizeof(g_ia.u));
    g_ia.wi = 0;
}

static void ia_add(ia_sum_t *s, double dt, double busy, double cmds, double game, double replay)
{
    s->n++;
    s->dt += dt;
    s->busy += busy;
    s->cmds += cmds;
    s->game += game;
    s->replay += replay;
}

/* The frame's replay counters, as the next frame's start. */
static void ia_mark(void)
{
    memcpy(g_ia.rp_ms0, g_rp_ms, sizeof(g_rp_ms));
    memcpy(g_ia.rp_cmds0, g_rp_cmds, sizeof(g_rp_cmds));
    memcpy(g_ia.rp_n0, g_rp_n, sizeof(g_rp_n));
    g_ia.pair0 = g_st.pair_ms;
    g_ia.pace_ms = 0;
    g_ia.kused = 0;
    g_ia.paired = 0;
}

/* At each present while interpolating (and once after): the frame before
 * measured, and the count for this one chosen. Called with g_lock held. */
static void ia_frame(const fslot_t *f)
{
    LARGE_INTEGER q;
    unsigned used = g_ia.kused, cur, kn, i, swap_no = f->swap_no;
    int possible = ic_possible(), paired = g_ia.paired;
    double dt, busy, cmds = 0, since, pair_ms = g_st.pair_ms - g_ia.pair0, game_ms = f->game_ms, replay_ms = g_ia.replay_ms;
    char why[256];

    REPLAY_SIDE();
    QueryPerformanceCounter(&q);
    dt = g_ia.last ? (double)(q.QuadPart - g_ia.last) * 1000.0 / (double)g_qpf.QuadPart : 0.0;
    /* Busy: on one thread the time between the frames' replays less the
     * pacing waits, the game's own work included (step 78: the game side's
     * time on the frame, from the slot, and the replay side's, measured
     * apart too). Step 79: with the render thread each runs beside the
     * other, and the larger of the two decides: this frame's game side (it
     * was recorded while the render thread replayed the frame before) and
     * the render thread's time on the frame before, from taking it to its
     * last present, less the pacing waits. */
    g_ia_thr = t_rt;
    busy = dt - g_ia.pace_ms;
    if (t_rt)
        busy = game_ms > replay_ms ? game_ms : replay_ms;
    g_ia.rp0 = t_rt ? g_rt_take.QuadPart : q.QuadPart;
    for (i = 0; i < 2; i++) {           /* ms a command, plain and interpolated passes */
        unsigned n = g_rp_n[i] - g_ia.rp_n0[i];
        double c = g_rp_cmds[i] - g_ia.rp_cmds0[i], ms = g_rp_ms[i] - g_ia.rp_ms0[i];
        if (n && c >= 50) {
            g_ia.c_pass[i] = g_ia.c_pass[i] > 0 ? 0.9 * g_ia.c_pass[i] + 0.1 * ms / c : ms / c;
            if (i == 0 || !cmds)    /* a plain pass's commands, else (step 77: at 144 Hz a game frame */
                cmds = c / n;       /* seldom ends on a refresh, so few have one) an interpolated one's */
        }
    }
    if ((used > 1 || paired) && cmds >= 50 && pair_ms > 0)
        g_ia.c_pair = g_ia.c_pair > 0 ? 0.9 * g_ia.c_pair + 0.1 * pair_ms / cmds : pair_ms / cmds;
    g_ia.last = q.QuadPart;
    ia_mark();
    if (!f->interp || !possible) {      /* stopped: the next start is from the plan's rate */
        if (g_ia.gen && g_ia.k)
            fprintf(stderr, "[D3DB] host interpolation: sub-frames back to the plan's rate (it stopped)\n");
        memset(&g_ia, 0, sizeof(g_ia));
        g_ia.rp0 = t_rt ? g_rt_take.QuadPart : q.QuadPart;
        g_iadapt_seen = g_iadapt;
        return;
    }
    if (g_fp.gen != g_ia.gen) {         /* started, or another plan (display rate, setting) */
        double cp[2] = { g_ia.c_pass[0], g_ia.c_pass[1] }, cr = g_ia.c_pair;
        if (g_ia.gen)                   /* step 82: a restart, logged (it was silent) */
            fprintf(stderr, "[D3DB] host interpolation: a new frame-rate plan from guest swap %u: the adaptive rate"
                    " starts again at %.2f fps (was %.2f)\n", swap_no, fp_step_fps(0), g_ia.fps);
        memset(&g_ia, 0, sizeof(g_ia));
        g_ia.c_pass[0] = cp[0];
        g_ia.c_pass[1] = cp[1];
        g_ia.c_pair = cr;
        g_ia.gen = g_fp.gen;
        g_ia.k = 0;
        g_ia.fps = fp_step_fps(0);
        g_ia.t_ms = 1000.0 * g_fp.span / g_fp.slot_hz;
        g_ia.hold = IA_HOLD0;
        g_ia.last = q.QuadPart;
        g_ia.rp0 = t_rt ? g_rt_take.QuadPart : q.QuadPart;
        g_ia.changed = q.QuadPart;
        ia_mark();
        g_iadapt_seen = g_iadapt;
        return;
    }
    if (g_iadapt != g_iadapt_seen) {    /* step 75: the setting changed: from the display's count, judged afresh */
        g_iadapt_seen = g_iadapt;
        fprintf(stderr, "[D3DB] host interpolation: adaptive frame rate %s (the setting)\n", g_iadapt ? "on" : "off");
        if (g_ia.k)
            ia_change(0, q.QuadPart, swap_no, "the adaptive frame rate setting changed");
        else {
            memset(&g_ia.w, 0, sizeof(g_ia.w));
            memset(&g_ia.u, 0, sizeof(g_ia.u));
            g_ia.wi = 0;
            g_ia.settle = IA_SETTLE;
            g_ia.changed = q.QuadPart;
        }
        g_ia.rose = 0;
        g_ia.hold = IA_HOLD0;
        return;
    }
    if (!used || !cmds)
        return;                         /* not measured: the overlay held the game, nothing recorded */
    if (dt > 4.0 * g_ia.t_ms)
        return;                         /* a stall (a load): not the sub-frames' cost */
    cur = g_ia.k;
    g_st.interp_adapt_frames++;
    g_st.interp_adapt_dt += dt;
    g_st.interp_adapt_busy += busy;
    g_st.interp_adapt_game += game_ms;  /* step 78 */
    g_st.interp_adapt_replay += replay_ms;
    if (!g_iadapt || g_ifixed)
        return;                         /* measured only */
    if (g_ia.settle) {
        g_ia.settle--;                  /* the queue settling after a change */
        return;
    }
    since = (double)(q.QuadPart - g_ia.changed) / (double)g_qpf.QuadPart;
    if (g_ia.rose && g_ia.rose == g_ia.changed && since > IA_KEPT)
        g_ia.hold = IA_HOLD0;           /* the last rise held */
    ia_add(&g_ia.w, dt, busy, cmds, game_ms, replay_ms);
    g_ia.wi += used > 1 || paired;
    if (g_ia.w.n >= IA_WIN) {
        double mdt = g_ia.w.dt / g_ia.w.n, mb = g_ia.w.busy / g_ia.w.n, mc = g_ia.w.cmds / g_ia.w.n;
        kn = ia_next(cur, 0, &g_ia.w, mc);
        /* the work dropped saves at least half the overrun (step 79: of the larger side's: none when the
         * game side is the slower, whose time no sub-frame count changes; step 82: the larger of the two
         * means before against after, both from ia_busy: mb, the mean of each frame's larger side, is
         * never below the larger mean, so noise passed for a saving) */
        double save = g_ia_thr ? ia_busy(&g_ia.w, 0) - ia_busy(&g_ia.w, ia_cost(cur, kn, mc)) : -ia_cost(cur, kn, mc);
        if (g_ia_thr && g_ia.w.game >= g_ia.w.replay)
            save = 0;                   /* the game side the slower: nothing to save */
        if (kn != cur && mdt > g_ia.t_ms * IA_LATE && 2 * g_ia.wi >= g_ia.w.n && mb > g_ia.t_ms * IA_BUSY &&
            save >= 0.5 * (mdt - g_ia.t_ms)) {
            int failed = g_ia.rose && g_ia.rose == g_ia.changed && since < IA_FAIL;
            if (failed)
                g_ia.hold = g_ia.hold * 2 > IA_HOLD_MAX ? IA_HOLD_MAX : g_ia.hold * 2;
            snprintf(why, sizeof(why), "game frames ran late, %.1f fps over the last %u (%.2f ms a frame against"
                     " %.2f, %.2f ms of it busy; %.0f commands, an interpolated pass %.2f ms)%s", 1000.0 / mdt,
                     g_ia.w.n, mdt, g_ia.t_ms, mb, mc, mc * g_ia.c_pass[1],
                     failed ? "; the next rise after a longer hold" : "");
            ia_change(kn, q.QuadPart, swap_no, why);
            return;
        }
        memset(&g_ia.w, 0, sizeof(g_ia.w));
        g_ia.wi = 0;
    }
    if (cur > 0) {
        ia_add(&g_ia.u, dt, busy, cmds, game_ms, replay_ms);
        if (g_ia.u.n >= IA_UP) {
            double mdt = g_ia.u.dt / g_ia.u.n, mb = g_ia.u.busy / g_ia.u.n, mc = g_ia.u.cmds / g_ia.u.n, want;
            kn = ia_next(cur, 1, &g_ia.u, mc);
            want = ia_busy(&g_ia.u, ia_cost(cur, kn, mc) * IA_PASS_X);
            if (kn != cur && since >= g_ia.hold && mdt <= g_ia.t_ms * 1.01) {
                snprintf(why, sizeof(why), "room for them, %.2f ms busy a frame over the last %u, ~%.2f with %.4g"
                         " more (%.0f commands, an interpolated pass %.2f ms) of %.2f", mb, g_ia.u.n, want,
                         fp_step_per(kn) - fp_step_per(cur), mc, mc * g_ia.c_pass[1], g_ia.t_ms);
                ia_change(kn, q.QuadPart, swap_no, why);
                return;
            }
            memset(&g_ia.u, 0, sizeof(g_ia.u));
        }
    }
}

/* RECOMP_D3D_BRIDGE_INTERP_REC (a check): each recorded draw of this frame,
 * as interpolation sees it, appended to the file. */
static void ic_rec_dump(unsigned swap_no)
{
    char go[MAX_PATH + 8];
    FILE *f;
    unsigned i, cur = (unsigned)g_ic;

    snprintf(go, sizeof(go), "%s.go", g_irec_trigger);
    if (!g_irec_left && GetFileAttributesA(go) != INVALID_FILE_ATTRIBUTES) {
        FILE *g = fopen(go, "r");
        if (g) {
            if (fscanf(g, "%u", &g_irec_left) != 1)
                g_irec_left = 2;
            fclose(g);
        }
        DeleteFileA(go);
    }
    g_irec_this = 0;
    if (!g_irec_left)
        return;
    g_irec_left--;
    g_irec_this = 1;                    /* step 68: the pair log follows the recorded frames */
    f = fopen(g_irec_trigger, "ab");
    if (!f)
        return;
    {
        uint32_t h[4] = { 0x46524543u, swap_no, g_ps->rc_n, g_icam_n[cur] };
        fwrite(h, 4, 4, f);
        for (i = 0; i < g_icam_n[cur]; i++) {
            uint32_t ci[2] = { g_icam[cur][i].draws, (uint32_t)g_icam[cur][i].p_ok | (uint32_t)g_icam[cur][i].rot_ok << 1 };
            fwrite(g_icam[cur][i].m, 4, 32, f);
            fwrite(ci, 4, 2, f);
        }
    }
    for (i = 0; i < g_ps->rc_n; i++) {
        const rcmd_t *c = &g_ps->rc[i];
        uint32_t r[12];
        static const float zero[768];
        const d3dh_draw_t *d = &c->u.draw.d;
        memset(r, 0, sizeof(r));
        r[0] = (uint32_t)c->kind;
        if (c->kind == RC_DRAW) {
            int slot = d->vprog ? vs_find(d->vprog->key) : -1;
            r[1] = c->u.draw.cls;
            r[2] = c->u.draw.cam;
            r[3] = c->u.draw.n_cpos;
            r[4] = d->target.data;
            r[5] = d->target.width | d->target.height << 16;
            r[6] = d->fvf;
            r[7] = d->vprog ? d->vprog->key : 0;
            r[8] = (d->ff != NULL) | (d->vprog != NULL) << 1 | d->pillar << 2 | (d->camera != NULL) << 3;
            r[9] = d->vert_count;
            r[10] = d->prim_count;
            r[11] = d->mesh;                  /* step 62 */
            if (d->vprog) {                   /* step 67: bits 8-29, the bones uploaded for it */
                unsigned k, q;
                for (k = 0; k < IC_BONE_REGS / 3; k++)
                    for (q = IC_BONE_BASE + 3 * k; q < IC_BONE_BASE + 3 * k + 3; q++)
                        if (d->vprog->fresh[q >> 5] >> (q & 31) & 1)
                            r[8] |= 1u << (8 + k);
            }
            fwrite(r, 4, 12, f);
            fwrite(d->camera ? d->camera : zero, 4, 32, f);
            fwrite(d->ff ? d->ff->world : zero, 4, 16, f);
            fwrite(d->ff ? d->ff->view : zero, 4, 16, f);
            fwrite(d->ff ? d->ff->proj : zero, 4, 16, f);
            fwrite(d->vprog ? d->vprog->consts : zero, 4, 768, f);
            fwrite(slot >= 0 ? g_vs[slot].cmask : (const uint32_t *)zero, 4, 6, f);
        } else {
            fwrite(r, 4, 12, f);
        }
    }
    fclose(f);
}

static int cmp_iord(const void *a, const void *b)
{
    unsigned i = *(const unsigned *)a, j = *(const unsigned *)b;
    uint32_t x = g_iord_of[i].key, y = g_iord_of[j].key;
    return x < y ? -1 : x > y ? 1 : (i > j) - (i < j);
}

/* Step 63: how far apart two objects of one key are: W's origins, plus
 * for skinned meshes the mean over the bones both may move (squared). The
 * first bone alone left identical meshes drawn twice with nearly the same
 * root (a model and its reflection, fur passes) to a near tie. */
static double ic_dist(const iobj_t *o, const iobj_t *p)
{
    double dd = 0, bs = 0;
    unsigned b, nb = 0;
    int i;

    for (i = 0; i < 3; i++)
        dd += (o->w[12 + i] - p->w[12 + i]) * (o->w[12 + i] - p->w[12 + i]);
    if (o->bones >= 0 && p->bones >= 0) {
        const float *x = g_ibone[g_ic] + o->bones, *y = g_ibone[g_ip] + p->bones;
        for (b = 0; b < IC_BONE_REGS / 3; b++) {
            if (!((o->bmask & p->bmask) >> b & 1))
                continue;
            for (i = 0; i < 3; i++) {
                double d = (double)x[12 * b + 4 * i + 3] - y[12 * b + 4 * i + 3];
                bs += d * d;
            }
            nb++;
        }
        if (nb)
            dd += bs / nb;
    }
    return dd;
}

/* Step 63: the frame before's unskinned objects by W's origin x. */
static unsigned *g_ixord;
static unsigned g_ixord_cap;

static int cmp_ixord(const void *a, const void *b)
{
    double x = g_iord_of[*(const unsigned *)a].w[12], y = g_iord_of[*(const unsigned *)b].w[12];
    return x < y ? -1 : x > y;
}

/* Step 63: whether the frame before had an object of this key (g_iord
 * sorted by key). */
static int ic_kind_before(uint32_t key)
{
    const iobj_t *po = g_iord_of;
    unsigned lo = 0, hi = g_iobj_n[g_ip];

    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (po[g_iord[mid]].key < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < g_iobj_n[g_ip] && po[g_iord[lo]].key == key;
}

/* Step 63: an unskinned object where the frame before had one of another
 * kind (within half its size, W's first axis), with none of its own kind
 * within twice that, replaced that one in place: a pause-menu line taking or losing
 * the highlight's colour (another texture: another key), a HUD digit
 * changing. Paired by kind, each letter took the same glyph in the line
 * the highlight left and slid a line across; it is drawn as this frame has
 * it (marked used 2), and the one it replaced is nobody's partner. The one
 * replaced may already be another's (a letter and its drop shadow sit
 * almost on one place, and change together). Only when the frame before
 * had one of its kind somewhere (else it is "none of its kind" anyway, and
 * meshes built in world space, W = I, all sit at the origin).
 * Skinned meshes (W = I, the motion in the bones) don't take part. Called
 * from ic_pair with the frame before's objects unused. */
static void ic_replaced(void)
{
    unsigned cur = (unsigned)g_ic, prv = (unsigned)g_ip, n = g_iobj_n[prv], m = 0, i, j;
    iobj_t *po = g_iobj[prv];

    if (n > g_ixord_cap) {
        unsigned *o = (unsigned *)realloc(g_ixord, n * sizeof(unsigned));
        if (!o)
            return;
        g_ixord = o;
        g_ixord_cap = n;
    }
    for (i = 0; i < n; i++)
        if (po[i].bones < 0)
            g_ixord[m++] = i;
    if (!m)
        return;
    qsort(g_ixord, m, sizeof(unsigned), cmp_ixord);
    for (i = 0; i < g_iobj_n[cur]; i++) {
        iobj_t *o = &g_iobj[cur][i];
        double tol = 0.5 * sqrt(o->w[0] * o->w[0] + o->w[1] * o->w[1] + o->w[2] * o->w[2]);
        double d_other = tol * tol, d_same = 1e300;
        unsigned lo = 0, hi = m, other = (unsigned)-1;
        if (o->bones >= 0 || !(tol > 0))
            continue;
        while (lo < hi) {
            unsigned mid = (lo + hi) / 2;
            if (po[g_ixord[mid]].w[12] < o->w[12] - tol)
                lo = mid + 1;
            else
                hi = mid;
        }
        for (j = lo; j < m && po[g_ixord[j]].w[12] <= o->w[12] + tol; j++) {
            const iobj_t *p = &po[g_ixord[j]];
            double dx = o->w[12] - p->w[12], dy = o->w[13] - p->w[13], dz = o->w[14] - p->w[14];
            double dd = dx * dx + dy * dy + dz * dz;
            if (p->key == o->key) {
                if (dd < d_same)
                    d_same = dd;
            } else if (dd < d_other) {  /* taken or not: a letter and its drop shadow both replace theirs */
                d_other = dd;
                other = g_ixord[j];
            }
        }
        if (other != (unsigned)-1 && 4 * d_other < d_same && d_same > 0.04 * tol * tol && ic_kind_before(o->key)) {
            o->used = 2;
            po[other].used = 1;
            g_st.interp_obj_swap++;
        }
    }
}

/* Step 66 (notes 18.69): skeletons. Bones slerped one by one, each about
 * its own origin (where it takes the model's origin), stop agreeing where
 * two of them meet once they turn far in one tick: Coco's arms at the
 * start of a jump (the jump animation takes over, bones turn up to 98
 * degrees), her limbs in a spin (up to 179 degrees: each bone took its own
 * way round). Measured on recorded frames, the joints two bones share came
 * up to 0.2 units apart at the jump and 6 in a spin (0.002-0.03 otherwise).
 * Now a skinned object's bones are blended linearly in a frame G that moves
 * with its skeleton: B(t) = G(t) ((1 - t) Ga^-1 Ba + t Gb^-1 Bb), G(t)
 * slerped and lerped as a bone was. Linear in the bones, every point any
 * two bones share stays shared (a joint can't open, whatever G is); G takes
 * the body's turn, so only a limb's swing relative to it is blended
 * straight (a little shorter halfway through a big swing).
 * A skeleton: the skinned objects that hold one identical bone (a moving,
 * non-flat one: a value both frames have and that changed); G: the bone
 * most of them hold. A skeleton's draws hold its bones in different
 * registers, and unread registers hold earlier draws' leftovers, so a
 * group may take in another character: still consistent, as G is shared.
 * Flat bones (the stencil shadow's) take no part: one by one as before. */
typedef struct {
    double ga[16], gb[16], iga[16], igb[16], gt[16];
    unsigned stamp;                     /* the sub-frame gt is for */
    unsigned holder;                    /* step 68: the object whose bone G is */
    double step[3], sd, rd;             /* step 68: G's step, its square, and off the holder partner's step */
} igrp_t;
static igrp_t *g_igrp;
static unsigned g_igrp_n, g_igrp_cap, g_isub_stamp;
typedef struct {
    uint32_t h;                         /* 0: empty */
    uint32_t obj, bone, count;          /* the first object holding it, its bone; how many hold it */
} igval_t;
static igval_t *g_igval;
static unsigned g_igval_cap;
static unsigned *g_iuf;                 /* union-find over this frame's objects */
static unsigned g_iuf_cap;

static unsigned uf_find(unsigned x)
{
    while (g_iuf[x] != x) {
        g_iuf[x] = g_iuf[g_iuf[x]];
        x = g_iuf[x];
    }
    return x;
}

/* A bone (3 registers, rows of a 3x4) as a 4x4, row-major. */
static void bone_m4(const float *b, double *m)
{
    int i;
    for (i = 0; i < 12; i++)
        m[i] = b[i];
    m[12] = m[13] = m[14] = 0;
    m[15] = 1;
}

static double bone_det(const float *b)
{
    return (double)b[0] * ((double)b[5] * b[10] - (double)b[6] * b[9]) -
           (double)b[1] * ((double)b[4] * b[10] - (double)b[6] * b[8]) +
           (double)b[2] * ((double)b[4] * b[9] - (double)b[5] * b[8]);
}

/* A bone that may tie a skeleton together: moved, and neither frame's flat. */
static int ig_usable(const float *a, const float *b)
{
    double da = bone_det(a), db = bone_det(b);
    return memcmp(a, b, 12 * sizeof(float)) != 0 && fabs(da) > 1e-3 && fabs(db) > 1e-3;
}

static uint32_t ig_hash(const float *b)
{
    uint32_t h = 2166136261u, v;
    int i;
    for (i = 0; i < 12; i++) {
        memcpy(&v, &b[i], 4);
        h = fnv(h, v);
    }
    return h ? h : 1;
}

/* After pairing: this frame's paired skinned objects in skeletons, each with
 * its G. Called with g_lock held. */
static void ic_groups(void)
{
    unsigned cur = (unsigned)g_ic, prv = (unsigned)g_ip, n = g_iobj_n[cur], nv = 0, cap, i, b, s;
    iobj_t *ob = g_iobj[cur];

    g_igrp_n = 0;
    if (!g_iskel)
        return;
    for (i = 0; i < n; i++)
        if (ob[i].bones >= 0 && ob[i].prev >= 0 && g_iobj[prv][ob[i].prev].bones >= 0)
            nv += IC_BONE_REGS / 3;
    if (!nv)
        return;
    for (cap = 64; cap < 2 * nv; cap *= 2)
        ;
    if (cap > g_igval_cap) {
        igval_t *v = (igval_t *)realloc(g_igval, cap * sizeof(igval_t));
        if (!v)
            return;
        g_igval = v;
        g_igval_cap = cap;
    }
    if (n > g_iuf_cap) {
        unsigned *u = (unsigned *)realloc(g_iuf, n * sizeof(unsigned));
        if (!u)
            return;
        g_iuf = u;
        g_iuf_cap = n;
    }
    memset(g_igval, 0, cap * sizeof(igval_t));
    for (i = 0; i < n; i++)
        g_iuf[i] = i;
    for (i = 0; i < n; i++) {           /* each usable bone value: held before? then the same skeleton */
        const iobj_t *o = &ob[i], *p;
        if (o->bones < 0 || o->prev < 0)
            continue;
        p = &g_iobj[prv][o->prev];
        if (p->bones < 0 || !o->own || !p->own)     /* step 67: leftovers unknown, no skeleton */
            continue;
        for (b = 0; b < IC_BONE_REGS / 3; b++) {
            const float *vb = g_ibone[cur] + o->bones + 12 * b, *va = g_ibone[prv] + p->bones + 12 * b;
            uint32_t h;
            if (!((o->bmask & p->bmask) >> b & 1) || !ig_usable(va, vb))
                continue;
            h = ig_hash(vb);
            for (s = h & (cap - 1);; s = (s + 1) & (cap - 1)) {
                igval_t *e = &g_igval[s];
                if (!e->h) {
                    e->h = h;
                    e->obj = i;
                    e->bone = b;
                    e->count = 1;
                    break;
                }
                if (e->h == h && memcmp(g_ibone[cur] + ob[e->obj].bones + 12 * e->bone, vb, 12 * sizeof(float)) == 0) {
                    unsigned x = uf_find(i), y = uf_find(e->obj);
                    if (x != y)
                        g_iuf[x] = y;
                    if (e->obj != i)
                        e->count++;
                    break;
                }
            }
        }
    }
    for (i = 0; i < n; i++)             /* the skeletons, by their root object */
        ob[i].grp = -1;
    for (s = 0; s < cap; s++) {         /* each skeleton's G: the bone most of its objects hold */
        igval_t *e = &g_igval[s];
        unsigned r;
        if (!e->h)
            continue;
        r = uf_find(e->obj);
        if (ob[r].grp < 0 || e->count > g_igval[ob[r].grp].count)
            ob[r].grp = (int32_t)s;     /* for now: the slot of its G */
    }
    for (i = 0; i < n; i++) {
        unsigned r = uf_find(i);
        if (r == i && ob[i].grp >= 0) {
            const igval_t *e = &g_igval[ob[i].grp];
            const iobj_t *o = &ob[e->obj], *p = &g_iobj[prv][o->prev];
            igrp_t *gr;
            if (g_igrp_n == g_igrp_cap) {
                unsigned c2 = g_igrp_cap ? 2 * g_igrp_cap : 16;
                igrp_t *x = (igrp_t *)realloc(g_igrp, c2 * sizeof(igrp_t));
                if (!x) {
                    ob[i].grp = -1;
                    continue;
                }
                g_igrp = x;
                g_igrp_cap = c2;
            }
            gr = &g_igrp[g_igrp_n];
            gr->holder = e->obj;
            bone_m4(g_ibone[prv] + p->bones + 12 * e->bone, gr->ga);
            bone_m4(g_ibone[cur] + o->bones + 12 * e->bone, gr->gb);
            gr->stamp = 0;
            if (m4_inv(gr->ga, gr->iga) != 0 || m4_inv(gr->gb, gr->igb) != 0) {
                ob[i].grp = -1;
                continue;
            }
            ob[i].grp = (int32_t)(g_igrp_n++ | 0x40000000);   /* marked: a skeleton's index */
        }
    }
    for (i = 0; i < n; i++) {           /* every object its root's skeleton */
        unsigned r = uf_find(i);
        int32_t g = ob[r].grp;
        if (r != i)
            ob[i].grp = g >= 0 && (g & 0x40000000) ? g : -1;
    }
    for (i = 0; i < n; i++) {
        if (ob[i].grp >= 0 && (ob[i].grp & 0x40000000))
            ob[i].grp &= ~0x40000000;
        else
            ob[i].grp = -1;
    }
    g_st.interp_skel += g_igrp_n;
}

/* Step 66: a skeleton's G at this sub-frame's t, made once a sub-frame. */
static const double *ic_group_g(unsigned gi)
{
    igrp_t *gr = &g_igrp[gi];

    if (gr->stamp != g_isub_stamp) {
        aff_t a, b, r;
        int i, j;
        for (i = 0; i < 3; i++) {       /* axis j is column j */
            for (j = 0; j < 3; j++) {
                a.r[3 * j + i] = gr->ga[4 * i + j];
                b.r[3 * j + i] = gr->gb[4 * i + j];
            }
            a.t[i] = gr->ga[4 * i + 3];
            b.t[i] = gr->gb[4 * i + 3];
        }
        aff_lerp(&a, &b, g_it, &r);
        for (i = 0; i < 3; i++) {
            for (j = 0; j < 3; j++)
                gr->gt[4 * i + j] = r.r[3 * j + i];
            gr->gt[4 * i + 3] = r.t[i];
        }
        gr->gt[12] = gr->gt[13] = gr->gt[14] = 0;
        gr->gt[15] = 1;
        gr->stamp = g_isub_stamp;
    }
    return gr->gt;
}

/* Step 68 (notes 18.71): pairing a key's objects together. Step 62 paired
 * them one at a time in draw order, each with the nearest unpaired one: a
 * wumpa fruit just come into view at the screen's edge (culled the frame
 * before) could be drawn first and take its neighbour's partner, a fruit
 * spacing away and under the snap, and came out of that fruit and slid into
 * place (Bamboozled, Medieval Madness); the neighbour, its own taken, snapped
 * (step 63). Now every candidate pair of a key (each object's IC_CANDS
 * nearest of the frame before) is taken nearest first, so each keeps its
 * own and the one that wasn't there before is left with none: new, drawn
 * where it is. */
#define IC_CANDS 4
typedef struct {
    double dd;
    unsigned rd;                        /* ties: how far apart their places in the key's draw order are */
    unsigned c, p;                      /* this frame's object, the frame before's */
} icand_t;
static icand_t *g_icand;
static unsigned g_icand_cap;
static unsigned *g_icord;               /* this frame's objects by key */
static unsigned g_icord_cap;
static unsigned *g_iord2;               /* the frame before's by loose key */
static unsigned g_iord2_cap;
static int g_iloose;                    /* the pass: by key (0) or loose key (1) */
static uint8_t *g_ipick_loose;          /* each of this frame's objects: paired by its loose key */
static unsigned g_ipick_loose_cap;
static int32_t *g_ipick;                /* each of this frame's objects: its partner, -1 none */
static double *g_ipick_dd, *g_inear;    /* ... its distance (ic_dist), the nearest of its key's, taken or not */
static int32_t *g_inear_i;
static unsigned g_ipick_cap;
static const iobj_t *g_icord_of;

static uint32_t ic_mkey(const iobj_t *o)
{
    return g_iloose ? o->lkey : o->key;
}

static int cmp_icord(const void *a, const void *b)
{
    unsigned i = *(const unsigned *)a, j = *(const unsigned *)b;
    uint32_t x = ic_mkey(&g_icord_of[i]), y = ic_mkey(&g_icord_of[j]);
    return x < y ? -1 : x > y ? 1 : (i > j) - (i < j);
}

static int cmp_icand(const void *a, const void *b)
{
    const icand_t *x = (const icand_t *)a, *y = (const icand_t *)b;
    if (x->dd != y->dd)
        return x->dd < y->dd ? -1 : 1;
    if (x->rd != y->rd)
        return x->rd < y->rd ? -1 : 1;
    return x->c != y->c ? (x->c > y->c) - (x->c < y->c) : (x->p > y->p) - (x->p < y->p);
}

static int ic_grow(void **p, unsigned *cap, unsigned need, size_t sz)
{
    void *n;
    unsigned c = *cap ? *cap : 256;

    if (need <= *cap)
        return 1;
    while (c < need)
        c *= 2;
    n = realloc(*p, (size_t)c * sz);
    if (!n)
        return 0;
    *p = n;
    *cap = c;
    return 1;
}

/* One pass of ic_match, key by key (ic_mkey): [a, b) this frame's objects
 * of a key in g_icord (nc of them), [pa, pb) the frame before's in `pord`
 * (np). 0 if out of memory. */
static int ic_match_keys(unsigned nc, const unsigned *pord, unsigned np)
{
    unsigned cur = (unsigned)g_ic, prv = (unsigned)g_ip, i, a, b, pa;
    iobj_t *co = g_iobj[cur], *po = g_iobj[prv];

    for (a = 0, pa = 0; a < nc; a = b) {
        unsigned pb, round;
        uint32_t key = ic_mkey(&co[g_icord[a]]);
        for (b = a; b < nc && ic_mkey(&co[g_icord[b]]) == key; b++)
            ;
        while (pa < np && ic_mkey(&po[pord[pa]]) < key)
            pa++;
        for (pb = pa; pb < np && ic_mkey(&po[pord[pb]]) == key; pb++)
            ;
        if (pb == pa)
            continue;
        if (!ic_grow((void **)&g_icand, &g_icand_cap, (b - a) * IC_CANDS, sizeof(icand_t)))
            return 0;
        /* Rounds: the objects left without one propose again among those
         * left (exact ties, e.g. meshes built in world space, all at the
         * origin, go by their places in the draw order). */
        for (round = 0; round < 16; round++) {
            unsigned nn = 0, got = 0, k;
            for (i = a; i < b; i++) {   /* each object's IC_CANDS nearest unused of the frame before */
                unsigned c = g_icord[i], j, m = 0;
                icand_t best[IC_CANDS];
                if (co[c].used == 2 || g_ipick[c] >= 0)     /* step 63: replaced another in place; or paired */
                    continue;
                for (j = pa; j < pb; j++) {
                    unsigned p = pord[j], rd = i - a > j - pa ? (i - a) - (j - pa) : (j - pa) - (i - a);
                    double dd = ic_dist(&co[c], &po[p]);
                    if (!round && !g_iloose && dd < g_inear[c]) {   /* step 63: the nearest, taken or not */
                        g_inear[c] = dd;
                        g_inear_i[c] = (int32_t)p;
                    }
                    if (po[p].used)
                        continue;
                    if (m < IC_CANDS || dd < best[m - 1].dd || (dd == best[m - 1].dd && rd < best[m - 1].rd)) {
                        unsigned q = m < IC_CANDS ? m++ : m - 1;
                        while (q > 0 && (best[q - 1].dd > dd || (best[q - 1].dd == dd && best[q - 1].rd > rd))) {
                            best[q] = best[q - 1];
                            q--;
                        }
                        best[q].dd = dd;
                        best[q].rd = rd;
                        best[q].c = c;
                        best[q].p = p;
                    }
                }
                for (k = 0; k < m; k++)
                    g_icand[nn++] = best[k];
            }
            if (!nn)
                break;
            qsort(g_icand, nn, sizeof(icand_t), cmp_icand);
            for (k = 0; k < nn; k++) {  /* nearest first */
                icand_t *x = &g_icand[k];
                if (g_ipick[x->c] >= 0 || po[x->p].used)
                    continue;
                g_ipick[x->c] = (int32_t)x->p;
                g_ipick_dd[x->c] = x->dd;
                g_ipick_loose[x->c] = (uint8_t)g_iloose;
                po[x->p].used = 3;      /* matched; marked 1 once it is the partner */
                got++;
            }
            if (!got)
                break;
        }
        pa = pb;
    }
    return 1;
}

/* Each of this frame's objects' partner (g_ipick), its distance and its
 * nearest of the frame before's (g_inear): a key's candidate pairs nearest
 * first. Step 68: then those left by their loose key (lkey: their textures
 * by shape), so an object whose texture is a flipbook (H2 Oh No's caustics
 * on the sub's missile, the clock, the fruit: a texture each frame, so a
 * key each frame, never paired) is paired with its last frame's. 0 if out
 * of memory. */
static int ic_match(void)
{
    unsigned cur = (unsigned)g_ic, prv = (unsigned)g_ip, nc = g_iobj_n[cur], np = g_iobj_n[prv], i;
    iobj_t *co = g_iobj[cur], *po = g_iobj[prv];
    int ok = 1;

    if (!ic_grow((void **)&g_icord, &g_icord_cap, nc, sizeof(unsigned)) ||
        !ic_grow((void **)&g_iord2, &g_iord2_cap, np, sizeof(unsigned)) ||
        !ic_grow((void **)&g_ipick_loose, &g_ipick_loose_cap, nc, 1))
        return 0;
    if (nc > g_ipick_cap) {             /* four arrays of one capacity */
        unsigned c = g_ipick_cap ? g_ipick_cap : 256;
        void *x, *y, *w, *z;
        while (c < nc)
            c *= 2;
        if ((x = realloc(g_ipick, c * sizeof(int32_t))) != NULL)
            g_ipick = (int32_t *)x;
        if ((y = realloc(g_ipick_dd, c * sizeof(double))) != NULL)
            g_ipick_dd = (double *)y;
        if ((w = realloc(g_inear, c * sizeof(double))) != NULL)
            g_inear = (double *)w;
        if ((z = realloc(g_inear_i, c * sizeof(int32_t))) != NULL)
            g_inear_i = (int32_t *)z;
        if (!x || !y || !w || !z)
            return 0;
        g_ipick_cap = c;
    }
    for (i = 0; i < nc; i++) {
        g_icord[i] = i;
        g_ipick[i] = -1;
        g_ipick_dd[i] = 1e300;
        g_inear[i] = 1e300;
        g_inear_i[i] = -1;
        g_ipick_loose[i] = 0;
    }
    g_iloose = 0;
    g_icord_of = co;
    qsort(g_icord, nc, sizeof(unsigned), cmp_icord);
    ok = ic_match_keys(nc, g_iord, np);
    if (ok) {                           /* those left on both sides, by loose key */
        unsigned nc2 = 0, np2 = 0;
        for (i = 0; i < nc; i++)
            if (g_ipick[i] < 0 && co[i].used != 2)
                g_icord[nc2++] = i;
        for (i = 0; i < np; i++)
            if (!po[i].used)
                g_iord2[np2++] = i;
        if (nc2 && np2) {
            g_iloose = 1;
            g_icord_of = po;
            qsort(g_iord2, np2, sizeof(unsigned), cmp_icord);
            g_icord_of = co;
            qsort(g_icord, nc2, sizeof(unsigned), cmp_icord);
            ok = ic_match_keys(nc2, g_iord2, np2);
        }
    }
    g_iloose = 0;
    for (i = 0; i < np; i++)
        if (po[i].used == 3)
            po[i].used = 0;
    return ok;
}

/* Step 62: each object of this frame paired with the frame before's
 * nearest unpaired one of its key (step 68: ic_match's, else in draw
 * order); none within g_isnap: it stays where it is. Called with g_lock
 * held. */
static void ic_pair(void)
{
    unsigned cur = (unsigned)g_ic, prv = (unsigned)g_ip, n = g_iobj_n[prv], i, j;
    iobj_t *po = g_iobj[prv], *co = g_iobj[cur];
    int matched;
    LARGE_INTEGER t0, t1;

    REPLAY_SIDE();
    if (!g_iobj_n[cur])
        return;
    QueryPerformanceCounter(&t0);
    if (n > g_iord_cap) {
        unsigned *o = (unsigned *)realloc(g_iord, n * sizeof(unsigned));
        if (!o)
            return;
        g_iord = o;
        g_iord_cap = n;
    }
    for (i = 0; i < n; i++) {
        g_iord[i] = i;
        po[i].used = 0;
    }
    g_iord_of = po;
    qsort(g_iord, n, sizeof(unsigned), cmp_iord);
    ic_replaced();                      /* step 63 */
    matched = g_imatch && ic_match();
    for (i = 0; i < g_iobj_n[cur]; i++) {
        iobj_t *o = &co[i];
        unsigned lo = 0, hi = n, best = (unsigned)-1, a1i = (unsigned)-1;
        double bd = 1e300, a1 = 1e300, rel;
        o->lbest = -1;
        o->lwhat = "replaced";
        if (o->used == 2) {             /* step 63: replaced another in place, drawn as it is */
            g_st.interp_obj++;
            continue;
        }
        if (matched) {
            best = g_ipick[i] >= 0 ? (unsigned)g_ipick[i] : (unsigned)-1;
            bd = g_ipick_dd[i];
            a1 = g_inear[i];
            a1i = g_inear_i[i] >= 0 ? (unsigned)g_inear_i[i] : (unsigned)-1;
        } else {
            while (lo < hi) {
                unsigned mid = (lo + hi) / 2;
                if (po[g_iord[mid]].key < o->key)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            for (j = lo; j < n && po[g_iord[j]].key == o->key; j++) {
                const iobj_t *p = &po[g_iord[j]];
                double dd = ic_dist(o, p);
                if (dd < a1) {          /* step 63: the nearest, taken or not */
                    a1 = dd;
                    a1i = g_iord[j];
                }
                if (p->used)
                    continue;
                if (dd < bd) {
                    bd = dd;
                    best = g_iord[j];
                }
            }
        }
        g_st.interp_obj++;
        o->lnear = a1 < 1e299 ? (float)sqrt(a1) : -1.0f;
        if (best == (unsigned)-1) {
            g_st.interp_obj_new++;
            o->lwhat = "new";
            continue;
        }
        o->lbest = (int32_t)best;
        for (j = 0; j < 3; j++)         /* the snap goes by W's origin and the first bone's, as before */
            o->vel[j] = o->pos[j] - po[best].pos[j];
        bd = o->vel[0] * o->vel[0] + o->vel[1] * o->vel[1] + o->vel[2] * o->vel[2];
        /* Step 63: a partner over half the object's own size (W's first
         * axis) away, taken because one of its kind under half as far was
         * already another's, is a guess: the pause menu's highlighted line
         * pulses and changes colour together, and its letters took the
         * same glyph two to four letters off. It snaps. Skinned meshes
         * (W = I) keep the snap alone. */
        rel = 1e300;
        if (o->bones < 0 && a1i != best && 4 * a1 < bd)
            rel = 0.25 * (o->w[0] * o->w[0] + o->w[1] * o->w[1] + o->w[2] * o->w[2]);
        if (bd > rel) {
            g_st.interp_obj_snap++;
            o->lwhat = "snap-guess";
            o->lstep = o->loff = (float)sqrt(bd);
            continue;
        }
        if (!g_imatch && bd > g_isnap * g_isnap) {     /* step 62's snap, as before */
            g_st.interp_obj_snap++;
            o->lwhat = "snap";
            o->lstep = o->loff = (float)sqrt(bd);
            continue;
        }
        o->hv = 1;                      /* step 68: its partner known: its step (vel) is the motion to expect next */
        o->prev = (int32_t)best;        /* for now; step 68: a skeleton's snap is decided below, whole */
        po[best].used = 1;
        o->lwhat = matched && g_ipick_loose[i] ? "pair-texture" : "pair";
    }
    ic_groups();                        /* step 66 */
    /* Step 68: the snap. A step over g_isnap snaps only if it is a jump from
     * the object's own motion too: from where it would be had it gone on as
     * in the frame before (its partner's vel). A projectile flying on, the
     * ball rolling fast, are interpolated; a teleport or a respawn snaps as
     * before. A skeleton (step 66) snaps whole, by its frame G's step: by
     * their first bones (often a limb's), some of Crash's draws took a 1.7-2
     * unit step every fourth frame rolling fast in Medieval Madness, snapped
     * while the rest were interpolated, and came apart from the ball for
     * those sub-frames. */
    for (i = 0; i < g_igrp_n; i++) {
        igrp_t *gr = &g_igrp[i];
        const iobj_t *p = &po[co[gr->holder].prev];
        double ex, ey, ez;
        gr->step[0] = gr->gb[3] - gr->ga[3];
        gr->step[1] = gr->gb[7] - gr->ga[7];
        gr->step[2] = gr->gb[11] - gr->ga[11];
        ex = gr->step[0] - (p->hv ? p->vel[0] : 0.0);
        ey = gr->step[1] - (p->hv ? p->vel[1] : 0.0);
        ez = gr->step[2] - (p->hv ? p->vel[2] : 0.0);
        gr->sd = gr->step[0] * gr->step[0] + gr->step[1] * gr->step[1] + gr->step[2] * gr->step[2];
        gr->rd = p->hv ? ex * ex + ey * ey + ez * ez : gr->sd;
    }
    for (i = 0; i < g_iobj_n[cur]; i++) {
        iobj_t *o = &co[i];
        const iobj_t *p;
        double sd, rd, ex, ey, ez;
        if (o->prev < 0 || !g_imatch)
            continue;
        p = &po[o->prev];
        if (o->grp >= 0) {              /* its skeleton's step */
            const igrp_t *gr = &g_igrp[o->grp];
            memcpy(o->vel, gr->step, sizeof(o->vel));
            sd = gr->sd;
            rd = gr->rd;
        } else {
            ex = o->vel[0] - (p->hv ? p->vel[0] : 0.0);
            ey = o->vel[1] - (p->hv ? p->vel[1] : 0.0);
            ez = o->vel[2] - (p->hv ? p->vel[2] : 0.0);
            sd = o->vel[0] * o->vel[0] + o->vel[1] * o->vel[1] + o->vel[2] * o->vel[2];
            rd = p->hv ? ex * ex + ey * ey + ez * ez : sd;
        }
        o->lstep = (float)sqrt(sd);
        o->loff = (float)sqrt(rd);
        if (sd > g_isnap * g_isnap && rd > g_isnap * g_isnap) {
            g_st.interp_obj_snap++;
            o->lwhat = "snap";
            o->prev = -1;
            o->grp = -1;
            continue;
        }
        if (strcmp(o->lwhat, "pair-texture") == 0)
            g_st.interp_obj_loose++;
        if (sd > g_isnap * g_isnap) {
            g_st.interp_obj_motion++;
            o->lwhat = "pair-motion";
        }
    }
    QueryPerformanceCounter(&t1);
    g_st.pair_ms += (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)g_qpf.QuadPart;
    if (g_ipairlog && g_irec_this)      /* RECOMP_D3D_BRIDGE_PAIRLOG=1: each recorded frame's objects */
        for (i = 0; i < g_iobj_n[cur]; i++) {
            const iobj_t *o = &co[i];
            fprintf(stderr, "[D3DB] pairlog swap %u obj %u key %08X (kind %08X mesh %08X) pos %.3f %.3f %.3f prev %d "
                    "step %.3f off its motion %.3f near %.3f size %.3f skeleton %d %s\n", g_ipair_swap, i, o->key,
                    o->kind, o->mesh, o->pos[0], o->pos[1], o->pos[2], o->lbest, o->lbest < 0 ? -1.0 : o->lstep,
                    o->lbest < 0 ? -1.0 : o->loff, o->lnear, sqrt(o->w[0] * o->w[0] + o->w[1] * o->w[1] + o->w[2] * o->w[2]),
                    o->grp, o->lwhat);
        }
}

/* At the present: how many sub-frames this frame gets (1: shown as it is),
 * the cameras paired. `ovl`: the overlay opens or holds the game this
 * present (one frame). Called with g_lock held. */
static int ic_begin(unsigned swap_no, int ovl, int need)
{
    static unsigned logged;
    unsigned cur = (unsigned)g_ic, prv = (unsigned)g_ip, i, j, mc = IC_NO_CAM, matched = 0;
    int used[IC_CAMS], k = ic_possible();
    double frac = 1.0, step = 0.0, turn = 0.0;
    const char *why = NULL;

    REPLAY_SIDE();
    if (g_irec_trigger[0])
        ic_rec_dump(swap_no);
    qsort(g_ikey[cur], g_ikey_n[cur], sizeof(uint32_t), cmp_u32);    /* the frame before's next */
    g_st.interp_per = k ? g_fp.span / g_fp.step[0] : 1.0;
    ic_log_possible(k);                 /* step 63 */
    if (!k || ovl) {
        g_st.interp_off++;              /* this pacing, or the overlay opening or holding the game */
        return 0;
    }
    if (!g_iprev_ok) {
        g_st.interp_cut_first++;
        return 0;
    }
    if (!need)
        return 0;                       /* step 74/77: one present, as recorded: nothing to pair or to cut */
    for (i = j = 0; i < g_ikey_n[cur] && j < g_ikey_n[prv];) {
        if (g_ikey[cur][i] == g_ikey[prv][j]) {
            matched++;
            i++;
            j++;
        } else if (g_ikey[cur][i] < g_ikey[prv][j]) {
            i++;
        } else {
            j++;
        }
    }
    if (g_ikey_n[cur])
        frac = (double)matched / g_ikey_n[cur];
    /* Only cameras 3D draws used take part: each frame starts with the
     * frame before's camera still in place (the render-target passes and 2D
     * draw under it), and paired by value it took the main camera's pair. */
    for (j = 0; j < g_icam_n[prv]; j++)
        used[j] = !g_icam[prv][j].draws;
    for (i = 0; i < g_icam_n[cur]; i++) {
        g_ipair[i] = g_icam[cur][i].draws ? -1 : -2;
        for (j = 0; j < g_icam_n[prv] && g_ipair[i] == -1; j++)
            if (!used[j] && memcmp(g_icam[cur][i].m, g_icam[prv][j].m, sizeof(g_icam[cur][i].m)) == 0) {
                g_ipair[i] = (int)j;
                used[j] = 1;
                break;
            }
        if (mc == IC_NO_CAM || g_icam[cur][i].draws > g_icam[cur][mc].draws)
            mc = i;
    }
    for (;;) {                          /* the rest: most used first, each to the nearest one left */
        unsigned bi = IC_NO_CAM, bj = IC_NO_CAM;
        double best = 1e30;
        for (i = 0; i < g_icam_n[cur]; i++)
            if (g_ipair[i] == -1 && (bi == IC_NO_CAM || g_icam[cur][i].draws > g_icam[cur][bi].draws))
                bi = i;
        if (bi == IC_NO_CAM)
            break;
        for (j = 0; j < g_icam_n[prv]; j++) {
            const float *x = g_icam[cur][bi].m + 12, *y = g_icam[prv][j].m + 12;
            double d = ((double)x[0] - y[0]) * ((double)x[0] - y[0]) + ((double)x[1] - y[1]) * ((double)x[1] - y[1]) +
                       ((double)x[2] - y[2]) * ((double)x[2] - y[2]);
            if (!used[j] && d < best) {
                best = d;
                bj = j;
            }
        }
        if (bj == IC_NO_CAM) {
            for (i = 0; i < g_icam_n[cur]; i++)
                if (g_ipair[i] == -1)
                    g_ipair[i] = -2;    /* none left */
            break;
        }
        g_ipair[bi] = (int)bj;
        used[bj] = 1;
    }
    if (mc != IC_NO_CAM && g_icam[cur][mc].draws) {
        const icam_t *a = g_ipair[mc] >= 0 ? &g_icam[prv][g_ipair[mc]] : NULL;
        const icam_t *b = &g_icam[cur][mc];
        if (!a) {
            why = "its camera is new";
            step = 1e9;
        } else {
            double dx = (double)b->m[12] - a->m[12], dy = (double)b->m[13] - a->m[13], dz = (double)b->m[14] - a->m[14];
            step = sqrt(dx * dx + dy * dy + dz * dz);
            if (a->rot_ok && b->rot_ok) {
                double dot = fabs(a->q[0] * b->q[0] + a->q[1] * b->q[1] + a->q[2] * b->q[2] + a->q[3] * b->q[3]);
                turn = 2.0 * acos(dot > 1.0 ? 1.0 : dot) * 57.29577951308232;
            }
        }
    }
    if (step > g_icut_step) {
        g_st.interp_cut_step++;
        why = why ? why : "the camera stepped";
    } else if (turn > g_icut_turn) {
        g_st.interp_cut_turn++;
        why = "the camera turned";
    } else if (frac < g_icut_match) {
        g_st.interp_cut_match++;
        why = "too few draws matched";
    }
    if (why) {
        if (g_subdump_cuts && logged < 40) {
            g_subdump_from = swap_no;
            g_subdump_to = swap_no + 2;
        }
        if (++logged <= 40)
            fprintf(stderr, "[D3DB] host interpolation: guest swap %u shown as it is (%s: %.2f units, %.1f deg, %.0f%%"
                    " of %u draws matched)\n", swap_no, why, step > 1e8 ? 0.0 : step, turn, 100.0 * frac,
                    g_ikey_n[cur]);
        return 0;
    }
    g_st.interp_frames++;
    g_imc = mc;
    g_st.interp_matched += frac;
    g_ipair_swap = swap_no;
    ic_pair();
    return 1;
}

/* This sub-frame's corrections, camera by camera, at t (0 < t < 1). */
static void ic_sub(double t)
{
    unsigned cur = (unsigned)g_ic, prv = (unsigned)g_ip, i, j;

    REPLAY_SIDE();
    g_it = t;
    g_isub_stamp++;                     /* step 66: the skeletons' G at this t, made again */
    for (i = 0; i < g_icam_n[cur]; i++) {
        const icam_t *b = &g_icam[cur][i], *a;
        double ci[16], inv_ci[16], cn[16], vfix[16], tmp[16], c[16], pi[16];
        g_isub[i].on = 0;
        if (g_ipair[i] < 0 || !b->draws || !b->p_ok)
            continue;
        a = &g_icam[prv][g_ipair[i]];
        if (memcmp(a->m, b->m, sizeof(a->m)) == 0)
            continue;                   /* it didn't move */
        if (a->rot_ok && b->rot_ok) {
            double q[4], r[9];
            q_slerp(a->q, b->q, t, q);
            m3_from_q(q, r);
            for (j = 0; j < 3; j++) {
                double l = a->len[j] + (b->len[j] - a->len[j]) * t;
                ci[4 * j] = r[3 * j] * l;
                ci[4 * j + 1] = r[3 * j + 1] * l;
                ci[4 * j + 2] = r[3 * j + 2] * l;
                ci[4 * j + 3] = b->m[4 * j + 3];
            }
            for (j = 12; j < 15; j++)
                ci[j] = a->m[j] + ((double)b->m[j] - a->m[j]) * t;
            ci[15] = b->m[15];
        } else {
            for (j = 0; j < 16; j++)
                ci[j] = a->m[j] + ((double)b->m[j] - a->m[j]) * t;
        }
        if (m4_inv(ci, inv_ci) != 0)
            continue;
        for (j = 0; j < 16; j++)
            pi[j] = a->m[16 + j] + ((double)b->m[16 + j] - a->m[16 + j]) * t;
        m4_from(b->m, cn);
        m4_mul(cn, inv_ci, vfix);
        m4_mul(b->inv_p, vfix, tmp);
        m4_mul(tmp, pi, c);
        for (j = 0; j < 16; j++) {
            g_isub[i].c[j] = (float)c[j];
            g_isub[i].vfix[j] = (float)vfix[j];
            g_isub[i].dp[j] = (float)(pi[j] - b->m[16 + j]);
        }
        for (j = 0; j < 3; j++)
            g_isub[i].pos[j] = (float)ci[12 + j];
        g_isub[i].on = 1;
    }
}

/* A recorded draw in a sub-frame: under its camera's correction, or as it
 * was. Called with g_lock held. */
static void ic_draw(const rcmd_t *c)
{
    static float k4_copy[192 * 4];
    static d3dh_ff_t ff;
    static d3dh_vprog_t vp;
    static struct {                     /* step 76: registers changed in place, as they were */
        unsigned reg;
        float v[4];
    } keep[4 + 4 + IC_BONE_REGS];
    unsigned n_keep;
    float *k4 = k4_copy;
    const d3dh_draw_t *d = &c->u.draw.d;
    const iobj_t *o = NULL, *p = NULL;
    const float *pb = NULL;
    d3dh_draw_t m;
    double wt[16];
    int i, j, n, cam_on, w_move = 0, b_move = 0, in_place = 0;

    REPLAY_SIDE();
#define IC_KEEP(r) do {                                                         \
        if (in_place) {                                                         \
            keep[n_keep].reg = (unsigned)(r);                                   \
            memcpy(keep[n_keep++].v, &k4[4 * (r)], 4 * sizeof(float));         \
        }                                                                       \
    } while (0)

    if (c->u.draw.cls != IC_NONE && c->u.draw.cam != g_imc) {
        g_st.interp_kept++;             /* step 65: under another camera than the world's: as recorded */
        g_st.interp_other_cam++;
        draw_cmd(c, d, 0);              /* step 87 */
        return;
    }
    cam_on = c->u.draw.cam < IC_CAMS && g_isub[c->u.draw.cam].on;
    if (c->u.draw.obj >= 0) {           /* step 62: the object, if it moved */
        o = &g_iobj[g_ic][c->u.draw.obj];
        if (o->prev >= 0) {
            p = &g_iobj[g_ip][o->prev];
            for (i = 0; i < 16 && !w_move; i++)
                w_move = fabs(o->w[i] - p->w[i]) > 1e-5 * (1.0 + fabs(o->w[i]));
            if (o->bones >= 0 && p->bones >= 0) {
                pb = g_ibone[g_ip] + p->bones;
                b_move = memcmp(pb, d->vprog->consts + 4 * IC_BONE_BASE, IC_BONE_REGS * 4 * sizeof(float)) != 0;
            }
        }
    }
    if (c->u.draw.cls == IC_NONE || (!cam_on && !w_move && !b_move)) {
        g_st.interp_kept++;
        draw_cmd(c, d, 0);
        return;
    }
    if (w_move) {                       /* W(t), from the frame before's W and this one's */
        aff_t a, b, r;
        for (i = 0; i < 3; i++)
            for (j = 0; j < 3; j++) {
                a.r[3 * i + j] = p->w[4 * i + j];
                b.r[3 * i + j] = o->w[4 * i + j];
            }
        for (j = 0; j < 3; j++) {
            a.t[j] = p->w[12 + j];
            b.t[j] = o->w[12 + j];
        }
        aff_lerp(&a, &b, g_it, &r);
        for (i = 0; i < 3; i++) {
            for (j = 0; j < 3; j++)
                wt[4 * i + j] = r.r[3 * i + j];
            wt[4 * i + 3] = 0;
            wt[12 + i] = r.t[i];
        }
        wt[15] = 1;
    }
    m = *d;
    n_keep = 0;
    if (c->u.draw.cls == IC_VP) {
        const float *s = d->vprog->consts;
        double wvp[16], t1[16], t2[16];
        /* Step 76: the registers that change are changed in the recording
         * itself, each kept first and put back after the draw, instead of
         * 3 KB copied for every draw. Writes go in the order they always did
         * (the camera's position, c96-c99, the bones, each read before it is
         * written), so only a camera-position register among c96-c99 or the
         * bones, where a later write would read an earlier one, takes the
         * copy. */
        in_place = 1;
        for (n = 0; n < c->u.draw.n_cpos; n++)
            if ((c->u.draw.cpos[n] >= 96 && c->u.draw.cpos[n] < 100) ||
                (c->u.draw.cpos[n] >= IC_BONE_BASE && c->u.draw.cpos[n] < IC_BONE_BASE + IC_BONE_REGS))
                in_place = 0;
        if (in_place) {
            k4 = (float *)s;
        } else {
            k4 = k4_copy;
            memcpy(k4, s, sizeof(k4_copy));
        }
        for (j = 0; j < 4; j++)         /* c(96+j) is column j of WVP */
            for (i = 0; i < 4; i++)
                wvp[4 * i + j] = s[4 * (96 + j) + i];
        if (w_move) {                   /* W(t) W^-1 WVP */
            double wi[16], l[16];
            if (m4_inv(o->w, wi) == 0) {
                m4_mul(wt, wi, l);
                m4_mul(l, wvp, t1);
                memcpy(wvp, t1, sizeof(wvp));
                g_st.interp_obj_vp++;
            }
        }
        if (cam_on) {                   /* ... C: the camera at t */
            m4_from(g_isub[c->u.draw.cam].c, t1);
            m4_mul(wvp, t1, t2);
            memcpy(wvp, t2, sizeof(wvp));
            for (n = 0; n < c->u.draw.n_cpos; n++) {
                IC_KEEP(c->u.draw.cpos[n]);
                memcpy(&k4[4 * c->u.draw.cpos[n]], g_isub[c->u.draw.cam].pos, 3 * sizeof(float));
            }
            g_st.interp_cpos += c->u.draw.n_cpos;
        }
        for (j = 0; j < 4; j++) {
            IC_KEEP(96 + j);
            for (i = 0; i < 4; i++)
                k4[4 * (96 + j) + i] = (float)wvp[4 * i + j];
        }
        if (b_move) {                   /* the bones at t */
            int bi, moved = 0;
            const double *gt = o->grp >= 0 ? ic_group_g((unsigned)o->grp) : NULL;
            double os[3];               /* step 68: a bone's step is judged less the object's own */
            for (i = 0; i < 3; i++)
                os[i] = g_imatch ? o->pos[i] - p->pos[i] : 0.0;
            for (bi = 0; bi < IC_BONE_REGS / 3; bi++) {
                const float *ba = pb + 12 * bi, *bb = s + 4 * (IC_BONE_BASE + 3 * bi);
                float *bo = k4 + 4 * (IC_BONE_BASE + 3 * bi);
                aff_t a, b, r;
                double dx, dy, dz;
                if (!(o->bmask >> bi & 1) || memcmp(ba, bb, 12 * sizeof(float)) == 0)
                    continue;
                if (gt) {                       /* step 66: blended in its skeleton's frame */
                    const igrp_t *gr = &g_igrp[o->grp];
                    double ma[16], mb[16], ra[16], rb[16], rl[16], mt[16];
                    dx = (double)ba[3] - bb[3] + os[0];
                    dy = (double)ba[7] - bb[7] + os[1];
                    dz = (double)ba[11] - bb[11] + os[2];
                    if (!(dx * dx + dy * dy + dz * dz <= 16 * g_isnap * g_isnap)) {
                        g_st.interp_bone_snap++;
                        continue;
                    }
                    bone_m4(ba, ma);
                    bone_m4(bb, mb);
                    m4_mul(gr->iga, ma, ra);
                    m4_mul(gr->igb, mb, rb);
                    for (i = 0; i < 12; i++)
                        rl[i] = ra[i] + (rb[i] - ra[i]) * g_it;
                    rl[12] = rl[13] = rl[14] = 0;
                    rl[15] = 1;
                    m4_mul(gt, rl, mt);
                    for (i = 0; i < 3; i++)
                        IC_KEEP(IC_BONE_BASE + 3 * bi + i);
                    for (i = 0; i < 12; i++)
                        bo[i] = (float)mt[i];
                    moved = 1;
                    g_st.interp_skel_bones++;
                    continue;
                }
                for (i = 0; i < 3; i++) {       /* axis j is column j of the 3x4 rows */
                    for (j = 0; j < 3; j++) {
                        a.r[3 * j + i] = ba[4 * i + j];
                        b.r[3 * j + i] = bb[4 * i + j];
                    }
                    a.t[i] = ba[4 * i + 3];
                    b.t[i] = bb[4 * i + 3];
                }
                dx = a.t[0] - b.t[0] + os[0];
                dy = a.t[1] - b.t[1] + os[1];
                dz = a.t[2] - b.t[2] + os[2];
                if (!(dx * dx + dy * dy + dz * dz <= 16 * g_isnap * g_isnap)) {
                    g_st.interp_bone_snap++;
                    continue;           /* a bone that jumped, or a register not read */
                }
                aff_lerp(&a, &b, g_it, &r);
                for (i = 0; i < 3; i++)
                    IC_KEEP(IC_BONE_BASE + 3 * bi + i);
                for (i = 0; i < 3; i++) {
                    for (j = 0; j < 3; j++)
                        bo[4 * i + j] = (float)r.r[3 * j + i];
                    bo[4 * i + 3] = (float)r.t[i];
                }
                moved = 1;
            }
            g_st.interp_bones += moved;
        }
        vp = *d->vprog;
        vp.consts = k4;
        m.vprog = &vp;
        g_st.interp_vp++;
    } else {
        double v[16], f[16], x[16];
        ff = *d->ff;
        if (cam_on) {
            m4_from(d->ff->view, v);
            m4_from(g_isub[c->u.draw.cam].vfix, f);
            m4_mul(v, f, x);
            for (i = 0; i < 16; i++)
                ff.view[i] = (float)x[i];
            if (c->u.draw.cls == IC_FF_PROJ)
                for (i = 0; i < 16; i++)
                    ff.proj[i] += g_isub[c->u.draw.cam].dp[i];
        }
        if (w_move) {
            for (i = 0; i < 16; i++)
                ff.world[i] = (float)wt[i];
            g_st.interp_obj_ff++;
        }
        m.ff = &ff;
        g_st.interp_ff++;
    }
    draw_cmd(c, &m, 1);
    while (n_keep) {                    /* step 76: the recording as it was, for the next sub-frame */
        n_keep--;
        memcpy(&k4[4 * keep[n_keep].reg], keep[n_keep].v, 4 * sizeof(float));
    }
#undef IC_KEEP
}

/* After the frame's last sub-frame: this frame becomes the frame before
 * (step 78: its tables; the set recorded into next was emptied at the
 * handover). */
static void ic_end(void)
{
    REPLAY_SIDE();
    if (t_rt)                           /* step 79: the game picks its next set by it (present_thread) */
        EnterCriticalSection(&g_ho);
    g_ip = g_ic;
    if (t_rt)
        LeaveCriticalSection(&g_ho);
    g_iprev_ok = 1;
}

/* Step 78: the recording set emptied. */
static void ic_clear(int i)
{
    g_icam_n[i] = 0;
    g_ikey_n[i] = 0;
    g_iobj_n[i] = 0;
    g_ibone_n[i] = 0;
}

/* Frames recorded for interpolation is what the setting asks for now. */
static int ic_want(void)
{
    return g_interp_want && !g_replay_check && g_replay <= 1;
}

/* At a frame's end, with nothing recorded: the setting, taken. */
static void ic_apply(void)
{
    int want = ic_want();

    GAME_SIDE();
    if (want == g_interp)
        return;
    g_interp = want;
    g_iprev_ok = 0;
    ic_clear(g_ir);
    fprintf(stderr, "[D3DB] host frame rate: %s from present %u\n",
            want ? "the display's, interpolated" : "the game's 59.94", g_st.presents);
}

/* Step 78 (notes 18.81): at the guest's Swap, the frame just recorded goes
 * to the replay side (g_ps, its tables g_ic) and the game side records the
 * next one into the other slot and the third table set. On one thread the
 * other slot is free: its frame was replayed inside the Swap before. Called
 * with g_lock held. */
static LARGE_INTEGER g_game_t0;         /* step 78, game side: its last Swap returned (0: none yet) */

/* What a frame handed over carries for the replay side (step 79: with the
 * render thread, the game side's settings it was recorded under too, and
 * whether the overlay holds the game at it). */
static void slot_fill(fslot_t *f, double t_s, unsigned swap_no)
{
    LARGE_INTEGER q;

    GAME_SIDE();
    QueryPerformanceCounter(&q);
    f->seq = ++g_slot_seq;
    f->it = g_ir;
    f->swap_no = swap_no;
    f->t_s = t_s;
    f->rec = REC_ON();
    f->interp = g_interp;
    f->game_ms = g_game_t0.QuadPart ? (double)(q.QuadPart - g_game_t0.QuadPart) * 1000.0 / (double)g_qpf.QuadPart : 0;
    f->set_gen = g_sq.side[0].gen;
    f->hold = 0;
    if (g_thr) {
        if (f->set_gen && f->set_copy != f->set_gen) {
            AcquireSRWLockShared(&g_sq.lock);
            f->set = g_sq.side[0].was;  /* what the game side applied last (sq_drain) */
            ReleaseSRWLockShared(&g_sq.lock);
            f->set_copy = f->set_gen;
        }
        f->hold = g_ovl_open && g_ovl_pauses;
    }
}

static void slot_handover(double t_s, unsigned swap_no)
{
    fslot_t *f = g_rs;

    GAME_SIDE();
    if (g_ps)                           /* never on one thread: the slot before is replayed in its own Swap */
        fprintf(stderr, "[D3DB] host frame slots: slot %d (frame %u) not replayed before the next\n",
                (int)(g_ps - g_slot), g_ps->seq);
    slot_fill(f, t_s, swap_no);
    if (g_sq.side[1].gen < f->set_gen)  /* never on one thread: the host side drains first */
        fprintf(stderr, "[D3DB] host frame slots: frame %u recorded under settings %u, the host side's at %u\n",
                f->seq, f->set_gen, g_sq.side[1].gen);
    g_ps = f;
    g_rs = &g_slot[(f - g_slot + 1) % FSLOTS];
    rk_forget();                        /* step 84 */
    AcquireSRWLockExclusive(&g_pool_lock);  /* the command array the last replay gave back */
    {
        rcmd_t *rc = g_rs->rc;
        unsigned cap = g_rs->rc_cap;
        g_rs->rc = g_rc_pool;
        g_rs->rc_cap = g_rc_pool_cap;
        g_rc_pool = rc;
        g_rc_pool_cap = cap;
    }
    ReleaseSRWLockExclusive(&g_pool_lock);
    g_ic = g_ir;
    g_ir = 3 - g_ic - g_ip;             /* the set that is neither: the frame before last's, done with */
    ic_clear(g_ir);
}

void d3dh_set_interp_adaptive(int on)
{
    InterlockedExchange(&g_iadapt, on ? 1 : 0);
}

void d3dh_set_render_thread(int mode)
{
    InterlockedExchange(&g_rt_mode, mode == TWOC_RT_ON || mode == TWOC_RT_AUTO ? mode : TWOC_RT_OFF);
}

void d3dh_set_framerate(int display)
{
    InterlockedExchange(&g_interp_want, display ? 1 : 0);
    if (display && (g_replay_check || g_replay > 1))
        fprintf(stderr, "[D3DB] host frame rate: interpolation is off while RECOMP_D3D_BRIDGE_REPLAY%s is set\n",
                g_replay_check ? "_CHECK" : "=N");
}

/* ── Step 92: ambient occlusion (notes 18.95) ─────────────────
 *
 * The world pass (the display list flushed at 0x8929A, d3d_bridge.c's
 * d3db_list_flush) draws the level's opaque geometry into the back buffer
 * first, then what is blended (glass, water's surface, shadows' darkening,
 * decals) with depth writes off, and the stencil shadow; the sky comes in a
 * flush of its own before it, particles, snow and full-screen fades in
 * flushes after it, the HUD and the pause menu in the overlay pass last.
 * So the occlusion goes in at the world pass's first draw into the back
 * buffer that doesn't write depth, once AO_MIN_DRAWS opaque ones have (or
 * at the pass's end if none comes): everything drawn after it, blended
 * things, particles and the HUD, is left as the game draws it. A world pass
 * drawn into a render target (H2 Oh No's underwater pass, the textures the
 * game renders) gets none, and neither does a frame without a world pass
 * (the pause still).
 *
 * The game side notes the place (RC_AO among the frame's commands; at once
 * when frames aren't recorded) with the world's projection from the pass's
 * start and the fog of its last opaque draw; the replay side draws it
 * (ao_exec, ao.c) in every pass over the frame, so each sub-frame gets the
 * occlusion of its own depth. With it off nothing here runs and the back
 * buffer is made as before; on, the back buffer always has a render target
 * and a readable depth of its own (bb_rebuild). */
#define AO_MIN_DRAWS 8
static struct {
    int in, marked;                     /* inside the world pass; its occlusion placed */
    unsigned drew;                      /* its opaque draws into the back buffer so far */
    ao_mark_t m;
} g_aw;
static volatile LONG g_ao_marks[3];     /* placed at a blended draw, at the pass's end; world passes without one */
static unsigned g_ao_runs, g_ao_fails, g_ao_off;    /* replay side */

static uint32_t rs_value(const d3dh_draw_t *d, uint32_t state, uint32_t dflt)
{
    unsigned i;

    for (i = 0; i < d->n_rs; i++)
        if (d->rs[i].state == state)
            return d->rs[i].value;
    return dflt;
}

/* The occlusion here. Game side, the recording lock held. */
static void ao_mark(int at_end)
{
    int side;

    g_aw.marked = 1;
    InterlockedIncrement(&g_ao_marks[at_end ? 1 : 0]);
    if (REC_ON()) {
        rcmd_t *c;
        if (g_replay_check) {           /* at once too, as the draws are */
            side = side_set(SIDE_NOW);
            ao_exec(&g_aw.m);
            side_set(side);
        }
        c = rc_push(RC_AO);
        if (c)
            c->u.ao = g_aw.m;
    } else {
        side = side_set(SIDE_NOW);
        ao_exec(&g_aw.m);
        side_set(side);
    }
}

/* A world-pass draw into the back buffer. Game side, the recording lock held. */
static void ao_world_draw(const d3dh_draw_t *d)
{
    uint32_t v;

    if (rs_value(d, 7, 1) && rs_value(d, 14, 1)) {      /* ZENABLE, ZWRITEENABLE: opaque */
        g_aw.drew++;
        g_aw.m.fog_on = rs_value(d, 28, 0) ? (int)rs_value(d, 35, 0) : 0;     /* FOGENABLE: FOGTABLEMODE */
        v = rs_value(d, 36, 0);
        memcpy(&g_aw.m.fog_start, &v, 4);
        v = rs_value(d, 37, 0);
        memcpy(&g_aw.m.fog_end, &v, 4);
        v = rs_value(d, 38, 0);
        memcpy(&g_aw.m.fog_density, &v, 4);
    } else if (g_aw.drew >= AO_MIN_DRAWS) {
        ao_mark(0);
    }
}

/* d3d_bridge.c: the world pass begins (with the world's projection,
 * 0x242398) or ends. */
void d3dh_world_pass(int begin, const float *proj)
{
    int side;

    if (!g_ready || (!g_ao_mode && !g_aw.in))
        return;
    rec_lock(0);
    side = side_set(SIDE_GAME);
    if (begin) {
        memset(&g_aw, 0, sizeof(g_aw));
        g_aw.in = g_ao_mode != 0;
        memcpy(g_aw.m.proj, proj, sizeof(g_aw.m.proj));
    } else if (g_aw.in) {
        if (!g_aw.marked && g_aw.drew)
            ao_mark(1);
        else if (!g_aw.marked)
            InterlockedIncrement(&g_ao_marks[2]);
        g_aw.in = 0;
    }
    side_set(side);
    rec_unlock();
}

/* Draw the occlusion into the back buffer. Replay side, g_lock held. */
static void ao_exec(const ao_mark_t *m)
{
    static LARGE_INTEGER last;
    LARGE_INTEGER now;
    D3D11_TEXTURE2D_DESC td;
    ao_params_t p;
    int mode = (int)g_ao_mode;

    REPLAY_SIDE();
    if (!mode || !g_bb.dsrv || !g_bb.tex) {
        g_ao_off++;
        return;
    }
    ID3D11Texture2D_GetDesc(((D3D8Texture *)g_bb.tex)->d3d11_texture, &td);
    memset(&p, 0, sizeof(p));
    p.mode = mode;
    p.strength = (float)g_ao_strength / 100.0f;
    memcpy(p.proj, m->proj, sizeof(p.proj));
    p.fog_mode = m->fog_on;
    p.fog_start = m->fog_start;
    p.fog_end = m->fog_end;
    p.fog_density = m->fog_density;
    p.radius = g_ao_radius;
    p.half = g_ao_half;
    p.denoise = g_ao_denoise;
    p.debug = g_ao_debug;
    if (ao_apply(&p, g_bb.dsrv, g_bb.ms ? g_bb.ms : 1, ((D3D8Surface *)g_bb.surf)->rtv, td.Width, td.Height) == 0) {
        if (g_bb.ms)
            g_bb.dirty = 1;
        g_ao_runs++;
    } else {
        g_ao_fails++;
    }
    QueryPerformanceCounter(&now);
    if (!last.QuadPart)
        last = now;
    if ((double)(now.QuadPart - last.QuadPart) >= 10.0 * (double)g_qpf.QuadPart) {
        ao_stats_t s;
        char gpu[160] = "";
        ao_stats_take(&s);
        if (s.timed)
            snprintf(gpu, sizeof(gpu), "; GPU ms a pass (%u timed): depth %.3f, prefilter %.3f, occlusion %.3f,"
                     " denoise %.3f, apply %.3f, all %.3f", s.timed, s.ms[0], s.ms[1], s.ms[2], s.ms[3], s.ms[4],
                     s.total_ms);
        fprintf(stderr, "[AO] %s, strength %ld%%, radius %.2f, %s size %ux%u%s: drawn %u, failed %u, off %u; placed"
                " at a blended draw %ld, at the world pass's end %ld, world passes without one %ld%s\n",
                twoc_ao_name(mode), (long)g_ao_strength, (double)p.radius, p.half ? "half" : "full", td.Width,
                td.Height, g_bb.ms ? ", multisampled" : "", g_ao_runs, g_ao_fails, g_ao_off, (long)g_ao_marks[0],
                (long)g_ao_marks[1], (long)g_ao_marks[2], gpu);
        last = now;
    }
}

/* The setting, at once (replay side): on for the first time, the back
 * buffer is made again with a depth a shader can read. */
static void set_ao(int mode, int strength)
{
    int was = (int)g_ao_mode;

    REPLAY_SIDE();
    EnterCriticalSection(&g_lock);
    InterlockedExchange(&g_ao_strength, strength < 0 ? 0 : strength > 100 ? 100 : strength);
    if (mode && !g_ao_depth) {
        g_ao_depth = 1;
        if (FAILED(bb_rebuild(g_w, g_h))) {
            g_ao_depth = 0;
            fprintf(stderr, "[D3DB] host ambient occlusion: no readable depth for the back buffer; off\n");
            mode = TWOC_AO_OFF;
        }
    }
    InterlockedExchange(&g_ao_mode, mode);
    {
        ao_stats_t s;
        ao_stats_take(&s);              /* the [AO] line's GPU times start over with the new mode */
    }
    LeaveCriticalSection(&g_lock);
    if (mode != was || mode)
        fprintf(stderr, "[D3DB] host ambient occlusion: %s, strength %ld%%\n", twoc_ao_name(mode), (long)g_ao_strength);
}

int d3dh_draw_up(const d3dh_draw_t *d)
{
    int r, side;

    if (!g_ready)
        return -1;
    rec_lock(0);
    side = side_set(SIDE_GAME);         /* step 78 */
    if (g_aw.in && !g_aw.marked && !d->target.data)     /* step 92 */
        ao_world_draw(d);
    if (g_replay_check) {               /* at once, as without replay, and recorded */
        g_tc_now = &g_tcc;              /* step 78: at once the old way, from guest memory, in a cache of its own */
        side_set(SIDE_NOW);
        r = draw_exec(d, NULL, NULL, NULL);
        side_set(SIDE_GAME);
        g_tc_now = &g_tc;
        rec_draw(d);
    } else if (REC_ON()) {
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        r = rec_draw(d);
        QueryPerformanceCounter(&t1);
        g_stg.rec_ms += (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)g_qpf.QuadPart;
        g_stg.rec_draws++;
    } else {
        side_set(SIDE_NOW);
        r = draw_exec(d, NULL, NULL, NULL);
    }
    side_set(side);
    if (d->overlay)                     /* step 65 */
        g_stg.overlay_draws++;
    if (d->world_cam)
        g_stg.overlay_world_cam++;
    if (d->hud)
        g_stg.hud_draws++;
    rec_unlock();
    return r;
}

/* ── Step 65: the HUD at the screen edges (notes 18.68) ─────────
 *
 * The game draws its HUD counters as 3D models and text just in front of
 * the world's camera, in its overlay pass (the display list flushed at
 * 0x89679; d3d_bridge.c tags those draws, and those the panel's in-game part
 * queued as `hud`). Under the widened projection (18.56) they keep their 4:3
 * places in the middle. With the HUD at the edges, the overlay pass's host
 * calls are held to its end (recorded as for a replay, unless the frame is
 * recorded anyway), and there the HUD's draws are grouped into elements:
 * the draws one call site of the panel queued (a counter's digits, its icon,
 * a card's line) are one. Since step 68 an element's anchor is fixed by its
 * call site (g_hud_sites): left ones move left by the picture's extra width
 * on that side, right ones right, the rest stay; only things flying between
 * the world and the HUD move by where they are (in the middle, within 0.2
 * of the 4:3 half width, not at all; past 0.45 all the way; part of the way
 * in between, so the clock just picked up never jumps). Each draw of a moved element gets x_clip += dx w (draw_exec), so
 * its shape and its distance from its 4:3 edge are kept; `margin` pulls it
 * in by that share of the picture's width, never past its 4:3 place. Not
 * interpolated (d3dh_draw_t.overlay; step 65 item 3). */
static volatile LONG g_hud_edges, g_hud_margin;    /* RECOMP_HUD=edges, RECOMP_HUD_MARGIN (percent) */
static int g_ovl_in, g_ovl_anchor;      /* inside the overlay pass; its HUD is to be anchored */
static unsigned g_ovl_first;            /* its first command in g_rc */

void d3dh_set_hud(int edges, int margin)
{
    InterlockedExchange(&g_hud_edges, edges ? 1 : 0);
    InterlockedExchange(&g_hud_margin, margin < 0 ? 0 : margin > 10 ? 10 : margin);
}

/* A recorded draw's screen box in clip-space NDC (x0, y0, x1, y1). Returns
 * 0 if it has none (nothing in front of the eye, or no position known). */
static int hud_box(const d3dh_draw_t *d, double *b)
{
    double m[16], t[16], w[16], v[16], p[16];
    const uint8_t *vb = (const uint8_t *)d->verts;
    unsigned i, n = 0;

    b[0] = b[1] = 1e30;
    b[2] = b[3] = -1e30;
    if (d->vprog) {
        int slot = vs_slot(d->vprog), r, k;
        const float *k4 = d->vprog->consts;
        if (slot < 0)
            return 0;
        for (r = 0; r < 4; r++)         /* m: rows c96-c99 (x, y, z, w of clip space) */
            for (k = 0; k < 4; k++)
                m[4 * r + k] = k4[4 * (96 + r) + k];
        if (g_vs[slot].rel_bones) {     /* skinned: where its first bone puts it */
            double q[4] = { k4[4 * 124 + 3], k4[4 * 125 + 3], k4[4 * 126 + 3], 1.0 };
            double cx = m[0] * q[0] + m[1] * q[1] + m[2] * q[2] + m[3] * q[3];
            double cy = m[4] * q[0] + m[5] * q[1] + m[6] * q[2] + m[7] * q[3];
            double cw = m[12] * q[0] + m[13] * q[1] + m[14] * q[2] + m[15] * q[3];
            if (cw <= 1e-6)
                return 0;
            b[0] = b[2] = cx / cw;
            b[1] = b[3] = cy / cw;
            return 1;
        }
        if (!(g_vs[slot].inputs & 1) || !vb || d->stride < 16)
            return 0;                   /* v0, a float4 first in the host's layout, is the position */
        for (i = 0; i < d->vert_count; i++) {
            const float *q = (const float *)(vb + (size_t)i * d->stride);
            double cx = m[0] * q[0] + m[1] * q[1] + m[2] * q[2] + m[3] * q[3];
            double cy = m[4] * q[0] + m[5] * q[1] + m[6] * q[2] + m[7] * q[3];
            double cw = m[12] * q[0] + m[13] * q[1] + m[14] * q[2] + m[15] * q[3];
            if (cw <= 1e-6)
                continue;
            b[0] = cx / cw < b[0] ? cx / cw : b[0];
            b[2] = cx / cw > b[2] ? cx / cw : b[2];
            b[1] = cy / cw < b[1] ? cy / cw : b[1];
            b[3] = cy / cw > b[3] ? cy / cw : b[3];
            n++;
        }
        return n != 0;
    }
    if (!d->ff || (d->fvf & D3DFVF_POSITION_MASK) != D3DFVF_XYZ || !vb)
        return 0;
    m4_from(d->ff->world, w);
    m4_from(d->ff->view, v);
    m4_from(d->ff->proj, p);
    m4_mul(w, v, t);
    m4_mul(t, p, m);                    /* row vectors: clip = (x y z 1) W V P */
    for (i = 0; i < d->vert_count; i++) {
        const float *q = (const float *)(vb + (size_t)i * d->stride);
        double cx = q[0] * m[0] + q[1] * m[4] + q[2] * m[8] + m[12];
        double cy = q[0] * m[1] + q[1] * m[5] + q[2] * m[9] + m[13];
        double cw = q[0] * m[3] + q[1] * m[7] + q[2] * m[11] + m[15];
        if (cw <= 1e-6)
            continue;
        b[0] = cx / cw < b[0] ? cx / cw : b[0];
        b[2] = cx / cw > b[2] ? cx / cw : b[2];
        b[1] = cy / cw < b[1] ? cy / cw : b[1];
        b[3] = cy / cw > b[3] ? cy / cw : b[3];
        n++;
    }
    return n != 0;
}

/* Step 68 (notes 18.71): each HUD element's anchor, fixed by the panel's
 * call site that queued it (the return address d3d_bridge.c finds), read
 * from sub_000B0EF0's code: the x it passes is a constant per site (the HUD
 * slides in and out vertically, and grows after a death; x stays). Step 65
 * judged each element by where it was, and joined elements lying near each
 * other: the wumpa counter's "40" came within reach of the spinning crate,
 * so both moved part of the way, in and out every few frames, and the
 * BONUS letters joined the counters the same way. Now no element joins
 * another. Sites not listed stay in the middle (the crate counter, BONUS
 * and DEMO, ENTERING TIME TRIAL, the countdowns, the collectables' icons at
 * the bottom centre). HUD_FLY: things that fly between the world and the
 * HUD (the clock picked up, fruit flying to the counter, the clock to the
 * middle at a time trial's end) move by where they are, on a ramp, each
 * piece of one site apart. */
enum { HUD_MID, HUD_LEFT, HUD_RIGHT, HUD_FLY };
static const struct {
    uint32_t site;
    uint8_t anchor;
} g_hud_sites[] = {
    /* modes 2-4 (a bonus round's HUD): wumpa, crates (middle), lives */
    { 0xB26A4, HUD_LEFT }, { 0xB2751, HUD_LEFT }, { 0xB2768, HUD_LEFT },
    { 0xB2AA0, HUD_RIGHT }, { 0xB2B00, HUD_RIGHT },
    /* the level HUD: wumpa (fruit x -0.78, digits -0.68), crates (middle), lives (0.49, 0.61) */
    { 0xB2C53, HUD_LEFT }, { 0xB2C6A, HUD_LEFT }, { 0xB2CCD, HUD_LEFT },
    { 0xB2F53, HUD_RIGHT }, { 0xB2FAF, HUD_RIGHT },
    /* fruit flying to the counter (sub_000B06D0; the 0x562F78 list) */
    { 0xB3354, HUD_FLY }, { 0xB33B7, HUD_FLY },
    /* a time trial's end: the clock to the middle, its list */
    { 0xB3631, HUD_FLY }, { 0xB3692, HUD_FLY }, { 0xB37A3, HUD_FLY },
    /* the time-trial clock, flying in from its pickup */
    { 0xB3A37, HUD_FLY },
    /* the bottom corners: percentage and icon left (-0.8, -0.7), count and bar right (0.65-1) */
    { 0xB3AC8, HUD_LEFT }, { 0xB3B45, HUD_LEFT }, { 0xB3B84, HUD_RIGHT }, { 0xB3BEF, HUD_RIGHT },
    { 0xB3C4C, HUD_RIGHT }, { 0xB3C89, HUD_RIGHT }, { 0xB3CDD, HUD_RIGHT },
    { 0xB3D40, HUD_LEFT }, { 0xB3DBD, HUD_LEFT }, { 0xB3DF8, HUD_RIGHT }, { 0xB3E2B, HUD_RIGHT },
    { 0xB3E78, HUD_RIGHT }, { 0xB3EAC, HUD_RIGHT }, { 0xB3EFA, HUD_RIGHT }, { 0xB3F2F, HUD_RIGHT },
    { 0xB405D, HUD_LEFT }, { 0xB40A9, HUD_LEFT }, { 0xB418A, HUD_LEFT }, { 0xB41D8, HUD_LEFT },
    { 0xB4250, HUD_RIGHT }, { 0xB43A9, HUD_LEFT }, { 0xB4464, HUD_LEFT }, { 0xB44DA, HUD_RIGHT },
    { 0xB4585, HUD_RIGHT }, { 0xB459C, HUD_RIGHT }, { 0xB462A, HUD_LEFT },
};

static int hud_site_anchor(uint32_t site)
{
    unsigned i;

    for (i = 0; i < sizeof(g_hud_sites) / sizeof(g_hud_sites[0]); i++)
        if (g_hud_sites[i].site == site)
            return g_hud_sites[i].anchor;
    return HUD_MID;
}

#define HUD_MAX 512
static int hud_root(int *par, int i)
{
    while (par[i] != i)
        i = par[i] = par[par[i]];
    return i;
}

/* The overlay pass's commands [first, g_rs->rc_n): each HUD draw's hud_dx. */
static void hud_anchor(unsigned first)
{
    static double box[HUD_MAX][4], el[HUD_MAX][4];
    static unsigned cmd[HUD_MAX];
    static int par[HUD_MAX], anc[HUD_MAX], logn = -1;
    double s = (4.0 / 3.0) / g_rwide, mg = 0.02 * (double)g_hud_margin, dl, dr;
    unsigned i, j, n = 0;

    GAME_SIDE();
    if (logn < 0) {
        const char *e = getenv("RECOMP_D3D_BRIDGE_HUDLOG");
        logn = e ? atoi(e) : 0;
    }
    for (i = first; i < g_rs->rc_n && n < HUD_MAX; i++)
        if (g_rs->rc[i].kind == RC_DRAW && g_rs->rc[i].u.draw.d.hud) {
            anc[n] = hud_site_anchor(g_rs->rc[i].u.draw.d.hud);
            if (!hud_box(&g_rs->rc[i].u.draw.d, box[n])) {
                if (anc[n] == HUD_FLY)  /* nowhere to judge it by: as it is */
                    continue;
                box[n][0] = box[n][1] = box[n][2] = box[n][3] = 0;
            }
            box[n][0] /= s;             /* x in halves of the 4:3 picture's width, y in halves of its height */
            box[n][2] /= s;
            cmd[n] = i;
            par[n] = (int)n;
            n++;
        }
    if (!n)
        return;
    for (i = 0; i < n; i++)             /* one element a site; a flying site's pieces by nearness */
        for (j = i + 1; j < n; j++) {
            double hi = box[i][3] - box[i][1], hj = box[j][3] - box[j][1], h = hi > hj ? hi : hj;
            double gap = (box[i][0] > box[j][0] ? box[i][0] - box[j][2] : box[j][0] - box[i][2]) * 320.0,
                   vov = (box[i][3] < box[j][3] ? box[i][3] : box[j][3]) - (box[i][1] > box[j][1] ? box[i][1] : box[j][1]);
            if (g_rs->rc[cmd[i]].u.draw.d.hud != g_rs->rc[cmd[j]].u.draw.d.hud)
                continue;
            if (anc[i] != HUD_FLY || (vov >= 0 && gap < 0.4 * h * 240.0 + 2.0))   /* in 4:3 pixels: x 320 and y 240 a half */
                par[hud_root(par, (int)i)] = hud_root(par, (int)j);
        }
    for (i = 0; i < n; i++) {
        el[i][0] = el[i][1] = 1e30;
        el[i][2] = el[i][3] = -1e30;
    }
    for (i = 0; i < n; i++) {
        int r = hud_root(par, (int)i);
        for (j = 0; j < 4; j++)
            el[r][j] = j < 2 ? (box[i][j] < el[r][j] ? box[i][j] : el[r][j]) : (box[i][j] > el[r][j] ? box[i][j] : el[r][j]);
    }
    dl = s - 1.0 + mg;                  /* to the left edge (NDC), pulled in by the margin */
    dl = dl > 0 ? 0 : dl;
    dr = -dl;
    for (i = 0; i < n; i++) {
        int r = hud_root(par, (int)i);
        double c = 0.5 * (el[r][0] + el[r][2]), a = (fabs(c) - 0.2) / 0.25, sh;
        a = a < 0 ? 0 : a > 1 ? 1 : a;  /* a ramp, so one flying in never jumps */
        sh = anc[i] == HUD_LEFT ? -1.0 : anc[i] == HUD_RIGHT ? 1.0 : anc[i] == HUD_FLY ? (c < 0 ? -a : a) : 0.0;
        g_rs->rc[cmd[i]].u.draw.d.hud_dx = (float)(sh < 0 ? -sh * dl : sh * dr);
        if (r == (int)i)
            ++*(sh < 0 ? &g_stg.hud_left : sh > 0 ? &g_stg.hud_right : &g_stg.hud_mid);
        if (logn > 0) {                 /* RECOMP_D3D_BRIDGE_HUDLOG=N: N passes' boxes, anchors and shares */
            const d3dh_draw_t *d = &g_rs->rc[cmd[i]].u.draw.d;
            fprintf(stderr, "[D3DB] hudlog pass %u draw %u %s %08X site %05X box %.3f %.3f %.3f %.3f element %d "
                    "share %.3f anchor %c\n", g_stg.hud_passes, cmd[i] - first, d->vprog ? "vp" : "ff",
                    d->vprog ? d->vprog->key : d->fvf, d->hud, box[i][0], box[i][1], box[i][2], box[i][3], r, sh,
                    "MLRF"[anc[i]]);
        }
    }
    if (logn > 0)
        logn--;
    g_stg.hud_passes++;
}

/* Forget recorded commands from `first` on (the overlay pass's, run). */
static void rc_forget(unsigned first)
{
    unsigned i, s;

    REPLAY_SIDE();
    rk_forget();                        /* step 84 */
    rc_unhold(g_rs, first, g_rs->rc_n);
    for (i = first; i < g_rs->rc_n; i++)
        if (g_rs->rc[i].kind == RC_DRAW)
            for (s = 0; s < 4; s++)
                if (g_rs->rc[i].u.draw.pin[s])
                    g_rs->rc[i].u.draw.pin[s]->lpVtbl->Release(g_rs->rc[i].u.draw.pin[s]);
    g_rs->rc_n = first;
    while (g_rs->up_n && g_rs->up[g_rs->up_n - 1].at >= first)   /* step 78: their uploads, if not made */
        g_rs->up_n--;
    if (!first) {
        g_rs->ra_cur = NULL;
        g_rs->ra_bytes = 0;
        g_rs->targets_n = 0;
        g_rs->overflow = 0;
    }
}

/* Step 65 (notes 18.68): the game's overlay pass, d3d_bridge.c's
 * d3db_list_flush around it. */
void d3dh_overlay_pass(int begin)
{
    unsigned i;
    int side;

    if (!g_ready)
        return;
    rec_lock(0);
    side = side_set(SIDE_GAME);         /* step 78 */
    if (begin) {
        g_stg.overlay_passes++;
        g_ovl_in = 1;
        g_ovl_anchor = g_hud_edges && g_rwide > 0 && !g_replay_check;
        g_ovl_first = g_rs->rc_n;
        g_ovl_defer = g_ovl_anchor && !g_replay && !g_interp;
    } else if (g_ovl_in) {
        if (g_ovl_anchor)
            hud_anchor(g_ovl_first);
        if (g_ovl_defer) {              /* drawn now, as recorded, and forgotten */
            g_ovl_defer = 0;
            side_set(SIDE_NOW);         /* step 78: frames aren't recorded: at once */
            slot_prepare(g_rs, g_ovl_first);    /* step 78: their textures uploaded and pinned */
            for (i = g_ovl_first; i < g_rs->rc_n; i++) {
                const rcmd_t *c = &g_rs->rc[i];
                if (c->kind == RC_DRAW)
                    draw_exec(&c->u.draw.d, c->u.draw.pin, c->u.draw.pinned, NULL);
                else if (c->kind == RC_CLEAR)
                    clear_exec(&c->u.clear.t, c->u.clear.flags, c->u.clear.color, c->u.clear.z, c->u.clear.stencil);
                else if (c->kind == RC_COPY)
                    copy_exec(&c->u.copy.t, c->u.copy.rects, c->u.copy.points, c->u.copy.n);
                else
                    freed_exec(c->u.freed.data, c->u.freed.bytes);
            }
            rc_forget(g_ovl_first);
        }
        g_ovl_in = g_ovl_anchor = 0;
    }
    side_set(side);
    rec_unlock();
}

/* ── CopyRects (step 35) ───────────────────────────────────── */

#define XDK_FMT_LIN_A8B8G8R8 0x3F      /* R8G8B8A8 in xbox_d3d8, the back buffer's DXGI format */

/* The host back buffer's texture, referenced: xbox_d3d8's swap-chain buffer,
 * or since a live size change its stand-in (step 54), resolved from its
 * multisampled surface with MSAA (step 58). NULL if unavailable. */
static ID3D11Texture2D *back_tex(void)
{
    ID3D11Texture2D *t = NULL;

    REPLAY_SIDE();
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

    REPLAY_SIDE();
    if (!src || twoc_aa_post(g_aa) == TWOC_AA_OFF)     /* step 90: MSAA + SMAA filters the resolved frame too */
        return src;
    if (aa_post(twoc_aa_post(g_aa), src, &out) != 0) {
        g_st.post_aa_fails++;
        return src;
    }
    g_st.post_aa++;
    ID3D11Texture2D_Release(src);
    return out;
}

int d3dh_copy_back(const d3dh_target_t *dst, const int32_t *rects, const int32_t *points, unsigned n)
{
    int ok = 0, side;

    if (!g_ready)
        return -1;
    rec_lock(0);
    side = side_set(SIDE_GAME);         /* step 78 */
    if (REC_ON()) {                     /* step 60: the rects and points copied */
        rcmd_t *c = rc_push(RC_COPY);
        if (g_replay_check) {
            side_set(SIDE_NOW);
            ok = copy_exec(dst, rects, points, n);
            side_set(SIDE_GAME);
        }
        if (c) {
            c->u.copy.t = *dst;
            c->u.copy.n = n;
            c->u.copy.rects = n ? (int32_t *)ra_dup(rects, (size_t)n * 16) : NULL;
            c->u.copy.points = n && points ? (int32_t *)ra_dup(points, (size_t)n * 8) : NULL;
            rc_target(dst->data);
            if (n && !c->u.copy.rects) {
                g_rs->rc_n--;
                ok = -1;
            }
        } else {
            ok = -1;
        }
    } else {
        side_set(SIDE_NOW);
        ok = copy_exec(dst, rects, points, n);
    }
    side_set(side);
    rec_unlock();
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

    REPLAY_SIDE();
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

    REPLAY_SIDE();
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

    REPLAY_SIDE();
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
    int ok = -1, side;

    if (!g_ready || t->cube)
        return -1;
    rec_lock(1);
    side = side_set(SIDE_NOW);          /* step 78: a check, at once */
    tex = tex_get(&g_tcc, t);           /* step 78: apart from the cache recorded frames use */
    if (tex) {
        dump_texture(((D3D8Texture *)tex)->d3d11_texture, NULL, path, 1);
        ok = 0;
    }
    side_set(side);
    rec_unlock();
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

    REPLAY_SIDE();
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

/* Step 63: why the pacing is what it is, for the log and the overlay when
 * interpolation can't run ("" when the display's vsync paces). */
static void pace_note(const char *why)
{
    AcquireSRWLockExclusive(&g_why_lock);
    snprintf(g_pace_why, sizeof(g_pace_why), "%s", why ? why : "");
    ReleaseSRWLockExclusive(&g_why_lock);
}

void d3dh_pacing_note(const char *why)
{
    pace_note(why);
}

/* S7: wait for this present's slot on the g_frame_hz clock. Outside g_lock,
 * (Step 61: `mid` 0, an interpolated sub-frame's present goes as soon as the
 * queue has room, without the mid-refresh hold: the queue paces them, and
 * holding each for its own refresh kept the game in its Swap 3+ refreshes.)
 * so a loading thread's host calls never queue behind the wait; the game
 * serialises its swaps anyway, and pace_lock keeps the clock consistent if
 * two threads ever swapped at once. Step 77: under the limiter a present
 * waits `slots` of its ticks (more than 1, or part of one, under a frame-rate
 * plan's limiter); under vsync the queue paces whatever the slots. */
static void pace(int mid, double slots)
{
    static SRWLOCK pace_lock = SRWLOCK_INIT;
    static LARGE_INTEGER freq;
    static double next;                 /* QPC tick of the next slot, 0 before the first present */
    static HANDLE timer;
    static int timer_tried;
    LARGE_INTEGER now;
    double period, left;

    REPLAY_SIDE();
    if (!g_latency && g_vs_held) {      /* step 63: vsync pacing again after a stall */
        AcquireSRWLockExclusive(&pace_lock);
        if (!freq.QuadPart)
            QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&now);
        if (!g_latency && g_vs_held && now.QuadPart >= g_vs_retry) {
            g_latency = g_vs_held;
            g_interval = g_vs_held_interval;
            g_frame_hz = 0;
            g_vs_held = NULL;
            next = 0;
            pace_note("");
            fprintf(stderr, "[D3DB] host: vsync pacing again, interval %u, %d s after the display's stall\n",
                    g_interval, VSYNC_RETRY_S);
        }
        ReleaseSRWLockExclusive(&pace_lock);
    }
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
         * limiter paces, for VSYNC_RETRY_S seconds (step 63: it was the
         * rest of the run, and with it frame interpolation, until a
         * restart). */
        r = WaitForSingleObjectEx(g_latency, VSYNC_WAIT_MS, TRUE);
        QueryPerformanceCounter(&now);
        g_st.paced++;
        g_st.paced_wait_ms += ((double)now.QuadPart - left) * 1000.0 / (double)freq.QuadPart;
        if (r == WAIT_TIMEOUT) {
            g_st.paced_late++;
            if (++misses >= 3) {
                char why[160];
                g_vs_held = g_latency;
                g_vs_held_interval = g_interval;
                g_vs_retry = now.QuadPart + (LONGLONG)VSYNC_RETRY_S * freq.QuadPart;
                g_frame_hz = 60000.0 / 1001.0;  /* the game's rate (DWM's rate over the interval could be off) */
                g_interval = 0;
                g_latency = NULL;
                misses = 0;
                snprintf(why, sizeof(why), "the display released no frame in %d ms 3 times running (vsync again in"
                         " %d s)", VSYNC_WAIT_MS, VSYNC_RETRY_S);
                pace_note(why);
                fprintf(stderr, "[D3DB] host: the display released no frame in %d ms, 3 times running;"
                        " the frame limiter paces for %d s, %.3f Hz\n", VSYNC_WAIT_MS, VSYNC_RETRY_S, g_frame_hz);
                InterlockedIncrement(&g_stall_seq);    /* step 90: the window thread's recovery */
            }
        } else {
            misses = 0;
            if (mid)
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
    if (next != 0 && slots != 1.0)
        next += period * (slots - 1.0); /* this present's slot: `slots` ticks after the one before */
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

    REPLAY_SIDE();
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

    REPLAY_SIDE();
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

    REPLAY_SIDE();
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

    REPLAY_SIDE();
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
/* Step 61: while frames are interpolated the visible swap chain queues
 * more: the game works on its next frame (~8 ms upstairs) while the queue
 * shows the sub-frames already presented, and with the usual 2 buffers and
 * 2 frames of latency at interval 1 that covered only ~8 ms (56 fps there).
 * Back to 2 and 2 when it stops. Called with g_lock held, holding no buffer. */
static void ic_queue(int on)
{
    IDXGISwapChain2 *sc2 = NULL;
    unsigned nb = on ? g_iq_buffers : 2, lat = on ? g_iq_latency : 2;
    HRESULT hr;

    REPLAY_SIDE();
    if (!g_flip || !g_latency || on == g_iq_on)
        return;
    nb = nb < 2 ? 2 : nb > 8 ? 8 : nb;
    lat = lat < 1 ? 1 : lat > 8 ? 8 : lat;
    hr = IDXGISwapChain1_ResizeBuffers(g_flip, nb, g_buf_w, g_buf_h, DXGI_FORMAT_UNKNOWN, g_flip_flags);
    if (SUCCEEDED(hr) && SUCCEEDED(IDXGISwapChain1_QueryInterface(g_flip, &IID_IDXGISwapChain2, (void **)&sc2))) {
        hr = IDXGISwapChain2_SetMaximumFrameLatency(sc2, lat);
        IDXGISwapChain2_Release(sc2);
    }
    g_iq_on = on;
    fprintf(stderr, "[D3DB] host swap chain: %u buffers, %u frames of latency%s\n", nb, lat,
            FAILED(hr) ? " (not set: as it was)" : "");
}

static void flip_fit(void)
{
    unsigned w = g_fs ? (unsigned)g_client_w : g_vw, h = g_fs ? (unsigned)g_client_h - g_fs_buf_short : g_vh;
    HRESULT hr;

    REPLAY_SIDE();
    if (!w || !h || h > 0x7FFFFFFF || (w == g_buf_w && h == g_buf_h))
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
    HMONITOR mon;
    UINT32 np = 0, nm = 0, i;
    DISPLAYCONFIG_PATH_INFO *paths;
    DISPLAYCONFIG_MODE_INFO *modes;
    double hz = 0;

    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (g_hwnd)
        mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
    else {                              /* step 63: before the window (pacing at the start), the primary monitor's */
        POINT o = { 0, 0 };
        mon = MonitorFromPoint(o, MONITOR_DEFAULTTOPRIMARY);
    }
    if (!GetMonitorInfoA(mon, (MONITORINFO *)&mi) ||
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

void d3dh_set_apply(d3dh_apply_fn game, d3dh_apply_fn host)
{
    g_apply_game = game;
    g_apply_host = host;
}

void d3dh_record_wide(double wide)
{
    g_rwide = wide;
}

static volatile LONG g_ovl_closes;      /* times the overlay has closed (the controller gate) */
static volatile LONG g_ovl_session;     /* from the overlay's first frame until it has closed */

/* Step 82: the controller chord opens the overlay, as Esc does (the game's
 * poll; recomp_manual.c). */
void d3dh_overlay_pad_open(void)
{
    if (!g_ready || g_ovl_open)
        return;
    ovl_opened();
    InterlockedExchange(&g_ovl_open, 1);
}

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

    REPLAY_SIDE();
    if (FAILED(IDXGISwapChain_GetParent(d3d8_GetSwapChain(), &IID_IDXGIFactory2, (void **)&f)))
        return -1;
    if (g_media) {                      /* step 90: its reference keeps the old swap chain (and the window) */
        IDXGISwapChainMedia_Release(g_media);
        g_media = NULL;
    }
    g_media_tried = 0;
    IDXGISwapChain1_Release(g_flip);
    g_flip = NULL;
    if (g_latency) {
        CloseHandle(g_latency);
        g_latency = NULL;
    }
    if (g_vs_held) {                    /* step 63 */
        CloseHandle(g_vs_held);
        g_vs_held = NULL;
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
        g_iq_on = 0;                    /* step 61: made with the usual queue */
        IDXGIFactory2_MakeWindowAssociation(f, g_hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    } else {
        fprintf(stderr, "[D3DB] host swap chain not made again (0x%08lX): the host stops presenting\n",
                (unsigned long)hr);
        InterlockedExchange(&g_ready, 0);
    }
    IDXGIFactory2_Release(f);
    return g_flip && sd.Flags == flags ? 0 : -1;
}

/* Step 90: the visible swap chain made again after the display stalled
 * (the window thread asks, g_flip_remake), as it was; vsync pacing again if
 * the limiter stood in for it. With g_lock held, from the presenting thread,
 * after flip_fit. */
static void flip_remake(void)
{
    int held = g_vs_held != NULL;
    unsigned hi = g_vs_held_interval;
    int r;

    REPLAY_SIDE();
    r = recreate_flip(g_flip_flags);
    if (held && g_latency) {
        g_interval = hi;
        g_frame_hz = 0;
        pace_note("");
    }
    fprintf(stderr, "[D3DB] host swap chain made again after the display's stall: %ux%u%s%s\n", g_buf_w, g_buf_h,
            r == 0 ? "" : " (not as it was)", held && g_latency ? "; vsync pacing again" : "");
}

void d3dh_set_pacing(unsigned interval, double frame_hz)
{
    UINT want = interval && frame_hz <= 0 ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT : 0;

    REPLAY_SIDE();
    if (!g_ready)
        return;
    EnterCriticalSection(&g_lock);
    if (want != g_flip_flags && recreate_flip(want) != 0) {
        LeaveCriticalSection(&g_lock);
        fprintf(stderr, "[D3DB] host pacing unchanged: the swap chain couldn't be made for it\n");
        return;
    }
    if (want && !g_latency && g_vs_held) {      /* step 63: the limiter stood in after a stall: vsync again */
        g_latency = g_vs_held;
        g_vs_held = NULL;
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
    ID3D11ShaderResourceView *dsrv = NULL;
    HRESULT hr = S_OK;

    REPLAY_SIDE();
    if (w != g_dev_w || h != g_dev_h || n > 1 || g_ao_depth) {     /* step 92: a readable depth needs its own */
        hr = g_dev->lpVtbl->CreateTexture(g_dev, w, h, 1, D3DUSAGE_RENDERTARGET, (D3DFORMAT)XDK_FMT_LIN_A8B8G8R8,
                                          D3DPOOL_DEFAULT, &tex);
        if (SUCCEEDED(hr) && n > 1) {
            surf = ms_surface(w, h, (D3DFORMAT)XDK_FMT_LIN_A8B8G8R8, n);
            depth = !surf ? NULL : g_ao_depth ? ao_depth_surface(w, h, n, &dsrv) : ms_surface(w, h, D3DFMT_D24S8, n);
            if (!depth)
                hr = E_FAIL;
        } else if (SUCCEEDED(hr)) {
            hr = tex->lpVtbl->GetSurfaceLevel(tex, 0, &surf);
            if (SUCCEEDED(hr) && g_ao_depth) {
                depth = ao_depth_surface(w, h, 1, &dsrv);
                if (!depth)
                    hr = E_FAIL;
            } else if (SUCCEEDED(hr))
                hr = g_dev->lpVtbl->CreateDepthStencilSurface(g_dev, w, h, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &depth);
        }
        if (FAILED(hr) || !surf || !depth) {
            if (dsrv)
                ID3D11ShaderResourceView_Release(dsrv);
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
    if (g_bb.dsrv)
        ID3D11ShaderResourceView_Release(g_bb.dsrv);
    g_bb.tex = tex;                     /* NULL at xbox_d3d8's own size: its back buffer again */
    g_bb.surf = surf;
    g_bb.depth = depth;
    g_bb.dsrv = dsrv;                   /* step 92 */
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

    REPLAY_SIDE();
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

void d3dh_resize(unsigned view_w, unsigned view_h, double wide)
{
    HRESULT hr;
    int i, view_changed;
    unsigned host_w, host_h;

    REPLAY_SIDE();
    if (!g_ready || !view_w || !view_h)
        return;
    EnterCriticalSection(&g_lock);
    g_wide = wide;
    scaled_size(view_w, view_h, g_rscale, &host_w, &host_h);   /* step 90: the render scale */
    view_changed = view_w != g_vw || view_h != g_vh;
    g_vw = view_w;
    g_vh = view_h;
    if (host_w == g_w && host_h == g_h) {
        LeaveCriticalSection(&g_lock);
        if (view_changed)
            PostMessageA(g_hwnd, WM_TWOC_FIT, 0, 0);
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
    {
        char sc[64] = "";
        if (g_rscale != 100 || host_w != view_w || host_h != view_h)
            snprintf(sc, sizeof(sc), "; render scale %ld%%, shown at %ux%u", g_rscale, view_w, view_h);
        fprintf(stderr, "[D3DB] host picture %ux%u -> %ux%u (%s)%s%s\n", g_w, g_h, host_w, host_h,
                wide > 0 ? "widescreen" : "4:3", g_bb.tex ? ", in a render target of its own" : "", sc);
    }
    g_w = host_w;
    g_h = host_h;
    LeaveCriticalSection(&g_lock);
    PostMessageA(g_hwnd, WM_TWOC_FIT, 0, 0);
}

/* Step 90: the render scale, live (the host side's part of a settings
 * change): the back buffer made again at that share of the render size. */
void d3dh_set_render_scale(int pct)
{
    REPLAY_SIDE();
    pct = pct < TWOC_RENDER_SCALE_MIN ? TWOC_RENDER_SCALE_MIN : pct > TWOC_RENDER_SCALE_MAX ? TWOC_RENDER_SCALE_MAX : pct;
    if (pct == g_rscale)
        return;
    InterlockedExchange(&g_rscale, pct);
    fprintf(stderr, "[OVL] render scale: %d%%\n", pct);
    d3dh_resize(g_vw, g_vh, g_wide);
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

/* ── Step 78: live settings, queued (notes 18.81) ─────────────────
 *
 * A change in the overlay (or the sub-frame trigger file's test switches)
 * that the frames depend on is queued, and each side applies it at its own
 * frame boundary, the way a render thread will need: the host side
 * (fullscreen, anisotropic filtering, anti-aliasing, the adaptive frame rate,
 * and the bridge's host part: the host picture's size, the pacing) on the
 * presenting side between frames, and the game side (the frame-rate setting,
 * which decides whether frames are recorded, and the bridge's game part: the
 * aspect its translation reads) on the game thread at its
 * Swap. On one thread both drain where the change used to be applied at
 * once (after an overlay frame, after the hold), the host side first.
 * Settings the frames don't depend on (the HUD, read at record time and
 * interlocked; audio, pads, controls, thread-safe already) still apply at
 * once. A slot carries the game side's settings it was recorded under
 * (fslot_t.set_gen); the replay side's must have reached them. */

static void sq_push(const twoc_settings_t *was, const twoc_settings_t *s)
{
    AcquireSRWLockExclusive(&g_sq.lock);
    if (!g_sq.gen)                      /* what was in effect before the first change */
        g_sq.side[0].was = g_sq.side[1].was = *was;
    g_sq.s = *s;
    g_sq.gen++;
    ReleaseSRWLockExclusive(&g_sq.lock);
}

/* The host side's part of a change, between frames. */
static void sq_apply_host(const twoc_settings_t *was, const twoc_settings_t *s)
{
    REPLAY_SIDE();
    if (!s->fullscreen != !was->fullscreen)
        PostMessageA(g_hwnd, WM_TWOC_FULLSCREEN, s->fullscreen ? 1 : 0, 0);
    if (s->anisotropy != was->anisotropy) {
        InterlockedExchange(&g_aniso, s->anisotropy);
        fprintf(stderr, "[D3DB] host: anisotropic filtering %s%dx\n", s->anisotropy > 1 ? "" : "off, ",
                s->anisotropy);
    }
    if (s->antialiasing != was->antialiasing)
        set_antialiasing(s->antialiasing);      /* step 58 */
    if (s->adaptive != was->adaptive) {         /* step 75: at the next present */
        d3dh_set_interp_adaptive(s->adaptive);
        fprintf(stderr, "[OVL] adaptive frame rate: %s\n", s->adaptive ? "on" : "off");
    }
    if (s->render_scale != was->render_scale)  /* step 90: the size drawn, the shown one kept */
        d3dh_set_render_scale(s->render_scale);
    if (g_apply_host && (s->widescreen != was->widescreen || s->wide_w != was->wide_w || s->wide_h != was->wide_h ||
                         strcmp(s->render_size, was->render_size) != 0 || s->pacing != was->pacing ||
                         s->framerate != was->framerate))
        g_apply_host(s);                /* d3d_bridge.c: the host picture's size, the pacing */
}

/* The game side's part, at the game's Swap. */
static void sq_apply_game(const twoc_settings_t *was, const twoc_settings_t *s)
{
    GAME_SIDE();
    if (s->framerate != was->framerate)
        d3dh_set_framerate(s->framerate);       /* step 61: from the frame after this one */
    if (g_apply_game && (s->widescreen != was->widescreen || s->wide_w != was->wide_w || s->wide_h != was->wide_h))
        g_apply_game(s);                /* d3d_bridge.c: the aspect */
}

/* Side `k` (0 the game's, 1 the host's) applies what was queued since it
 * last did. */
static void sq_drain(int k)
{
    twoc_settings_t s, was;
    int side;

    if (g_sq.side[k].gen == g_sq.gen)
        return;
    side = side_set(k ? SIDE_REPLAY : SIDE_GAME);
    AcquireSRWLockExclusive(&g_sq.lock);
    s = g_sq.s;
    was = g_sq.side[k].was;
    g_sq.side[k].was = s;
    g_sq.side[k].gen = g_sq.gen;
    ReleaseSRWLockExclusive(&g_sq.lock);
    if (k)
        sq_apply_host(&was, &s);
    else
        sq_apply_game(&was, &s);
    side_set(side);
}

/* What changed between `was` and `s`: queued for the frames (sq_drain), or
 * applied now; then `was` = `s`. */
static void overlay_apply(twoc_settings_t *was, const twoc_settings_t *s)
{
    if (!s->fullscreen != !was->fullscreen || s->anisotropy != was->anisotropy ||
        s->antialiasing != was->antialiasing || s->framerate != was->framerate || s->adaptive != was->adaptive ||
        s->widescreen != was->widescreen || s->wide_w != was->wide_w || s->wide_h != was->wide_h ||
        strcmp(s->render_size, was->render_size) != 0 || s->pacing != was->pacing ||
        s->render_scale != was->render_scale)
        sq_push(was, s);                /* step 78 */
    if (s->pause_unfocused != was->pause_unfocused)    /* step 90 */
        d3dh_set_pause_unfocused(s->pause_unfocused);
    if (s->fast_title != was->fast_title)
        fprintf(stderr, "[OVL] skip the title animation: %s from the next start\n", s->fast_title ? "on" : "off");
    if (s->hud != was->hud || s->hud_margin != was->hud_margin) {   /* step 65: from the next overlay pass */
        d3dh_set_hud(s->hud, s->hud_margin);
        fprintf(stderr, "[OVL] HUD position: %s, margin %d%%\n", s->hud ? "screen edges" : "centre (4:3)",
                s->hud_margin);
    }
    if (s->volume != was->volume)
        twoc_audio_set_volume(s->volume);
    if (s->music != was->music)
        twoc_audio_set_kind(1, s->music);
    if (s->effects != was->effects)
        twoc_audio_set_kind(0, s->effects);
    if (s->hrtf != was->hrtf)
        twoc_audio_set_hrtf(s->hrtf);
    if (s->pause != was->pause) {
        InterlockedExchange(&g_ovl_pauses, s->pause);   /* step 79: the game side looks at its Swap */
        fprintf(stderr, "[OVL] pause the game while open: %s\n", s->pause ? "on" : "off");
    }
    if (s->show_fps != was->show_fps) {     /* step 82: from the next present */
        InterlockedExchange(&g_show_fps, s->show_fps);
        fprintf(stderr, "[OVL] frame-rate counter: %s\n", s->show_fps ? "on" : "off");
    }
    if (s->startup_message != was->startup_message) {  /* step 86: off takes it away at once */
        InterlockedExchange(&g_startmsg, s->startup_message);
        fprintf(stderr, "[OVL] startup message: %s\n", s->startup_message ? "on" : "off");
    }
    if (s->render_thread != was->render_thread) {   /* step 79: at the game's next Swap */
        d3dh_set_render_thread(s->render_thread);
        fprintf(stderr, "[OVL] render thread: %s\n", twoc_rt_name(s->render_thread));
    }
    if (s->controller_input != was->controller_input) {
        pads_set_mode(s->controller_input);     /* step 70: SDL started or stopped */
        fprintf(stderr, "[OVL] controller input: %s\n", s->controller_input == PADS_SDL ? "SDL (all pads)" : "XInput only");
    }
    if (s->keyboard != was->keyboard || memcmp(&s->controls, &was->controls, sizeof(s->controls)) != 0) {
        controls_set(&s->controls, s->keyboard);    /* step 57: at the game's next poll */
        fprintf(stderr, "[OVL] controls: keyboard %s, controller map %s\n", s->keyboard ? "on" : "off",
                controls_pad_is_default(&s->controls) ? "the default" : "changed");
    }
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

/* Step 82: what is measured, for the overlay's Frame rate line and the
 * counter: the game's updates (guest swaps) and the frames presented a
 * second, over the last second, from a sample at every MEAS_STEP s of
 * presents (replay side). A gap (the overlay held the game, a load) starts
 * it again. */
#define MEAS_STEP 0.5
#define MEAS_N 3                        /* samples kept: the rates span (MEAS_N - 1) * MEAS_STEP s */
static struct {
    LONGLONG t[MEAS_N];                 /* QPC */
    unsigned swap[MEAS_N], pres[MEAS_N];
    unsigned n;                         /* samples taken since the start or a gap */
    int ok;
    double game, shown;
} g_meas;

static void meas_present(unsigned swap_no)
{
    LARGE_INTEGER q;
    double since;
    unsigned i;

    QueryPerformanceCounter(&q);
    since = g_meas.n ? (double)(q.QuadPart - g_meas.t[0]) / (double)g_qpf.QuadPart : MEAS_STEP;
    if (since < MEAS_STEP)
        return;
    if (since > 3 * MEAS_STEP) {        /* a gap: from here */
        g_meas.n = 0;
        g_meas.ok = 0;
    }
    for (i = MEAS_N - 1; i > 0; i--) {
        g_meas.t[i] = g_meas.t[i - 1];
        g_meas.swap[i] = g_meas.swap[i - 1];
        g_meas.pres[i] = g_meas.pres[i - 1];
    }
    g_meas.t[0] = q.QuadPart;
    g_meas.swap[0] = swap_no;
    g_meas.pres[0] = g_st.presents;
    if (++g_meas.n >= MEAS_N) {
        double dt = (double)(g_meas.t[0] - g_meas.t[MEAS_N - 1]) / (double)g_qpf.QuadPart;
        g_meas.n = MEAS_N;
        g_meas.game = (double)(g_meas.swap[0] - g_meas.swap[MEAS_N - 1]) / dt;
        g_meas.shown = (double)(g_meas.pres[0] - g_meas.pres[MEAS_N - 1]) / dt;
        g_meas.ok = 1;
    }
}

/* The present after Esc: the overlay's first frame. No lock held. */
static int ovl_begin(void)
{
    REPLAY_SIDE();
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
    g_ovs.fs_seen = fs_wanted();
    g_ovs.s.fullscreen = g_ovs.fs_seen;
    g_ovs.was = g_ovs.s;
    InterlockedExchange(&g_ovl_pauses, g_ovs.s.pause);     /* step 79 */
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
    int r = 0, chord_close;

    REPLAY_SIDE();
    QueryPerformanceCounter(&now);
    if (fs_wanted() != g_ovs.fs_seen) {        /* Alt+Enter while open */
        g_ovs.fs_seen = fs_wanted();
        g_ovs.s.fullscreen = g_ovs.was.fullscreen = g_ovs.fs_seen;
    }
    memset(&in, 0, sizeof(in));
    in.host_w = g_w;
    in.host_h = g_h;
    in.view_w = g_vw;                   /* step 90 */
    in.view_h = g_vh;
    in.fast_title_now = getenv("RECOMP_DEV_FAST_TITLE") && *getenv("RECOMP_DEV_FAST_TITLE") &&
                        strcmp(getenv("RECOMP_DEV_FAST_TITLE"), "0") != 0;
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
    if (ic_possible()) {                /* step 61/77 */
        in.interp_fps = fp_step_fps(0);
        in.interp_per = fp_step_per(0);
        in.interp_now = fp_step_fps(ia_k());        /* step 74 */
    }
    in.interp_note = g_fp.on ? g_fp.note : NULL;    /* step 77 */
    in.rt_on = g_thr_v != 0;            /* step 79 */
    in.rt_cores = g_rt_cores;
    in.rt_auto_cores = RT_AUTO_CORES;
    in.interp_on = g_interp && in.interp_fps > 0;
    in.meas_ok = g_meas.ok;             /* step 82 */
    in.game_ups = g_meas.game;
    in.shown_fps = g_meas.shown;
    chord_close = twoc_pad_overlay(&in.pad, &in.pad_lx, &in.pad_ly);    /* step 82: the pad, and the chord */
    if (in.interp_fps <= 0) {           /* step 63 */
        static char why[256];
        ic_why(why, sizeof(why));
        in.interp_why = why;
    }
    in.save_note = g_ovs.dirty ? "Saved to crash_twoc.ini when you resume." : g_ovs.note;
    if (SUCCEEDED(ID3D11Device_CreateRenderTargetView(d3d8_GetD3D11Device(), (ID3D11Resource *)dst, NULL, &rtv)))
        r = ovl_frame(rtv, g_buf_w, g_buf_h, overlay_scale(),
                      (float)((double)(now.QuadPart - g_ovs.last.QuadPart) / (double)g_ovs.freq.QuadPart),
                      &g_ovs.s, &in);
    if (rtv)
        ID3D11RenderTargetView_Release(rtv);
    g_ovs.last = now;
    g_ovs.frames++;
    return chord_close ? r | OVL_CLOSE : r;
}

/* Step 86: the startup message's opacity now (1 for its first 7 s from the
 * first frame, fading to 0 by 8 s), 0 when it is done, off, or the host
 * isn't drawing. Its first call is the first frame. */
static float startmsg_alpha(void)
{
    LARGE_INTEGER q;
    double ms;

    if (!g_startmsg || g_startmsg_t0 < 0)
        return 0.0f;
    QueryPerformanceCounter(&q);
    if (!g_startmsg_t0) {
        g_startmsg_t0 = q.QuadPart;
        fprintf(stderr, "[D3DB] host startup message: \"Press F1 to open the settings menu\" (with a controller,"
                " \"or hold Start\") for %.0f s from this frame\n", STARTMSG_MS / 1000.0);
    }
    ms = (double)(q.QuadPart - g_startmsg_t0) * 1000.0 / (double)g_qpf.QuadPart;
    if (ms >= STARTMSG_MS) {
        g_startmsg_t0 = -1;             /* done for this run */
        return 0.0f;
    }
    return ms <= STARTMSG_MS - STARTMSG_FADE_MS ? 1.0f : (float)((STARTMSG_MS - ms) / STARTMSG_FADE_MS);
}

/* Step 82: the frame-rate counter over the picture in `dst` (the visible
 * swap chain's buffer), the overlay closed. With g_lock held. Step 86: and
 * the startup message at opacity `msg_alpha` (0: none). */
static void counter_draw(ID3D11Texture2D *dst, float msg_alpha)
{
    extern int twoc_pad_connected(void);
    ID3D11RenderTargetView *rtv = NULL;
    char lines[96];
    float pic[5];
    double k;

    REPLAY_SIDE();
    if (!g_ovs.ready) {
        g_ovs.ready = ovl_init(d3d8_GetD3D11Device(), d3d8_GetD3D11Context()) == 0 && blit_init() == 0 ? 1 : -1;
        if (g_ovs.ready < 0)
            fprintf(stderr, "[OVL] the frame-rate counter can't be drawn here\n");
    }
    if (g_ovs.ready < 0)
        return;
    ovl_rates_text(g_meas.ok, g_meas.game, g_meas.shown, 1, lines, sizeof(lines));
    /* the picture inside the buffer, as blit_draw places it (the whole buffer when it is the picture's size) */
    k = (double)g_buf_w / g_w < (double)g_buf_h / g_h ? (double)g_buf_w / g_w : (double)g_buf_h / g_h;
    pic[2] = (float)(int)(g_w * k + 0.5);
    pic[3] = (float)(int)(g_h * k + 0.5);
    pic[0] = (float)(int)((g_buf_w - pic[2]) / 2);
    pic[1] = (float)(int)((g_buf_h - pic[3]) / 2);
    pic[4] = 1.15f * (pic[3] > 490 ? pic[3] / 650.0f : 0.75f);  /* the counter's rule by the picture's height, +15% */
    if (SUCCEEDED(ID3D11Device_CreateRenderTargetView(d3d8_GetD3D11Device(), (ID3D11Resource *)dst, NULL, &rtv))) {
        /* sized by the picture, not the desktop's DPI: 22 px text at 800 lines */
        ovl_counter(rtv, g_buf_w, g_buf_h, g_buf_h > 490 ? (float)g_buf_h / 650.0f : 0.75f,
                    g_show_fps ? lines : NULL,
                    twoc_pad_connected() ? "Press F1 to open the settings menu\n(or hold Start on the controller)"
                                         : "Press F1 to open the settings menu", msg_alpha, pic);
        ID3D11RenderTargetView_Release(rtv);
    }
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

    REPLAY_SIDE();
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
    wd_rt(WD_R_OVERLAY, 0);
    while (g_ovl_open && g_ready && g_ovs.s.pause) {
        ID3D11Texture2D *dst = NULL;
        int r = 0;

        if (g_latency || g_frame_hz > 0)
            pace(1, 1.0);
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
        sq_drain(1);                    /* step 78: the host side's settings, between overlay frames */
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

    REPLAY_SIDE();
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

    REPLAY_SIDE();
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

/* Step 90 (notes 18.93): how DXGI presents the visible swap chain, from
 * IDXGISwapChainMedia::GetFrameStatisticsMedia's CompositionMode, read 4
 * times a second at a present: DWM composes it, a hardware overlay plane
 * shows it, "none" or a composition failure; counted for the 10-s interval
 * and a line when it changes (the runs that stuttered after the startup
 * stall may have been in another mode than the clean ones, notes 18.92).
 * With g_lock held, after the present. */
static const char *const g_mode_name[TWOC_PMODES] = {
    "composed (DWM)", "overlay (a hardware plane)", "none", "composition failure", "not read",
};

static void present_mode(double t_s)
{
    static LONGLONG last_q;
    static int last = -1, fails;
    DXGI_FRAME_STATISTICS_MEDIA m;
    LARGE_INTEGER q;
    HRESULT hr;
    int mode;

    REPLAY_SIDE();
    QueryPerformanceCounter(&q);
    if (last_q && q.QuadPart - last_q < g_qpf.QuadPart / 4)
        return;
    last_q = q.QuadPart;
    if (!g_media_tried) {
        g_media_tried = 1;
        if (FAILED(IDXGISwapChain1_QueryInterface(g_flip, &IID_IDXGISwapChainMedia, (void **)&g_media)))
            g_media = NULL;
        if (!g_media && last == -1)
            fprintf(stderr, "[D3DB] host presentation mode: no IDXGISwapChainMedia here; not read\n");
    }
    mode = TWOC_PMODES - 1;
    if (g_media) {
        memset(&m, 0, sizeof(m));
        hr = IDXGISwapChainMedia_GetFrameStatisticsMedia(g_media, &m);
        if (SUCCEEDED(hr))
            mode = m.CompositionMode <= DXGI_FRAME_PRESENTATION_MODE_COMPOSITION_FAILURE ? (int)m.CompositionMode
                                                                                         : TWOC_PMODES - 1;
        else if (!fails++)
            fprintf(stderr, "[D3DB] host presentation mode: GetFrameStatisticsMedia 0x%08lX (counted as not read)\n",
                    (unsigned long)hr);
    }
    g_st.pmode[mode]++;
    g_st.pmode_now = (unsigned)mode;
    if (mode != TWOC_PMODES - 1 && mode != last) {
        fprintf(stderr, "[D3DB] host presentation mode: %s%s%s at present %u, +%.1f s (%s)\n", g_mode_name[mode],
                last >= 0 ? ", was " : "", last >= 0 ? g_mode_name[last] : "", g_st.presents, t_s,
                fs_wanted() ? (g_fs ? "fullscreen" : "fullscreen asked for, in the window") : "in a window");
        last = mode;
    }
}

/* One present: the back buffer (as drawn now) dumped when due and shown at
 * DXGI `interval`, after its wait. Step 61: sub-frame `sub` of `k` (k 1: the
 * frame itself; only the last of a frame is a frame's dump). Returns the
 * running overlay's OVL_* bits. */
static int present_one(double t_s, unsigned swap_no, unsigned interval, unsigned sub, unsigned k, int mid,
                       double slots)
{
    ULONGLONG now;
    HRESULT hr;
    int ovl_r = 0;
    ID3D11Texture2D *src;

    REPLAY_SIDE();
    {
        LARGE_INTEGER p0, p1;
        wd_rt(WD_R_PACE, swap_no);
        QueryPerformanceCounter(&p0);
        pace(mid, slots);
        QueryPerformanceCounter(&p1);
        g_ia.pace_ms += (double)(p1.QuadPart - p0.QuadPart) * 1000.0 / (double)g_qpf.QuadPart;   /* step 74 */
    }
    wd_rt(WD_R_PRESENT, swap_no);
    EnterCriticalSection(&g_lock);
    src = frame_tex();                  /* step 58: resolved, and through FXAA or SMAA if one is on */
    now = GetTickCount64();
    if (g_dump_prefix[0] && sub && swap_no >= g_subdump_from && swap_no <= g_subdump_to) {
        char path[600];                 /* step 61: RECOMP_D3D_BRIDGE_SUBDUMP, every sub-frame */
        snprintf(path, sizeof(path), "%ssub%05u_%uof%u.bmp", g_dump_prefix, swap_no, sub, k);
        dump_back_buffer(path, src);
    }
    if (g_dump_prefix[0] && sub == k &&
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
    if (g_flip_remake && InterlockedExchange(&g_flip_remake, 0))
        flip_remake();                  /* step 90: after the display's stall */
    if (!g_flip) {
        if (src)
            ID3D11Texture2D_Release(src);
        LeaveCriticalSection(&g_lock);
        return 0;
    }
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
            else {
                float ma = startmsg_alpha();    /* step 86: the startup message, like the counter */
                if (g_show_fps || ma > 0.0f)
                    counter_draw(dst, ma);  /* step 82: on the window's picture only, never in a dump */
            }
        }
        if (src)
            ID3D11Texture2D_Release(src);
        if (dst)
            ID3D11Texture2D_Release(dst);
    }
    {
        LARGE_INTEGER p0;
        QueryPerformanceCounter(&p0);
        InterlockedExchange64(&g_present_since, p0.QuadPart);  /* step 90: the window thread watches */
        if (!g_first_present_q)
            g_first_present_q = p0.QuadPart;
    }
    hr = IDXGISwapChain1_Present(g_flip, interval, 0);
    InterlockedExchange64(&g_present_since, 0);
    g_st.presents++;
    meas_present(swap_no);              /* step 82 */
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
        static unsigned char ivl[64];   /* step 74: each present's interval, by present count mod 64 */
        static float gap[64];           /* step 77: under the limiter, ms presented after the one before */
        static LONGLONG last_sync;
        DXGI_FRAME_STATISTICS fs;
        DWM_TIMING_INFO ti;
        LARGE_INTEGER q, f;

        if (log_on < 0) {
            const char *e = getenv("RECOMP_D3D_BRIDGE_DISPLAYLOG");
            log_on = e && *e && *e != '0';
        }
        ivl[g_st.presents & 63] = (unsigned char)interval;
        gap[g_st.presents & 63] = g_fp.on && g_fp.limiter ? (float)(slots * 1000.0 / g_fp.slot_hz) : 0.0f;
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
                UINT dn = last_pc && fs.PresentCount > last_pc ? fs.PresentCount - last_pc : 1;
                InterlockedAdd(&g_shown_seq, (LONG)(dn > 16 ? 16 : dn));  /* step 90 */
                if (last_pc && fs.PresentCount == last_pc + 1) {
                    UINT d = fs.PresentRefreshCount - last_prc;
                    g_st.displayed[d > 8 ? 8 : d]++;
                    if (gap[fs.PresentCount & 63] > 0 && last_sync) {
                        /* step 77: the limiter's gaps aren't refreshes: when shown against when presented, in
                         * ms (variable refresh shows each as presented; a fixed refresh, on its grid) */
                        double ms = (double)(fs.SyncQPCTime.QuadPart - last_sync) * 1000.0 / f.QuadPart;
                        double want = gap[fs.PresentCount & 63];
                        g_st.disp_ms_n++;
                        g_st.disp_ms_dev += fabs(ms - want);
                        g_st.disp_ms_even += fabs(ms - want) < 1.0;
                        vrr_sample(ms, want, 1000.0 / g_fp.disp_hz);
                    } else if (d != ivl[fs.PresentCount & 63])
                        g_st.display_off++;     /* step 74: not the gap it was presented with */
                    if (log_on && d != ivl[fs.PresentCount & 63]) {
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
                last_sync = fs.SyncQPCTime.QuadPart;
            }
        } else {
            g_st.display_unmeasured++;
        }
    }
    present_mode(t_s);                  /* step 90 */
    if (g_fs_defer == 1 && !g_vs_held && !g_disp_recovering) {
        /* Step 90: fullscreen asked for at the start (or after a stall's recovery): once FS_DEFER_SHOWN presents
         * have been shown, the window thread switches, as Alt+Enter would. Without frame statistics at all, after
         * FS_DEFER_MAX_MS of presents with the display's vsync taking them. */
        LONG n = g_shown_seq - g_fs_defer_base;
        LARGE_INTEGER q;
        double ms;
        QueryPerformanceCounter(&q);
        ms = g_first_present_q ? (double)(q.QuadPart - g_first_present_q) * 1000.0 / (double)g_qpf.QuadPart : 0;
        if ((n >= g_fs_defer_n || (g_shown_seq == 0 && ms >= FS_DEFER_MAX_MS)) &&
            InterlockedCompareExchange(&g_fs_defer, 2, 1) == 1) {
            fprintf(stderr, "[D3DB] host window: %ld frames shown (+%.1f s, present %u): borderless fullscreen now%s\n",
                    (long)n, t_s, g_st.presents, n >= g_fs_defer_n ? "" : " (no frame statistics)");
            PostMessageA(g_hwnd, WM_TWOC_FULLSCREEN, 3, 0);
        }
    }
    if (FAILED(hr) && ++g_st.present_fails <= 5)
        fprintf(stderr, "[D3DB] host Present failed 0x%08lX\n", (unsigned long)hr);
    else if (hr != S_OK && ++g_st.present_status <= 5)   /* e.g. DXGI_STATUS_OCCLUDED: not shown */
        fprintf(stderr, "[D3DB] host Present returned status 0x%08lX\n", (unsigned long)hr);
    if (sub == k)
        g_frame_work = 0;
    LeaveCriticalSection(&g_lock);
    return ovl_r;
}

/* Step 77: a present `slots` of the plan's clock after the one before: the
 * limiter waits that many ticks; under vsync it is that DXGI interval, the
 * same picture again for each 4 refreshes past DXGI's longest. */
static int present_slots(double t_s, unsigned swap_no, double slots, unsigned sub, unsigned k, int mid)
{
    unsigned n;
    int r = 0;

    REPLAY_SIDE();
    if (g_fp.limiter)
        return present_one(t_s, swap_no, 1, sub, k, mid, slots);
    n = (unsigned)(slots + 0.5);
    n = n < 1 ? 1 : n;
    for (; n > 4; n -= 4, mid = 0)
        r |= present_one(t_s, swap_no, 4, sub, k, mid, 4.0);
    return r | present_one(t_s, swap_no, n, sub, k, mid, (double)n);
}

/* ── Step 78: the frame plan, the replay side's unit (notes 18.81) ──────
 *
 * Step 77's plan (g_fp: the slots, the steps) and its clock (g_fc), the
 * time-based sub-frames (fp_frame), the adaptive ladder (g_ia, ia_frame:
 * what the frames cost, the step the next gets, "busy") and the presents
 * (present_slots, present_one, pace) are the replay side's, used from here
 * and the overlay and pacing code beside it. From the game side they read
 * only what the slot brings (fslot_t): the swap number and the Swap's time
 * (dumps, logs), whether frames were recorded and for interpolation (the
 * frame-rate setting as the game side took it, ic_apply), and the game
 * side's time on the frame (busy). The plan itself is made on the host side
 * (d3dh_frame_plan, from the bridge's host part of a settings change, at
 * the start, and from replay_tail on a new display mode or a measurement):
 * it reads the display, the window mode and the setting, nothing of the
 * game's. Called by the game thread's Swap on one thread; a render thread's
 * loop, with the slot it takes, later. Returns the running overlay's OVL_*
 * bits. */
static int replay_frame(fslot_t *f)
{
    int ovl_r = 0, interp = 0;
    unsigned sub, swap_no = f->swap_no;
    double t_s = f->t_s;
    fp_frame_t fr;

    REPLAY_SIDE();
    fr.n = 0;
    if (f->rec || g_ia.gen || g_fp.on) {    /* step 60: the frame's recorded commands, drawn now */
        wd_rt(WD_R_PASS, swap_no);
        EnterCriticalSection(&g_lock);
        slot_prepare(f, 0);             /* step 78: its textures uploaded and pinned */
        ic_queue(f->interp && ic_possible());       /* step 61 */
        if (f->interp || g_ia.gen)
            ia_frame(f);                /* step 74: the frame before measured, this one's step chosen */
        if (fp_active())                /* step 77: this game frame's presents on the plan's clock */
            fp_frame(&fr, f->interp && ic_possible() ? ia_k() : g_fp.nsteps - 1);
        if (f->interp) {
            /* Step 79: on the render thread the overlay holds only the frames the game stopped after (it
             * looked at its Swap); one handed over before Esc goes on interpolated, the overlay not yet
             * drawn, unless it is the one that opens the overlay. */
            int held = !t_rt ? g_ovl_open && (!g_ovs.active || g_ovs.s.pause)
                             : f->hold ? g_ovl_open : g_ovl_open && !g_ovs.active;
            interp = ic_begin(swap_no, held, fr.n && fr.interp);    /* step 61 */
            if (!held && ic_possible()) {
                g_ia.kused = interp ? fr.n : 1;     /* step 74: this frame is measured at the next present */
                g_ia.paired = interp;
                g_st.interp_at[ia_k() < 8 ? ia_k() : 7]++;
            }
        }
        g_bk_pass = 0;                  /* step 87: the passes it gets */
        g_bk_plan = interp ? fr.n : (unsigned)(g_replay > 1 ? g_replay : 1);
        if (interp) {
            ic_sub(g_interp_t0 ? fr.t0[0] : fr.t[0]);
            replay_pass(1);             /* the first sub-frame */
            if (fr.n == 1) {
                replay_done();
                ic_end();
            }
        } else {
            if (g_replay_check)
                replay_check(0, swap_no);
            replay_run();
            if (g_replay_check)
                replay_check(1, swap_no);
            if (f->interp)
                ic_end();
        }
        LeaveCriticalSection(&g_lock);
    } else if (g_ps) {
        slot_free(g_ps);                /* step 78: nothing recorded: the slot free again */
    }
    if (interp) {                       /* step 61: the presents, the game held in its Swap */
        for (sub = 1; sub <= fr.n; sub++) {
            if (sub > 1) {
                wd_rt(WD_R_PASS, swap_no);
                EnterCriticalSection(&g_lock);
                if (fr.t[sub - 1] < 1) {
                    ic_sub(g_interp_t0 ? fr.t0[sub - 1] : fr.t[sub - 1]);
                    replay_pass(1);
                } else {
                    replay_pass(0);     /* t = 1: the frame as recorded */
                }
                if (sub == fr.n) {
                    replay_done();
                    ic_end();
                }
                LeaveCriticalSection(&g_lock);
            }
            /* step 77: a present held 3 refreshes or more (80 fps at 240 Hz) is held to mid-refresh too:
             * sent the moment the queue had room, 13% were shown 2 or 4 refreshes after the one before.
             * Step 79: and every present on the render thread: its queue stays full (no game work pauses
             * the presents now), so each went out just after a vblank, where DWM wavers (18.48): 1.6-2.0%
             * shown off their gap upstairs in a 1280x720 window, 0.0-0.1% held, at no cost */
            ovl_r |= present_slots(t_s, swap_no, fr.slots[sub - 1], sub, fr.n,
                                   fr.n == 1 || g_imid || t_rt || (!g_fp.limiter && fr.slots[sub - 1] >= 3));
            g_st.interp_subs++;
        }
    } else {
        if (g_ovl_open && !g_ovs.active) {  /* step 54: Esc */
            AcquireSRWLockExclusive(&g_pause_lock);
            if (g_ovl_open && !g_ovs.active)
                ovl_begin();
            ReleaseSRWLockExclusive(&g_pause_lock);
        }
        if (g_ovl_open && g_ovs.active && g_ovs.s.pause && (!t_rt || f->hold))
            overlay_pause();            /* this frame waits under the overlay */
        rt_let_go(f);                   /* step 79: the game waited in its Swap meanwhile */
        if (fr.n) {                     /* step 77: one present to the game frame's last slot */
            double slots = 0;
            for (sub = 0; sub < fr.n; sub++)
                slots += fr.slots[sub];
            ovl_r = present_slots(t_s, swap_no, slots, f->interp ? 1 : 0, f->interp ? 1 : 0, 1);
        } else {
            ovl_r = present_one(t_s, swap_no, g_interval, f->interp ? 1 : 0, f->interp ? 1 : 0, 1, 1.0);
        }
    }
    {   /* step 78: the replay side's busy time on the frame (the adaptive measure's half) */
        LARGE_INTEGER q;
        QueryPerformanceCounter(&q);
        if (g_ia.rp0)
            g_ia.replay_ms = (double)(q.QuadPart - g_ia.rp0) * 1000.0 / (double)g_qpf.QuadPart - g_ia.pace_ms;
    }
    rt_let_go(f);
    return ovl_r;
}

/* After the frame's presents, the replay side's: a new plan if the display
 * asked for one, the overlay's frame applied, the host side's settings
 * (step 79: on the render thread, those of the next frame before it is
 * replayed instead, sq_apply_slot). */
static void replay_tail(int ovl_r)
{
    REPLAY_SIDE();
    wd_rt(WD_R_TAIL, 0);
    if (g_fp.on && (InterlockedExchange(&g_fp_replan, 0) || (g_fp.needs_vrr && g_fp.fs != (g_fs ? 1 : 0)))) {
        unsigned iv;                    /* step 77: variable refresh measured, the display's mode changed, or a */
        double hz;                      /* plan that counts on variable refresh in the other window mode */
        if (d3dh_frame_plan(g_fp.fr, g_fp.game_hz, &iv, &hz))
            d3dh_set_pacing(iv, hz);
    }
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
    if (!t_rt)
        sq_drain(1);                    /* step 78: the frame's boundary: settings queued, the host side's */
}

/* The sub-frame trigger file (RECOMP_D3D_BRIDGE_SUBDUMP=@FILE, test aids):
 * N sub-frame dumps, "framerate 0|1" and "adaptive 0|1" as the overlay
 * changes them, and (step 78) "set KEY VALUE" lines applied as the
 * overlay's at the frame's end. The replay side's (step 79: the render
 * thread's, with it; the switches are then queued like any change). */
static void trigger_file(unsigned swap_no)
{
    FILE *f = fopen(g_subdump_trigger, "r");
    unsigned n = 0;
    int fr = -1, ad = -1, set = 0;
    twoc_settings_t ts;

    REPLAY_SIDE();
    if (!g_ovs.have) {
        twoc_settings_current(&g_ovs.s);
        g_ovs.s.fullscreen = fs_wanted();
        g_ovs.was = g_ovs.s;
        g_ovs.have = 1;
    }
    ts = g_ovs.s;
    if (f) {
        char line[64] = "";
        if (fgets(line, sizeof(line), f) && sscanf(line, "framerate %d", &fr) != 1 &&
            sscanf(line, "adaptive %d", &ad) != 1 && sscanf(line, "%u", &n) != 1)
            n = 0;                      /* "framerate 0|1", "adaptive 0|1" (step 75): test aids, the settings as */
        do {                            /* the overlay changes them; step 78: "set KEY VALUE" lines too, */
            char k[16], v[40];          /* applied as the overlay's at the frame's end (a live change) */
            if (sscanf(line, "set %15s %39s", k, v) != 2)
                continue;
            set = 1;
            if (strcmp(k, "aspect") == 0) {
                ts.widescreen = sscanf(v, "%lf:%lf", &ts.wide_w, &ts.wide_h) == 2;
            } else if (strcmp(k, "render") == 0) {
                snprintf(ts.render_size, sizeof(ts.render_size), "%s", strcmp(v, "0") == 0 ? "" : v);
            } else if (strcmp(k, "aa") == 0) {
                ts.antialiasing = twoc_aa_parse(v);
            } else if (strcmp(k, "aniso") == 0) {
                ts.anisotropy = atoi(v);
            } else if (strcmp(k, "framerate") == 0) {
                ts.framerate = twoc_framerate_parse(v);
            } else if (strcmp(k, "adaptive") == 0) {
                ts.adaptive = atoi(v) != 0;
            } else if (strcmp(k, "fullscreen") == 0) {
                ts.fullscreen = atoi(v) != 0;
            } else if (strcmp(k, "pacing") == 0) {
                ts.pacing = twoc_pacing_parse(v);
            } else if (strcmp(k, "startmsg") == 0) {        /* step 86: startup_message */
                ts.startup_message = atoi(v) != 0;
            } else if (strcmp(k, "pause") == 0) {           /* step 79 */
                ts.pause = atoi(v) != 0;
            } else if (strcmp(k, "rthread") == 0) {         /* step 79 (st.py turns '_' into spaces) */
                ts.render_thread = twoc_rt_parse(v);
            } else if (strcmp(k, "showfps") == 0) {         /* step 82 */
                ts.show_fps = atoi(v) != 0;
            } else if (strcmp(k, "scale") == 0) {           /* step 90: render_scale */
                ts.render_scale = twoc_render_scale_parse(v);
            } else if (strcmp(k, "aoradius") == 0) {        /* step 92's test aids: at the next pass */
                if (atof(v) > 0)
                    g_ao_radius = (float)atof(v);
            } else if (strcmp(k, "aodenoise") == 0) {
                g_ao_denoise = atoi(v) > 0 ? atoi(v) : g_ao_denoise;
            } else if (strcmp(k, "pausebg") == 0) {         /* step 90: pause_unfocused */
                ts.pause_unfocused = atoi(v) != 0;
            } else if (strcmp(k, "preset") == 0) {          /* step 91: as the overlay's Preset */
                ts.preset = twoc_preset_parse(v);
                twoc_preset_apply(&ts, ts.preset);
            } else {
                fprintf(stderr, "[D3DB] trigger file: set %s: not a setting here\n", k);
            }
        } while (fgets(line, sizeof(line), f));
        fclose(f);
    }
    DeleteFileA(g_subdump_trigger);
    if (fr >= 0 || ad >= 0) {           /* step 63: through overlay_apply, as the overlay does */
        if (fr >= 0)
            g_ovs.s.framerate = fr;
        if (ad >= 0)
            g_ovs.s.adaptive = ad != 0;
        overlay_apply(&g_ovs.was, &g_ovs.s);
        if (!t_rt) {
            sq_drain(1);                /* step 78: at once, as before */
            sq_drain(0);
        }
    } else if (set) {                   /* step 78: queued, at this present's end */
        fprintf(stderr, "[D3DB] trigger file: settings changed at guest swap %u\n", swap_no);
        g_ovs.s = ts;
        overlay_apply(&g_ovs.was, &g_ovs.s);
    }
    n = fr >= 0 || ad >= 0 || set ? 0 : n ? n : 8;
    if (n) {
        g_subdump_from = swap_no;
        g_subdump_to = swap_no + n - 1;
        fprintf(stderr, "[D3DB] host sub-frame dumps: guest swaps %u-%u\n", g_subdump_from, g_subdump_to);
    }
}

/* ── Step 79: the render thread's loop (notes 18.82) ─────────────────
 *
 * The game thread, at the guest's Swap (present_thread): once the render
 * thread has taken the frame handed over before, the frame it recorded is
 * handed over (its slot SL_PEND, g_pend) and the render thread woken; then
 * it waits until the slot it records into next is free (the render thread
 * has replayed the frame before: hand-over depth 1), until the
 * overlay lets it go if it stopped at this frame (f->hold: Esc with "Pause
 * the game" on, seen at its Swap), and in the sync check until this frame is
 * done too; then it takes the interpolation table set neither frame uses,
 * applies the game side's settings and goes on. The render thread (rt_main)
 * takes each frame handed over, applies the host side's part of the
 * settings it was recorded under (sq_apply_slot: the game side's apply at
 * its Swap, so a frame is always replayed under the settings it was recorded
 * for), and replays it as one thread did inside the Swap: uploads and pins,
 * the plan, the pairing, the passes and the presents, the overlay and its
 * hold (presenting overlay frames while the game waits), the plan made again,
 * the overlay's end. A slot is free once its last pass is drawn, before the
 * last present's wait, so the game records its next frame meanwhile.
 *
 * The frame-rate switch, and the setting turning the thread off, happen at
 * the game's Swap with the thread drained (rt_leave: nothing handed over, its
 * last frame done); then the game thread replays again as before. With the
 * thread on and the setting allowing it the game thread hands over from its
 * next Swap (rt_engage). Frame rate: Game (60) never starts it.
 *
 * Deadlock watchdog (rt_watchdog): if the game thread waits in its Swap, or
 * the render thread stays in one stage, for longer than
 * RECOMP_D3D_BRIDGE_RT_WATCHDOG seconds (5; 0 off), neither held by the
 * overlay, a [D3DB] render thread watchdog block says where each thread is
 * (stage, guest swap, how long, its instruction pointer and the code
 * addresses on top of its stack, as module+offset). It only logs. */
static HANDLE g_rt_th, g_wd_th;

/* What the render thread says it is doing (`what`), its instruction pointer
 * and the code addresses near the top of its stack, into `buf`. The thread is
 * suspended only while its context and stack are copied (nothing that could
 * wait on a lock it holds); the modules are looked up after. */
static void wd_where(DWORD tid, char *buf, size_t cap)
{
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    CONTEXT c;
    DWORD64 stack[64], pc[9];
    unsigned i, n = 0, k;
    size_t len = 0;
    SIZE_T got = 0;

    buf[0] = 0;
    if (!h) {
        snprintf(buf, cap, "(thread %lu not opened: %lu)", tid, GetLastError());
        return;
    }
    memset(&c, 0, sizeof(c));
    c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (SuspendThread(h) == (DWORD)-1) {
        CloseHandle(h);
        snprintf(buf, cap, "(thread %lu not suspended)", tid);
        return;
    }
    if (GetThreadContext(h, &c))
        ReadProcessMemory(GetCurrentProcess(), (LPCVOID)c.Rsp, stack, sizeof(stack), &got);
    ResumeThread(h);
    CloseHandle(h);
    pc[n++] = c.Rip;
    for (i = 0; i < got / 8 && n < 9; i++) {    /* return addresses: words that point into a loaded module's code */
        HMODULE m;
        if (stack[i] > 0x10000 &&
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)stack[i], &m))
            pc[n++] = stack[i];
    }
    for (k = 0; k < n && len + 64 < cap; k++) {
        HMODULE m = NULL;
        char name[MAX_PATH] = "?", *b;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)pc[k], &m))
            GetModuleFileNameA(m, name, sizeof(name));
        b = strrchr(name, '\\');
        len += (size_t)snprintf(buf + len, cap - len, "%s%s+0x%llX", k ? (k == 1 ? "; stack " : " ") : "rip ",
                                b ? b + 1 : name, m ? (unsigned long long)(pc[k] - (DWORD64)m) : (unsigned long long)pc[k]);
    }
}

static DWORD WINAPI rt_watchdog(LPVOID p)
{
    LONG64 tripped[2] = { 0, 0 };

    (void)p;
    for (;;) {
        LARGE_INTEGER q;
        int who, trip = -1;
        Sleep(500);
        QueryPerformanceCounter(&q);
        for (who = 0; who < 2 && trip < 0; who++) {
            LONG what = g_wd[who].what;
            LONG64 since = g_wd[who].since;
            int waiting = who == 0 ? what != WD_G_RUN && !(what == WD_G_HOLD && g_ovl_open)
                                   : what != WD_R_IDLE && what != WD_R_OVERLAY;
            if (waiting && since && since != tripped[who] &&
                (double)(q.QuadPart - since) / (double)g_qpf.QuadPart > g_wd_secs)
                trip = who;
        }
        if (trip >= 0) {
            char where[2][768];
            tripped[0] = g_wd[0].since;     /* one block an episode: neither thread trips again in these stages */
            tripped[1] = g_wd[1].since;
            for (who = 0; who < 2; who++)
                wd_where(g_wd[who].tid, where[who], sizeof(where[who]));
            fprintf(stderr, "[D3DB] render thread watchdog: the %s thread has been %s for %.1f s (guest swap %ld)\n"
                    "[D3DB] render thread watchdog: game thread %lu: %s for %.1f s, guest swap %ld; %s\n"
                    "[D3DB] render thread watchdog: render thread %lu: %s for %.1f s, guest swap %ld; %s\n"
                    "[D3DB] render thread watchdog: handed over %s, render thread %s, overlay %s\n",
                    trip ? "render" : "game", g_wd_what[g_wd[trip].what],
                    (double)(q.QuadPart - g_wd[trip].since) / (double)g_qpf.QuadPart, (long)g_wd[trip].swap,
                    g_wd[0].tid, g_wd_what[g_wd[0].what], (double)(q.QuadPart - g_wd[0].since) / (double)g_qpf.QuadPart,
                    (long)g_wd[0].swap, where[0],
                    g_wd[1].tid, g_wd_what[g_wd[1].what], (double)(q.QuadPart - g_wd[1].since) / (double)g_qpf.QuadPart,
                    (long)g_wd[1].swap, where[1], g_pend ? "a frame" : "nothing", g_rt_busy ? "busy" : "idle",
                    g_ovl_open ? "open" : "closed");
            fflush(stderr);
        }
    }
    return 0;
}

/* The game may go on after frame f (rt_let_go): it waited in its Swap while
 * the overlay held the game at f. */
static void rt_let_go(const fslot_t *f)
{
    if (!t_rt || g_hold_done >= f->seq)
        return;
    EnterCriticalSection(&g_ho);
    g_hold_done = f->seq;
    LeaveCriticalSection(&g_ho);
    WakeAllConditionVariable(&g_ho_game);
}

/* The host side's part of the settings frame f was recorded under, applied
 * before it is replayed, if not yet. */
static void sq_apply_slot(const fslot_t *f)
{
    twoc_settings_t was;

    if (!f->set_gen || f->set_gen <= g_sq.side[1].gen)
        return;
    AcquireSRWLockExclusive(&g_sq.lock);
    was = g_sq.side[1].was;
    g_sq.side[1].was = f->set;
    g_sq.side[1].gen = f->set_gen;
    ReleaseSRWLockExclusive(&g_sq.lock);
    sq_apply_host(&was, &f->set);
}

static DWORD WINAPI rt_main(LPVOID p)
{
    (void)p;
    t_rt = 1;
    side_set(SIDE_REPLAY);
    for (;;) {
        fslot_t *f;
        int ovl_r;
        EnterCriticalSection(&g_ho);
        g_rt_busy = 0;
        WakeAllConditionVariable(&g_ho_game);
        wd_rt(WD_R_IDLE, 0);
        if (!g_pend) {
            LARGE_INTEGER i0, i1;
            QueryPerformanceCounter(&i0);
            while (!g_pend)
                SleepConditionVariableCS(&g_ho_rt, &g_ho, INFINITE);
            QueryPerformanceCounter(&i1);
            g_st.rt_idle_ms += (double)(i1.QuadPart - i0.QuadPart) * 1000.0 / (double)g_qpf.QuadPart;
        }
        f = g_pend;
        g_pend = NULL;
        f->state = SL_REPLAY;
        g_rt_busy = 1;
        LeaveCriticalSection(&g_ho);
        WakeAllConditionVariable(&g_ho_game);  /* the game may hand over its next frame */
        QueryPerformanceCounter(&g_rt_take);
        wd_rt(WD_R_TAKE, f->swap_no);
        g_st.rt_frames++;
        if (!g_ready) {                 /* the swap chain couldn't be made again (recreate_flip): dropped */
            EnterCriticalSection(&g_lock);
            g_ps = f;
            rc_unhold(f, 0, f->rc_n);   /* step 84 */
            f->rc_n = f->up_n = 0;
            f->ra_cur = NULL;
            f->ra_bytes = 0;
            slot_free(f);
            LeaveCriticalSection(&g_lock);
            rt_let_go(f);
            continue;
        }
        if (g_subdump_trigger[0] && GetFileAttributesA(g_subdump_trigger) != INVALID_FILE_ATTRIBUTES)
            trigger_file(f->swap_no);
        sq_apply_slot(f);
        g_ps = f;
        g_ic = f->it;
        ovl_r = replay_frame(f);
        rt_let_go(f);
        replay_tail(ovl_r);
    }
    return 0;
}

/* The setting (and the CPU, for Auto) wants the render thread for frames
 * recorded as now: Frame rate: Display (the sync check: any recorded frame). */
static int rt_should(void)
{
    LONG m = g_rt_mode;

    if (m == TWOC_RT_OFF || (m == TWOC_RT_AUTO && g_rt_cores < RT_AUTO_CORES))
        return 0;
    return g_rt_sync ? REC_ON() && g_replay <= 1 : g_interp;
}

/* The render thread and its watchdog, made the first time. Returns 1 if
 * they run. */
static int rt_start(void)
{
    static int tried;
    typedef HRESULT(WINAPI * desc_t)(HANDLE, PCWSTR);
    desc_t desc;

    if (g_rt_th || tried)
        return g_rt_th != NULL;
    tried = 1;
    InitializeCriticalSection(&g_ho);
    InitializeConditionVariable(&g_ho_rt);
    InitializeConditionVariable(&g_ho_game);
    g_rt_th = CreateThread(NULL, 0, rt_main, NULL, 0, NULL);
    if (!g_rt_th) {
        fprintf(stderr, "[D3DB] host render thread not created (%lu): the game thread replays, as before\n",
                GetLastError());
        return 0;
    }
    desc = (desc_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetThreadDescription");
    if (desc)
        desc(g_rt_th, L"twoc render thread");
    if (g_wd_secs > 0) {
        g_wd_th = CreateThread(NULL, 0, rt_watchdog, NULL, 0, NULL);
        if (g_wd_th && desc)
            desc(g_wd_th, L"twoc render thread watchdog");
    }
    return 1;
}

/* At the end of a Swap on one thread, nothing recorded: the frames go to the
 * render thread from the next Swap on, if they should. */
static void rt_engage(unsigned swap_no)
{
    fslot_t *o;

    if (!rt_should())
        return;
    rec_lock(0);
    if (!g_thr && !g_rs->rc_n && !g_ps && rt_should() && rt_start()) {
        o = &g_slot[(g_rs - g_slot + 1) % FSLOTS];
        g_rs->state = SL_REC;
        o->state = SL_FREE;
        g_rprev = NULL;
        g_thr = 1;
        InterlockedExchange(&g_thr_v, 1);
        g_stg.rt_on = 1;
        fprintf(stderr, "[D3DB] host render thread: on from guest swap %u (%s%s; %u cores, %u logical)\n", swap_no + 1,
                g_rt_mode == TWOC_RT_AUTO ? "Auto" : "On", g_rt_sync ? ", sync: the game waits for each frame" : "",
                g_rt_cores, g_rt_logical);
    }
    rec_unlock();
}

/* At the game's Swap with the render thread on, after the hand-over: the
 * thread drained, and the frames back on the game thread, which takes the
 * frame-rate setting there (with g_rec held). */
static void rt_leave(unsigned swap_no, const char *why)
{
    EnterCriticalSection(&g_ho);
    while (g_pend || g_rt_busy) {
        wd_set(0, WD_G_DRAIN, swap_no);
        SleepConditionVariableCS(&g_ho_game, &g_ho, INFINITE);
    }
    LeaveCriticalSection(&g_ho);
    wd_set(0, WD_G_RUN, 0);
    g_thr = 0;
    InterlockedExchange(&g_thr_v, 0);
    g_stg.rt_on = 0;
    if (g_rprev) {                      /* the render thread left them for rc_maybe_target */
        g_rprev->targets_n = 0;
        g_rprev->overflow = 0;
        g_rprev = NULL;
    }
    sq_drain(1);                        /* the host side's part of what the game side just applied (the frames */
    EnterCriticalSection(&g_lock);      /* carried it to the render thread), as one thread does between frames */
    side_set(SIDE_GAME);
    if (ic_want() != g_interp && !g_rs->rc_n)
        ic_apply();
    side_set(SIDE_REPLAY);
    if (!g_interp)
        ic_queue(0);
    side_set(SIDE_GAME);
    LeaveCriticalSection(&g_lock);
    fprintf(stderr, "[D3DB] host render thread: off from guest swap %u (%s)\n", swap_no + 1, why);
}

/* The game thread's Swap with the render thread on: the frame handed over,
 * the wait for its next slot (and the overlay, and in the sync check the
 * frame itself), its next table set, the game side's settings. */
static void present_thread(double t_s, unsigned swap_no)
{
    fslot_t *f, *next;
    LARGE_INTEGER w0, w1;
    int side, s;

    EnterCriticalSection(&g_rec);
    side = side_set(SIDE_GAME);
    f = g_rs;
    next = &g_slot[(f - g_slot + 1) % FSLOTS];
    slot_fill(f, t_s, swap_no);
    QueryPerformanceCounter(&w0);
    EnterCriticalSection(&g_ho);
    while (g_pend) {                    /* depth 1: the frame handed over before is taken first (its slot is free */
        wd_set(0, WD_G_SLOT, swap_no);  /* once its last pass is drawn, before its last present and the take) */
        SleepConditionVariableCS(&g_ho_game, &g_ho, INFINITE);
    }
    f->state = SL_PEND;
    g_pend = f;
    WakeConditionVariable(&g_ho_rt);
    for (;;) {
        int what = next->state != SL_FREE ? WD_G_SLOT : f->hold && g_hold_done < f->seq ? WD_G_HOLD
                 : g_rt_sync && (g_pend || g_rt_busy) ? WD_G_SYNC : WD_G_RUN;
        if (what == WD_G_RUN)
            break;
        wd_set(0, what, swap_no);
        SleepConditionVariableCS(&g_ho_game, &g_ho, INFINITE);
    }
    for (s = 0; s < ITABS; s++)         /* the set neither the frame handed over nor the render thread's frame */
        if (s != f->it && s != g_ip)    /* before holds (g_ip, set under g_ho) */
            break;
    g_ir = s;
    next->state = SL_REC;
    g_rprev = f;
    LeaveCriticalSection(&g_ho);
    wd_set(0, WD_G_RUN, 0);
    QueryPerformanceCounter(&w1);
    g_stg.rt_wait_ms += (double)(w1.QuadPart - w0.QuadPart) * 1000.0 / (double)g_qpf.QuadPart;
    AcquireSRWLockExclusive(&g_pool_lock);  /* the command array the last replay gave back */
    {
        rcmd_t *rc = next->rc;
        unsigned cap = next->rc_cap;
        next->rc = g_rc_pool;
        next->rc_cap = g_rc_pool_cap;
        g_rc_pool = rc;
        g_rc_pool_cap = cap;
    }
    ReleaseSRWLockExclusive(&g_pool_lock);
    next->targets_n = 0;
    next->overflow = 0;
    g_rs = next;
    rk_forget();                        /* step 84 */
    ic_clear(g_ir);
    sq_drain(0);                        /* the game side's settings (those changed while the overlay held it) */
    if (!rt_should() || ic_want() != g_interp)
        rt_leave(swap_no, ic_want() != g_interp ? "the frame rate setting" : "the render thread setting");
    side_set(side);
    LeaveCriticalSection(&g_rec);
}

void d3dh_set_pause_unfocused(int on)
{
    InterlockedExchange(&g_pause_bg, on != 0);
    fprintf(stderr, "[OVL] pause when in the background: %s\n", on ? "on" : "off");
}

/* Step 90: the game held at the start of its Swap while the window is in the
 * background (bg_eval), its audio session muted; the frame before stays on
 * the screen. Game thread. */
static void bg_hold(unsigned swap_no)
{
    LARGE_INTEGER a, b;

    QueryPerformanceCounter(&a);
    fprintf(stderr, "[D3DB] host: the game holds at guest swap %u (its window in the background)\n", swap_no);
    twoc_audio_mute(1);
    while (g_bg_hold && g_ready)
        Sleep(20);
    twoc_audio_mute(0);
    QueryPerformanceCounter(&b);
    fprintf(stderr, "[D3DB] host: the game goes on after %.1f s held\n",
            (double)(b.QuadPart - a.QuadPart) / (double)g_qpf.QuadPart);
    QueryPerformanceCounter(&g_game_t0);    /* the hold isn't the game side's time on the frame */
}

void d3dh_present(double t_s, unsigned swap_no)
{
    int ovl_r, side;

    if (!g_ready)
        return;
    if (g_bg_hold)                      /* step 90: pause when in the background */
        bg_hold(swap_no);
    if (g_burn_ms > 0) {                /* step 82: the test aid, inside the game side's time (g_game_t0 to slot_fill) */
        static uint32_t rnd = 1;
        LARGE_INTEGER b0, b;
        double ms = g_burn_ms;
        if (g_burn_j > 0) {
            rnd = rnd * 1664525u + 1013904223u;
            ms += g_burn_j * ((double)(rnd >> 8) / 8388608.0 - 1.0);
        }
        QueryPerformanceCounter(&b0);
        do
            QueryPerformanceCounter(&b);
        while ((double)(b.QuadPart - b0.QuadPart) * 1000.0 / (double)g_qpf.QuadPart < ms);
    }
    if (g_thr) {                        /* step 79: the render thread replays it */
        present_thread(t_s, swap_no);
        QueryPerformanceCounter(&g_game_t0);
        return;
    }
    side = side_set(SIDE_REPLAY);       /* step 78: the trigger file's test aids are the replay side's */
    if (g_subdump_trigger[0] && GetFileAttributesA(g_subdump_trigger) != INVALID_FILE_ATTRIBUTES)
        trigger_file(swap_no);
    rec_lock(0);                        /* step 78: the frame recorded, to the replay side */
    side_set(SIDE_GAME);
    slot_handover(t_s, swap_no);
    side_set(SIDE_REPLAY);
    rec_unlock();
    ovl_r = replay_frame(g_ps);
    sq_drain(0);                        /* step 78: settings changed while the overlay held the game */
    if (g_interp_want != g_interp || (g_iq_on && !g_interp)) {     /* step 61: between frames, nothing recorded */
        rec_lock(0);
        side_set(SIDE_GAME);            /* step 78: the game side takes the setting (whether it records) */
        if (!g_rs->rc_n)
            ic_apply();
        side_set(SIDE_REPLAY);          /* ... the replay side's queue follows */
        if (!g_interp)
            ic_queue(0);
        rec_unlock();
    }
    replay_tail(ovl_r);
    sq_drain(0);                        /* step 78: then the game side's */
    side_set(side);
    rt_engage(swap_no);                 /* step 79 */
    QueryPerformanceCounter(&g_game_t0);
}

/* ── Pixel-shader probe (S4) ───────────────────────────────── */

#define PROBE_SIZE 4

int d3dh_ps_probe(const d3dh_ps_t *ps, uint32_t diffuse, uint32_t specular, const uint32_t tex_argb[4],
                  uint32_t *out_argb)
{
    int side = SIDE_ANY;
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
    rec_lock(1);
    side = side_set(SIDE_NOW);         /* step 78: a check, at once */
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
            side_set(side);
            rec_unlock();
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
    side_set(side);
    rec_unlock();
    return ok;
}

/* ── Vertex-program probe (S5) ─────────────────────────────── */

#define VS_PROBE_SENTINEL 0x01020304u

int d3dh_vs_probe(const d3dh_vprog_t *p, const void *vert, uint32_t stride, const d3dh_draw_t *view, int *px,
                  int *py, uint32_t *argb)
{
    int side = SIDE_ANY;
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
    rec_lock(1);
    side = side_set(SIDE_NOW);         /* step 78: a check, at once */
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
            side_set(side);
            rec_unlock();
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
    side_set(side);
    rec_unlock();
    return found;
}

/* Step 79: both sides' counters added up, read without a lock (the log's
 * figures; a render thread would otherwise hold the game up to a pass). */
void d3dh_stats(d3dh_stats_t *out)
{
    const d3dh_stats_t *g = &g_stg;

    if (!g_ready) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = g_st;
    out->draw_fails += g->draw_fails;
    out->tex_upload_fails += g->tex_upload_fails;
    out->tex_evictions += g->tex_evictions;
    out->tex_snaps += g->tex_snaps;
    out->tex_snap_bytes += g->tex_snap_bytes;
    out->rec_ms += g->rec_ms;
    out->rec_draws += g->rec_draws;
    out->overlay_passes += g->overlay_passes;
    out->overlay_draws += g->overlay_draws;
    out->overlay_world_cam += g->overlay_world_cam;
    out->hud_draws += g->hud_draws;
    out->hud_left += g->hud_left;
    out->hud_right += g->hud_right;
    out->hud_mid += g->hud_mid;
    out->hud_passes += g->hud_passes;
    out->rt_on = g->rt_on;              /* step 79 */
    out->rt_wait_ms += g->rt_wait_ms;
}
