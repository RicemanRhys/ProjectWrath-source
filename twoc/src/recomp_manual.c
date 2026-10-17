/**
 * Manual function overrides and ICALL diagnostics
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* xbox_bridge_resolve_handle: resolves a guest handle token back to the real
 * native HANDLE, for the hand-written GetExitCodeThread below. */
#include <xbox/xboxrecomp.h>

/* The generated-code headers: register aliases (eax, ecx, esp ...), MEM32/MEM8,
 * PUSH32, RECOMP_ABI_CALL and the declarations of every lifted function. The
 * hand-written sub_ functions below need them; they are only active with
 * RECOMP_GENERATED_CODE defined. */
#define RECOMP_GENERATED_CODE
#include "recomp/gen/recomp_funcs.h"

/* D3DB_CALL and the entry ids for the D3D mirror bridge wrappers (end of file). */
#include "d3d_bridge.h"
#include "log_block.h"
#include "controls.h"     /* step 57: the controller map and the keyboard */
#include "pads.h"         /* step 70: every other pad, through SDL */
#include "saves.h"        /* step 83: the save's backups */

/* ── ICALL trace ring buffer ───────────────────────────────── */

/*
 * The RECOMP_ICALL macro (recomp_types.h) writes g_icall_trace, g_icall_trace_idx
 * and g_icall_count on every indirect call; recomp_icall_fail_log() dumps the
 * last 16 targets. The runtime owns them (xbox_kernel, src/kernel/
 * xbox_memory_layout.c) and recomp_types.h declares them extern, so nothing is
 * defined or declared here: a second definition is a duplicate symbol (LNK2005).
 */

/* ── Test-only options ─────────────────────────────────────── */

/*
 * Two switches that get a run to the level load in about a minute with nobody
 * at the controller, instead of ~13 minutes with someone there. Each is off
 * unless its variable is set to something other than "" or "0"
 * (run_twoc.ps1 -FastTitle / -AutoStart). With both off, nothing below
 * writes guest memory or changes what the game reads.
 *
 * RECOMP_DEV_FAST_TITLE: the title animation (notes 18.24) steps
 * time += speed once per update and calls the main-menu callback
 * (sub_0002BCC0) once time >= 961. Its speed setter sub_0002C2C0 writes 0.5
 * every frame, then uses the float at 0x1BAA98 instead if that is nonzero.
 * Nothing in the binary writes 0x1BAA98 (BSS, 0 at boot; that setter is its
 * only reader), so it reads as a leftover tuning knob. Seeding it with
 * DEV_TITLE_SPEED finishes the ~660 remaining units in about a dozen updates
 * instead of ~1300. The pause while the title music isn't playing
 * (sub_0002C330 sets speed 0 and skips the setter) still applies. The knob is
 * cleared when the menu opens, so only the title runs fast.
 *
 * RECOMP_DEV_AUTOSTART: injects A on port 0 at +5 s and +20 s after the main
 * menu opens: menu 0 -> menu 4 -> "start game" (notes 18.25). If no real
 * pad is on port 0, a virtual one (neutral state) is reported there. The game
 * only opens a slot for a connected port, and the presses need a slot.
 *
 * RECOMP_DEV_INPUT_FILE=<path>: port 0 also takes input from a one-line text
 * file, reread at most every 50 ms, so a test can drive the pad from outside
 * the process while the game runs. Tokens are key=value: buttons=<hex
 * wButtons>, a b x y black white lt rt (0-255), lx ly rx ry (-32768..32767).
 * A missing or empty file adds nothing. Like autostart, it stands in a virtual
 * pad on port 0 if no real one is there. Buttons are ORed onto the real pad's
 * and autostart's; a nonzero stick value replaces the real one.
 */
#define DEV_TITLE_SPEED_VA  0x001BAA98u
#define DEV_TITLE_SPEED     64.0f

static int g_dev_fast_title;
static int g_dev_autostart;
static const char *g_dev_input_file;
static const char *g_dev_xi_file;   /* step 82: RECOMP_DEV_XINPUT_FILE, XInput user 0's stand-in (test only) */
static ULONGLONG g_dev_start_ms;
static ULONGLONG g_dev_menu_ms;     /* when the main menu opened; 0 = not yet */

static int dev_flag(const char *name)
{
    const char *v = getenv(name);
    return v && *v && strcmp(v, "0") != 0;
}

/* Called once from main.c, before the guest runs. */
void twoc_dev_options_init(void)
{
    g_dev_fast_title = dev_flag("RECOMP_DEV_FAST_TITLE");
    g_dev_autostart  = dev_flag("RECOMP_DEV_AUTOSTART");
    g_dev_start_ms   = GetTickCount64();

    if (g_dev_fast_title) {
        MEMF(DEV_TITLE_SPEED_VA) = DEV_TITLE_SPEED;
        fprintf(stderr, "[DEV] fast title: title speed knob 0x%08X = %.1f (normal 0.5)\n",
                DEV_TITLE_SPEED_VA, DEV_TITLE_SPEED);
    }
    if (g_dev_autostart)
        fprintf(stderr, "[DEV] autostart: A on port 0 at +5 s and +20 s after the main menu opens\n");
    g_dev_input_file = getenv("RECOMP_DEV_INPUT_FILE");
    if (g_dev_input_file && !*g_dev_input_file)
        g_dev_input_file = NULL;
    if (g_dev_input_file)
        fprintf(stderr, "[DEV] input file: port 0 also reads %s\n", g_dev_input_file);
    g_dev_xi_file = getenv("RECOMP_DEV_XINPUT_FILE");
    if (g_dev_xi_file && !*g_dev_xi_file)
        g_dev_xi_file = NULL;
    if (g_dev_xi_file)
        fprintf(stderr, "[DEV] XInput stand-in: user 0 reads %s in place of XInput\n", g_dev_xi_file);
}

/* Step 57: port 0 is the keyboard's too, in the bridge's window, with no
 * controller needed. */
static int kb_port(DWORD port)
{
    return port == 0 && d3db_host_on && controls_keyboard_on();
}

/* A port the game may open: really connected, or port 0 under autostart, the
 * input file or the keyboard; step 70: or port 0 with any other pad (pads.h:
 * SDL's, XInput users 1-3). */
static int dev_port_connected(DWORD port)
{
    return xbox_InputIsConnected(port)
        || ((g_dev_autostart || g_dev_input_file || g_dev_xi_file) && port == 0)
        || kb_port(port)
        || (port == 0 && pads_others());
}

/* A pad file's reader: its pad as last read (step 82: one per file and
 * reader, the poll's and the overlay's apart). */
typedef struct {
    XBOX_GAMEPAD cur;
    ULONGLONG last_ms;
    char last_line[256];
    unsigned changes;
} dev_pad_file_t;

/* A pad file (the input file; step 82: the XInput stand-in too), reread at
 * most every 50 ms. Returns 1 if it changed since the last call, so the
 * caller can advance the packet number; `what` names it in the log (NULL:
 * not logged). */
static int dev_pad_file_read(const char *path, dev_pad_file_t *d, const char *what, XBOX_GAMEPAD *pad)
{
    static const char *const analog[8] = { "a", "b", "x", "y", "black", "white", "lt", "rt" };
    XBOX_GAMEPAD cur = d->cur;
    char *last_line = d->last_line;
    ULONGLONG now = GetTickCount64();
    char line[256] = "";
    int changed = 0;

    if (now - d->last_ms >= 50) {
        FILE *f = fopen(path, "r");
        d->last_ms = now;
        if (f) {
            if (!fgets(line, sizeof(line), f))
                line[0] = '\0';
            fclose(f);
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, last_line) != 0) {
            char buf[256], *tok, *ctx = NULL;
            strcpy(last_line, line);
            strcpy(buf, line);
            memset(&cur, 0, sizeof(cur));
            for (tok = strtok_s(buf, " \t", &ctx); tok; tok = strtok_s(NULL, " \t", &ctx)) {
                char *eq = strchr(tok, '=');
                long v;
                int i;
                if (!eq)
                    continue;
                *eq = '\0';
                v = strtol(eq + 1, NULL, 0);
                if (!strcmp(tok, "buttons")) cur.wButtons = (WORD)v;
                else if (!strcmp(tok, "lx")) cur.sThumbLX = (SHORT)v;
                else if (!strcmp(tok, "ly")) cur.sThumbLY = (SHORT)v;
                else if (!strcmp(tok, "rx")) cur.sThumbRX = (SHORT)v;
                else if (!strcmp(tok, "ry")) cur.sThumbRY = (SHORT)v;
                else
                    for (i = 0; i < 8; i++)
                        if (!strcmp(tok, analog[i]))
                            cur.bAnalogButtons[i] = (BYTE)v;
            }
            if (what)
                fprintf(stderr, "[DEV] %s (+%.1f s): \"%s\"\n", what, (now - g_dev_start_ms) / 1000.0, line);
            changed = 1;
            d->changes++;
        }
    }
    d->cur = cur;
    *pad = cur;
    return changed;
}

static int dev_input_file_read(XBOX_GAMEPAD *pad)
{
    static dev_pad_file_t d;
    return dev_pad_file_read(g_dev_input_file, &d, "input file", pad);
}

/* Step 82: XInput user 0's state, from the stand-in file (as the runtime's
 * xbox_InputGetState gives an Xbox pad: buttons=<digital>, a b x y black
 * white lt rt 0-255, sticks) when RECOMP_DEV_XINPUT_FILE is set; `d` the
 * reader's own. Unset: xbox_InputGetState. */
static DWORD xi_get(DWORD port, XBOX_INPUT_STATE *s, dev_pad_file_t *d, const char *what)
{
    if (port == 0 && g_dev_xi_file) {
        memset(s, 0, sizeof(*s));
        dev_pad_file_read(g_dev_xi_file, d, what, &s->Gamepad);
        s->dwPacketNumber = d->changes;
        return ERROR_SUCCESS;
    }
    return xbox_InputGetState(port, s);
}
static dev_pad_file_t g_xi_poll;        /* the poll's reader of the stand-in */

/* Autostart's part of the poll: stand in a neutral virtual pad if port 0 has
 * no real one, then hold A for 500 ms at +5 s and +20 s after the menu. */
static void dev_autostart_poll(DWORD port, XBOX_INPUT_STATE *state, DWORD *result)
{
    static DWORD packet;
    static int a_down;
    int want_a = 0;

    if (!(g_dev_autostart || g_dev_input_file) || port != 0)
        return;
    if (*result != ERROR_SUCCESS) {
        memset(state, 0, sizeof(*state));
        *result = ERROR_SUCCESS;
    }
    if (g_dev_input_file) {
        XBOX_GAMEPAD f;
        int i;
        if (dev_input_file_read(&f))
            packet++;
        state->Gamepad.wButtons |= f.wButtons;
        for (i = 0; i < 8; i++)
            if (f.bAnalogButtons[i] > state->Gamepad.bAnalogButtons[i])
                state->Gamepad.bAnalogButtons[i] = f.bAnalogButtons[i];
        if (f.sThumbLX) state->Gamepad.sThumbLX = f.sThumbLX;
        if (f.sThumbLY) state->Gamepad.sThumbLY = f.sThumbLY;
        if (f.sThumbRX) state->Gamepad.sThumbRX = f.sThumbRX;
        if (f.sThumbRY) state->Gamepad.sThumbRY = f.sThumbRY;
    }
    if (g_dev_autostart && g_dev_menu_ms) {
        ULONGLONG dt = GetTickCount64() - g_dev_menu_ms;
        want_a = (dt >= 5000 && dt < 5500) || (dt >= 20000 && dt < 20500);
    }
    if (want_a != a_down) {
        a_down = want_a;
        packet++;
        fprintf(stderr, "[DEV] autostart: A %s (+%.1f s after the menu)\n",
                a_down ? "down" : "up", (GetTickCount64() - g_dev_menu_ms) / 1000.0);
    }
    if (a_down)
        state->Gamepad.bAnalogButtons[XBOX_BUTTON_A] = 0xFF;
    state->dwPacketNumber += packet;
}

/* ── Manual function overrides ─────────────────────────────── */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
extern void sub_001542AB(void);
extern void sub_0015431A(void);

/* The title animation's end callback (notes 18.24): sub_0002C020 stores it at
 * anim+0x90 and the animation engine calls it indirectly at 0x7AA35, so this
 * lookup sees it. It is set_menu(0), the main menu. The wrapper announces
 * that moment so a person at the controller knows when to press, and under
 * the test-only options records it and ends the fast title; the lifted
 * function runs unchanged. */
extern void sub_0002BCC0(void);
static void traced_sub_0002BCC0(void)
{
    static const char banner[] =
        "\n==================================================================\n"
        "  [GAME] MAIN MENU OPEN -- press Start, then A\n"
        "==================================================================\n";

    fputs(banner, stderr);
    fputs(banner, stdout);
    if (g_dev_fast_title || g_dev_autostart) {
        g_dev_menu_ms = GetTickCount64();
        fprintf(stderr, "[DEV] main menu opened %.1f s after start\n",
                (g_dev_menu_ms - g_dev_start_ms) / 1000.0);
        if (g_dev_fast_title)
            MEMF(DEV_TITLE_SPEED_VA) = 0.0f;   /* title done: back to normal speed */
    }
    fflush(stderr);
    fflush(stdout);
    d3db_mark("main menu opens (sub_0002BCC0)");
    sub_0002BCC0();
}

/* ── 16:9: the camera aspect (notes 18.54, 18.56) ──────────── */

/* sub_000956B0(camera) (cdecl) builds the frame's projection: it copies the
 * camera's 0x60-byte block to 0x242620, then reads fov and aspect (height
 * over width, 0.75) at +0x40/+0x44 for the projection's x scale, cot(fov/2) *
 * aspect, and the frustum tangent the culling uses, tan(fov/2) / aspect.
 * Nothing else reads the aspect, so with RECOMP_WIDESCREEN (bridge on) the
 * camera's aspect is scaled for the call (0.75 -> 0.5625 at 16:9) and put
 * back after it: the view is wider and so is the culling, for every camera
 * that renders to the screen. Off, the lifted function runs as before.
 *
 * Step 69 (notes 18.72): the draw distance the same way. The far plane is
 * the camera's +0x4C, the level's draw distance (the level table's word at
 * +0x2A, set at the level's start by sub_00087410; 1000 when under 10), and
 * the culling compares against the copy at 0x24266C. With a draw distance
 * multiplier (d3db_draw_distance) the world camera's (*0x942080) far plane
 * is scaled for the call; the fog and the sky with it, below. */
#define WORLD_CAMERA_PTR 0x00942080u
extern void sub_000956B0_gen(void);
void sub_000956B0(void)
{
    uint32_t cam = MEM32(esp + 4), saved, saved_far;
    float scale, k = (float)d3db_draw_distance;
    int far_on = k > 1.0f && cam && cam == MEM32(WORLD_CAMERA_PTR);

    if ((d3db_wide <= 0 && !far_on) || !cam) {
        sub_000956B0_gen();
        return;
    }
    saved = MEM32(cam + 0x44);
    saved_far = MEM32(cam + 0x4C);
    scale = d3db_camera_aspect_scale(cam, MEMF(cam + 0x44));
    if (scale != 1.0f)
        MEMF(cam + 0x44) *= scale;
    if (far_on)
        MEMF(cam + 0x4C) *= k;
    sub_000956B0_gen();
    MEM32(cam + 0x44) = saved;
    MEM32(cam + 0x4C) = saved_far;
}

/* sub_0009D3F0(start, end, colour, ., .) sets the fog: start and end
 * (D3DRS_FOGSTART/FOGEND, linear table fog) and its colour, or turns it off
 * when start == end. The world's fog is set once a frame from the level
 * table (+0x44, +0x48; some levels move them) at 0x88B30, before the world
 * camera's projection; the reflection passes and the fog reset call it
 * elsewhere. With a draw distance multiplier that call's start and end are
 * scaled with the far plane, so the fog keeps its shape over the longer
 * view. */
#define FOG_WORLD_RET 0x00088B35u
extern void sub_0009D3F0_gen(void);
void sub_0009D3F0(void)
{
    uint32_t args = esp + 4, start, end;

    if (d3db_draw_distance <= 1.0 || MEM32(esp) != FOG_WORLD_RET) {
        sub_0009D3F0_gen();
        return;
    }
    start = MEM32(args);
    end = MEM32(args + 4);
    MEMF(args) *= (float)d3db_draw_distance;
    MEMF(args + 4) *= (float)d3db_draw_distance;
    sub_0009D3F0_gen();
    MEM32(args) = start;                /* the caller's pushed copies, as they were */
    MEM32(args + 4) = end;
}

/* sub_000674E0 queues the level's sky (the first flush of the frame, at
 * 0x88BBF): its models scaled by the world camera's far plane x 0.05 and
 * centred on the eye. Under a scaled fog the sky would come out of it and
 * hide what lies beyond it, so with the multiplier it is scaled with the far
 * plane: as far off, and as fogged, as before. */
extern void sub_000674E0_gen(void);
void sub_000674E0(void)
{
    uint32_t cam = MEM32(WORLD_CAMERA_PTR), saved;

    if (d3db_draw_distance <= 1.0 || !cam) {
        sub_000674E0_gen();
        return;
    }
    saved = MEM32(cam + 0x4C);
    MEMF(cam + 0x4C) *= (float)d3db_draw_distance;
    sub_000674E0_gen();
    MEM32(cam + 0x4C) = saved;
}

/* ── Step 65: the game's render passes (notes 18.68) ───────────
 *
 * sub_000A8140 draws the display list the game has queued so far (models,
 * text, HUD items through sub_000A0990). The main loop flushes it several
 * times a frame: at 0x8929A for the world, at 0x89679 for the overlay (the
 * HUD counters, title cards, the pause menu and its pages). Drawn under the
 * world's camera, the overlay can't be told from the world by its draws, so
 * the bridge is told which flush is running. Bridge off: the lifted body
 * alone. */
extern void sub_000A8140_gen(void);
void sub_000A8140(void)
{
    uint32_t ret = MEM32(esp);

    if (!d3db_host_on) {
        sub_000A8140_gen();
        return;
    }
    d3db_list_flush(ret, 0);
    sub_000A8140_gen();
    d3db_list_flush(ret, 1);
}

/* sub_000B0EF0 is the game's whole panel: the pause menu and its pages and
 * the hub pads' panels first, then (from 0xB2568) the HUD counters and the
 * title cards of a level being played. The bridge notes its stack frame, so
 * an item it queues can be told by the call site (d3db_item_queued). */
extern void sub_000B0EF0_gen(void);
void sub_000B0EF0(void)
{
    if (!d3db_host_on) {
        sub_000B0EF0_gen();
        return;
    }
    d3db_panel(esp);
    sub_000B0EF0_gen();
    d3db_panel(0);
}

/* sub_0009EF10(material, item) links a queued item into the display list;
 * sub_000A8240(item) draws one when the list is flushed. */
extern void sub_0009EF10_gen(void);
void sub_0009EF10(void)
{
    if (d3db_host_on)
        d3db_item_queued(MEM32(esp + 8));
    sub_0009EF10_gen();
}

extern void sub_000A8240_gen(void);
void sub_000A8240(void)
{
    if (!d3db_host_on) {
        sub_000A8240_gen();
        return;
    }
    d3db_item_drawn(MEM32(esp + 4));
    sub_000A8240_gen();
    d3db_item_drawn(0);
}

/* ── Cooperative NtSuspendThread / NtResumeThread ──────────── */

/*
 * The level setup (sub_0002C020, notes 18.26) suspends the stream thread at
 * 0x2C032 and resumes it at 0x2C245. The runtime's NtSuspendThread is a real
 * SuspendThread, so the thread stops wherever it happens to be. That can be
 * inside host code holding a host lock. In step 24 it was the runtime's
 * `[CS]` contention fprintf holding the CRT stderr lock. Every thread that
 * logs then blocks, the watchdog too. The heap lock and the runtime's own
 * locks are exposed the same way.
 *
 * Here a suspend only marks the target. The target parks itself at its next
 * kernel-call boundary, where it holds no host lock. Every guest kernel call
 * is an indirect call through a thunk VA, and RECOMP_ICALL asks
 * recomp_lookup_manual first. So kernel VAs are routed through
 * coop_kernel_call, which checks for a pending suspend on entry and again on
 * return. Only the outermost call parks. Kernel code that calls back into
 * guest code (APCs, DPCs) may hold host locks, so a nested call never parks.
 *
 * Suspend returns once the target is parked or inside a kernel call. In both
 * cases it runs no more guest code until it is resumed, as with a real
 * suspend. A thread spinning in guest code with no kernel calls gets 100 ms
 * and is then left to park at its next kernel call. That is logged.
 *
 * Handles that don't resolve to a spawned thread go to the real bridge (the
 * main thread never has a spawned HANDLE). So does a resume of a thread with
 * no cooperative suspend count, such as one created suspended.
 *
 * A thread that owns a guest critical section doesn't park either (notes
 * 18.32, 18.33): it parks at the first boundary where it owns none. Level
 * setup suspended the stream thread while it held DSOUND's lock 0x152BB0,
 * then entered DSOUND itself and waited on that lock forever. On the Xbox a
 * suspend is an APC, which the holder of DSOUND's lock can't take at that
 * point either. The game takes critical sections only through the two thunks
 * below (no TryEnter, no AndRegion import), so a per-thread count of enters
 * minus leaves, kept at this boundary, is exact. DSOUND's IRQL lock (18.45)
 * counts as one more: a thread at DISPATCH_LEVEL can't take a suspend either.
 */
#define COOP_NTSUSPEND_THUNK  0x0015AA7Cu      /* [thunk] = NtSuspendThread (ordinal 231) */
#define COOP_NTRESUME_THUNK   0x0015AA80u      /* [thunk] = NtResumeThread (ordinal 224) */
#define COOP_CS_ENTER_THUNK   0x0015AA64u      /* [thunk] = RtlEnterCriticalSection (ordinal 277) */
#define COOP_CS_LEAVE_THUNK   0x0015AA60u      /* [thunk] = RtlLeaveCriticalSection (ordinal 294) */
#define COOP_KERNEL_VA_LO     0xFE000000u      /* the bridge's synthetic thunk VAs */
#define COOP_KERNEL_VA_HI     0xFF000000u      /* (kernel_bridge.c KERNEL_VA_BASE) */
#define COOP_MAX_THREADS      64
#define COOP_SETTLE_MS        100
#define COOP_LOG_MAX          64

enum { COOP_GUEST, COOP_KERNEL, COOP_PARKED };

typedef struct {
    DWORD         tid;                 /* 0 = free slot */
    volatile LONG suspend;             /* cooperative suspend count */
    volatile LONG state;               /* COOP_GUEST / COOP_KERNEL / COOP_PARKED */
    volatile LONG locks;               /* guest critical sections held (enters - leaves) */
    volatile LONG acquiring;           /* inside RtlEnterCriticalSection */
    LONG          deferred;            /* boundaries passed holding a lock, this suspend */
    HANDLE        wake;                /* manual-reset, signaled while suspend == 0 */
} coop_thread_t;

static coop_thread_t g_coop[COOP_MAX_THREADS];
static SRWLOCK g_coop_lock = SRWLOCK_INIT;
static volatile LONG g_coop_created;    /* records made, for the spawn wait (S7) */
static volatile LONG g_coop_logged;
static RECOMP_TLS coop_thread_t *t_coop;
static RECOMP_TLS uint32_t t_coop_kernel_va;   /* set by the lookup, read by the call */
static RECOMP_TLS int t_coop_depth;            /* nesting of kernel calls */
#define COOP_CS_TRACK 32
static RECOMP_TLS uint32_t t_cs_held[COOP_CS_TRACK]; /* guest CS addresses owned, innermost last */
static RECOMP_TLS int t_cs_n;                  /* how many (can exceed COOP_CS_TRACK) */
static RECOMP_TLS int t_ds_irql;               /* DSOUND's IRQL lock nesting, counted as a lock (18.45) */
static uint32_t g_cs_watch;                     /* coop_watch_cs */
static volatile LONG g_cs_watch_holders;       /* threads owning it now */

int coop_holds_cs(uint32_t cs_va);

static const char *const coop_state_name[] = { "in guest code", "in a kernel call", "parked" };

/* The record for a thread id, created if asked. NULL if the table is full.
 * A worker's record is given back when it ends (coop_forget, S7): every level
 * load starts a loading-screen thread, so an attract loop would otherwise fill
 * the table after ~30 menu -> demo cycles. */
static coop_thread_t *coop_find(DWORD tid, int create)
{
    coop_thread_t *t = NULL, *empty = NULL;
    int i;

    AcquireSRWLockExclusive(&g_coop_lock);
    for (i = 0; i < COOP_MAX_THREADS; i++) {
        if (g_coop[i].tid == tid) {
            t = &g_coop[i];
            break;
        }
        if (!g_coop[i].tid && !empty)
            empty = &g_coop[i];
    }
    if (!t && create && empty) {
        t = empty;
        t->suspend = 0;
        t->state = COOP_GUEST;
        t->locks = 0;
        t->acquiring = 0;
        t->deferred = 0;
        if (t->wake)
            SetEvent(t->wake);          /* a reused record keeps its event */
        else
            t->wake = CreateEventA(NULL, TRUE, TRUE, NULL);
        t->tid = tid;
        InterlockedIncrement(&g_coop_created);
    }
    ReleaseSRWLockExclusive(&g_coop_lock);
    return t;
}

/* A worker is ending: its record can serve the next thread. */
static void coop_forget(coop_thread_t *t)
{
    if (!t)
        return;
    AcquireSRWLockExclusive(&g_coop_lock);
    t->tid = 0;
    ReleaseSRWLockExclusive(&g_coop_lock);
}

static coop_thread_t *coop_self(void)
{
    if (!t_coop)
        t_coop = coop_find(GetCurrentThreadId(), 1);
    return t_coop;
}

static void coop_log(const char *what, DWORD tid, LONG prev, uint32_t ret, const char *state);

/* Called only at depth 0, where this thread holds no host lock. It may still
 * own guest critical sections; then it runs on to the next boundary. */
static void coop_park(coop_thread_t *t)
{
    if (t->suspend <= 0)
        return;
    if (InterlockedCompareExchange(&t->locks, 0, 0) > 0) {
        t->deferred++;
        return;
    }
    if (t->deferred) {
        char why[64];
        sprintf(why, "after %ld boundaries holding a guest lock", (long)t->deferred);
        coop_log("park", t->tid, t->suspend, 0, why);
        t->deferred = 0;
    }
    InterlockedExchange(&t->state, COOP_PARKED);
    while (InterlockedCompareExchange(&t->suspend, 0, 0) > 0)
        WaitForSingleObject(t->wake, INFINITE);
    InterlockedExchange(&t->state, COOP_GUEST);
}

/* The kernel-call boundary. Hand-written kernel functions use these too. */
static coop_thread_t *coop_enter(void)
{
    coop_thread_t *self = coop_self();

    if (self && t_coop_depth == 0)
        coop_park(self);
    if (++t_coop_depth == 1 && self)
        InterlockedExchange(&self->state, COOP_KERNEL);
    return self;
}

static void coop_leave(coop_thread_t *self)
{
    if (--t_coop_depth == 0 && self) {
        /* Publish "guest" before checking, so a suspend that lands in
         * between either sees guest and waits, or is seen here. */
        InterlockedExchange(&self->state, COOP_GUEST);
        coop_park(self);
    }
}

static void heap_watch(void);
static int hg_kernel_call(uint32_t va, coop_thread_t *self);
static void hg_enter(void);
static void hg_leave(void);

/* Every guest kernel call, via recomp_lookup_manual. */
static void coop_kernel_call(void)
{
    uint32_t va = t_coop_kernel_va;
    coop_thread_t *self = coop_enter();
    int enter = va == MEM32(COOP_CS_ENTER_THUNK);
    int leave = !enter && va == MEM32(COOP_CS_LEAVE_THUNK);
    uint32_t cs = (enter || leave) ? MEM32(esp + 4) : 0;   /* stdcall (PRTL_CRITICAL_SECTION) */
    int watched = cs && cs == g_cs_watch;
    int had = watched && coop_holds_cs(cs);            /* before this call */

    if (enter && self)
        InterlockedExchange(&self->acquiring, 1);
    if (!hg_kernel_call(va, self))         /* the guest heap guard, below (S7) */
        recomp_lookup_kernel(va)();        /* the lookup also selects this thread's dispatch slot */
    if (enter) {
        if (t_cs_n < COOP_CS_TRACK)
            t_cs_held[t_cs_n] = cs;
        t_cs_n++;
        if (watched && !had)
            InterlockedIncrement(&g_cs_watch_holders);
    } else if (leave) {
        int i = t_cs_n < COOP_CS_TRACK ? t_cs_n : COOP_CS_TRACK;
        while (i-- > 0 && t_cs_held[i] != cs)
            ;
        if (i >= 0) {                      /* innermost matching entry */
            for (; i + 1 < t_cs_n && i + 1 < COOP_CS_TRACK; i++)
                t_cs_held[i] = t_cs_held[i + 1];
            t_cs_n--;
        } else if (t_cs_n > COOP_CS_TRACK) {
            t_cs_n--;                      /* an untracked overflow entry */
        }                                  /* else: a leave with no enter; ignore */
        if (had && !coop_holds_cs(cs))
            InterlockedDecrement(&g_cs_watch_holders);
    }
    if (self && (enter || leave)) {
        InterlockedExchange(&self->locks, t_cs_n + t_ds_irql);
        InterlockedExchange(&self->acquiring, 0);
    }
    coop_leave(self);
    heap_watch();
}

/* Whether the calling thread owns guest critical section `cs_va` (d3d_bridge.c
 * checks the game's D3D lock with it). */
int coop_holds_cs(uint32_t cs_va)
{
    int i, n = t_cs_n < COOP_CS_TRACK ? t_cs_n : COOP_CS_TRACK;
    for (i = 0; i < n; i++)
        if (t_cs_held[i] == cs_va)
            return 1;
    return 0;
}

/* One critical section whose owners are counted across threads, so a caller
 * without it can tell whether another thread holds it right now. */
void coop_watch_cs(uint32_t cs_va)
{
    g_cs_watch = cs_va;
}

int coop_cs_holders(void)
{
    return (int)g_cs_watch_holders;
}

static void coop_log(const char *what, DWORD tid, LONG prev, uint32_t ret, const char *state)
{
    if (InterlockedIncrement(&g_coop_logged) > COOP_LOG_MAX)
        return;
    if (what[0] == 'p') {                  /* "park": no count change */
        fprintf(stderr, "[SUSPEND] park thread %lu: count %ld, %s\n",
                (unsigned long)tid, (long)prev, state);
        fflush(stderr);
        return;
    }
    fprintf(stderr, "[SUSPEND] %s thread %lu from 0x%08X: count %ld -> %ld%s%s\n",
            what, (unsigned long)tid, ret, (long)prev,
            (long)(what[0] == 's' ? prev + 1 : prev - 1),
            state ? ", target " : "", state ? state : "");
    fflush(stderr);
}

/* Resolve a guest thread handle to its record. NULL: not a spawned thread. */
static coop_thread_t *coop_target(uint32_t handle, DWORD *tid)
{
    HANDLE h = xbox_bridge_resolve_handle(handle);

    *tid = h ? GetThreadId(h) : 0;
    return *tid ? coop_find(*tid, 1) : NULL;
}

/* NTSTATUS NtSuspendThread(HANDLE ThreadHandle, PULONG PreviousSuspendCount); ret 8 */
static void coop_NtSuspendThread(void)
{
    uint32_t ret     = MEM32(esp);
    uint32_t handle  = MEM32(esp + 4);
    uint32_t prev_va = MEM32(esp + 8);
    DWORD tid;
    coop_thread_t *t = coop_target(handle, &tid);
    coop_thread_t *self = coop_self();
    LONG prev;

    if (!t) {
        t_coop_kernel_va = MEM32(COOP_NTSUSPEND_THUNK);
        coop_kernel_call();
        return;
    }
    if (self && t_coop_depth == 0)
        coop_park(self);                   /* a kernel-call boundary for the caller too */

    AcquireSRWLockExclusive(&g_coop_lock);
    prev = t->suspend;
    InterlockedIncrement(&t->suspend);
    if (prev == 0)
        ResetEvent(t->wake);
    ReleaseSRWLockExclusive(&g_coop_lock);

    if (prev_va)
        MEM32(prev_va) = (uint32_t)prev;
    eax = 0;                               /* STATUS_SUCCESS */
    esp += 12;                             /* ret 8 */

    if (t == self) {
        coop_log("suspend (self)", tid, prev, ret, NULL);
        if (t_coop_depth == 0)
            coop_park(self);
        return;
    }
    {
        /* Settled: parked, or in a kernel call it will park on return from.
         * One that owns or is taking a guest lock runs on until it lets go. */
        ULONGLONG until = GetTickCount64() + COOP_SETTLE_MS;
        LONG st, locks, acq;
        char why[96];
        for (;;) {
            st    = InterlockedCompareExchange(&t->state, 0, 0);
            locks = InterlockedCompareExchange(&t->locks, 0, 0);
            acq   = InterlockedCompareExchange(&t->acquiring, 0, 0);
            if (st == COOP_PARKED || (st == COOP_KERNEL && !locks && !acq) ||
                t->suspend <= 0 || GetTickCount64() >= until)
                break;
            Sleep(1);
        }
        if (acq)
            sprintf(why, "taking a guest lock, parks once it holds none");
        else if (locks && st != COOP_PARKED)
            sprintf(why, "%s holding %ld guest lock(s), parks once it holds none",
                    coop_state_name[st], (long)locks);
        else
            sprintf(why, "%s", st == COOP_GUEST
                    ? "still in guest code, parks at its next kernel call"
                    : coop_state_name[st]);
        coop_log("suspend", tid, prev, ret, why);
    }
}

/* NTSTATUS NtResumeThread(HANDLE ThreadHandle, PULONG PreviousSuspendCount); ret 8 */
static void coop_NtResumeThread(void)
{
    uint32_t ret     = MEM32(esp);
    uint32_t handle  = MEM32(esp + 4);
    uint32_t prev_va = MEM32(esp + 8);
    DWORD tid;
    coop_thread_t *t = coop_target(handle, &tid);
    LONG prev = 0;

    if (t) {
        AcquireSRWLockExclusive(&g_coop_lock);
        prev = t->suspend;
        if (prev > 0 && InterlockedDecrement(&t->suspend) == 0)
            SetEvent(t->wake);
        ReleaseSRWLockExclusive(&g_coop_lock);
    }
    if (prev <= 0) {                       /* not ours: e.g. created suspended */
        t_coop_kernel_va = MEM32(COOP_NTRESUME_THUNK);
        coop_kernel_call();
        return;
    }
    if (prev_va)
        MEM32(prev_va) = (uint32_t)prev;
    eax = 0;                               /* STATUS_SUCCESS */
    esp += 12;                             /* ret 8 */
    coop_log("resume", tid, prev, ret, NULL);
}

/* ── NtFreeVirtualMemory / guest heap watch ────────────────── */

/*
 * The runtime's NtAllocateVirtualMemory hands out xbox_HeapAlloc blocks, but
 * its NtFreeVirtualMemory passes the guest PVOID* on as a 64-bit PVOID* and
 * calls host VirtualFree on two guest dwords read as one pointer. That always
 * fails, so every MEM_RELEASE leaked and every MEM_DECOMMIT failed. The intro
 * level load ran the 48.5 MiB heap out at a live peak of ~24.5 MiB (notes
 * 18.27). Only xbox_HeapFree can release those blocks.
 *
 * So, for [0x15AB0C] (ordinal 199):
 *   MEM_RELEASE at the start of a heap block: xbox_HeapFree, and report the
 *     block's size back through RegionSize, as NT does.
 *   MEM_DECOMMIT: success, nothing done. The heap commits a block for its
 *     whole life, and the guest heap's large-block path (sub_000F0D65)
 *     decommits pages it will commit again (a MEM_COMMIT on a live block is
 *     already a no-op in the bridge).
 *   Anything else (a reservation above RAM, an interior address, a null base):
 *     the bridge, as before.
 * It runs inside the cooperative kernel-call boundary, like any other kernel
 * call, so a suspended thread can't stop holding the heap.
 *
 * Since 18.42 new regions come from a zone of their own (below, "The VM
 * zone"), and a region from the zone is released back to it.
 */
#define VM_NTALLOC_THUNK      0x0015AB08u      /* [thunk] = NtAllocateVirtualMemory (ordinal 184) */
#define VM_NTFREE_THUNK       0x0015AB0Cu      /* [thunk] = NtFreeVirtualMemory (ordinal 199) */
#define VM_MEM_DECOMMIT       0x4000u
#define VM_MEM_RELEASE        0x8000u
#define VM_WASTE_SLOTS        1024
#define VM_WASTE_MIN          0x10000u         /* ignore reuse slack under 64 KiB */
#define HEAP_WATCH_MS         2000             /* the [KERNEL] summary's cadence */

static volatile LONG g_vm_released, g_vm_released_kb, g_vm_decommits, g_vm_bridged;

/*
 * The heap reuses a freed block first-fit without splitting it (notes 18.27),
 * so a small request can take a whole multi-MB block. Watched, not fixed:
 * each NtAllocateVirtualMemory that lands in a block at least 64 KiB bigger
 * than it asked for is recorded here until that block is released. Since
 * 18.42 only regions the zone can't hold reach the heap.
 */
static struct { uint32_t base, waste; } g_vm_waste[VM_WASTE_SLOTS];
static SRWLOCK g_vm_lock = SRWLOCK_INIT;
static uint32_t g_vm_waste_live, g_vm_waste_max, g_vm_waste_events;

static void vm_waste_forget(uint32_t base)
{
    int i;

    AcquireSRWLockExclusive(&g_vm_lock);
    for (i = 0; i < VM_WASTE_SLOTS; i++)
        if (g_vm_waste[i].base == base) {
            g_vm_waste_live -= g_vm_waste[i].waste;
            g_vm_waste[i].base = 0;
            break;
        }
    ReleaseSRWLockExclusive(&g_vm_lock);
}

/*
 * The VM zone (notes 18.42).
 *
 * Nearly all of the game's virtual memory is XAPI's RtlAllocateHeap passing
 * big blocks straight to NtAllocateVirtualMemory (MEM_COMMIT at 0xF0C51,
 * MEM_RELEASE at 0xF0EFA): the level pool (7 MB at boot, 10 MB from the menu
 * on) and each level file's buffer (1-4 MB), allocated and released at every
 * load. Given back to the heap, a released block went whole to the next
 * request it could hold (first fit, no split): at the hub a 7 MB request
 * held the old 10 MB pool and a 1 MB one a 2.6 MB block, and the slack piled
 * up. Cutting released blocks up here instead wasn't enough either: the
 * heap's blocks are exact byte sizes with page-alignment padding between
 * them, so two released neighbours never merged, and every level whose
 * buffer outgrew the biggest piece took 4 MB more of the bump space (four
 * loads ran it out).
 *
 * On a console none of this arises: a region is contiguous virtual memory
 * over whatever physical pages are free. Here a guest address is its
 * physical place, so the regions need an allocator that keeps free space in
 * one piece. The zone is one heap block taken at the first new region (at
 * boot, from bump space, so it costs its size and nothing more): every region
 * after that is cut from it, page-aligned and a whole number of pages, from
 * the smallest free range that holds it; a released region goes back and
 * merges with the free ranges on either side. The heap keeps VM_ZONE_SPARE
 * for everything else it serves (thread stacks and TIBs, the pool arena and
 * its big blocks, the runtime's own). A region the zone can't hold, and any
 * region asked for at a given address, goes to the bridge as before.
 *
 * Reported as the bridge does: the size asked back through RegionSize, and
 * zeroed (its whole pages; the bridge zeroed the bytes asked). A commit
 * inside a zone region is the bridge's no-op, as for any live block.
 * NtQueryVirtualMemory doesn't look at blocks. The ranges are kept on the
 * host side, not in guest memory.
 */
#define VR_MAX                2048
#define VR_PAGE               0x1000u
#define VM_ZONE_SPARE         (10u * 1024u * 1024u)

typedef struct { uint32_t base, size; } vr_range_t;

static vr_range_t g_vr_free[VR_MAX], g_vr_live[VR_MAX];   /* each sorted by base */
static int g_vr_free_n, g_vr_live_n;
static uint32_t g_vr_zone, g_vr_zone_size;                 /* 0 = not made yet */
static int g_vr_zone_failed;
static uint32_t g_vr_free_bytes, g_vr_live_bytes, g_vr_live_peak;
static volatile LONG g_vr_cut, g_vr_returned, g_vr_to_heap, g_vr_table_full;
static SRWLOCK g_vr_lock = SRWLOCK_INIT;

/* The first index whose range starts at or above va. */
static int vr_lower(const vr_range_t *r, int n, uint32_t va)
{
    int lo = 0, hi = n;

    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (r[mid].base < va)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static int vr_insert(vr_range_t *r, int *n, int i, uint32_t base, uint32_t size)
{
    if (*n == VR_MAX)
        return 0;
    memmove(&r[i + 1], &r[i], (*n - i) * sizeof(r[0]));
    r[i].base = base;
    r[i].size = size;
    (*n)++;
    return 1;
}

static void vr_remove(vr_range_t *r, int *n, int i)
{
    memmove(&r[i], &r[i + 1], (*n - i - 1) * sizeof(r[0]));
    (*n)--;
}

static int vr_in_zone(uint32_t va)
{
    return g_vr_zone && va >= g_vr_zone && va - g_vr_zone < g_vr_zone_size;
}

/* Takes the zone from the heap. Caller holds g_vr_lock. */
static void vr_make_zone(void)
{
    xbox_heap_stats_t s;
    uint32_t size;

    hg_enter();
    xbox_HeapStats(&s);
    size = s.total - s.used > VM_ZONE_SPARE ? (s.total - s.used - VM_ZONE_SPARE) & ~(VR_PAGE - 1) : 0;
    g_vr_zone = size ? xbox_HeapAlloc(size, VR_PAGE) : 0;
    hg_leave();
    if (g_vr_zone) {
        g_vr_zone_size = size;
        g_vr_free[0].base = g_vr_zone;
        g_vr_free[0].size = size;
        g_vr_free_n = 1;
        g_vr_free_bytes = size;
    } else {
        g_vr_zone_failed = 1;
    }
    fprintf(stderr, "[VM] zone %u KiB at %08X, %u KiB of the heap left for the rest%s\n",
            size / 1024, g_vr_zone, (s.total - s.used - size) / 1024,
            g_vr_zone ? "" : ": no room, regions stay on the heap");
}

/* [base, base + size) becomes free, merged with its neighbours. 0 if the
 * table is full. Caller holds g_vr_lock. */
static int vr_free_add(uint32_t base, uint32_t size)
{
    int i = vr_lower(g_vr_free, g_vr_free_n, base);
    int down = i > 0 && g_vr_free[i - 1].base + g_vr_free[i - 1].size == base;
    int up = i < g_vr_free_n && base + size == g_vr_free[i].base;

    if (down && up) {
        g_vr_free[i - 1].size += size + g_vr_free[i].size;
        vr_remove(g_vr_free, &g_vr_free_n, i);
    } else if (down) {
        g_vr_free[i - 1].size += size;
    } else if (up) {
        g_vr_free[i].base = base;
        g_vr_free[i].size += size;
    } else if (!vr_insert(g_vr_free, &g_vr_free_n, i, base, size)) {
        return 0;
    }
    g_vr_free_bytes += size;
    return 1;
}

/* `need` bytes (a page multiple) from the smallest free range that holds
 * them, at its start, or 0. Every range starts and ends on a page, so there
 * is never padding to strand. Caller holds g_vr_lock. */
static uint32_t vr_take(uint32_t need)
{
    uint32_t at;
    int i, best = -1;

    if (g_vr_live_n == VR_MAX)
        return 0;
    for (i = 0; i < g_vr_free_n; i++)
        if (g_vr_free[i].size >= need && (best < 0 || g_vr_free[i].size < g_vr_free[best].size))
            best = i;
    if (best < 0)
        return 0;
    at = g_vr_free[best].base;
    if (g_vr_free[best].size == need) {
        vr_remove(g_vr_free, &g_vr_free_n, best);
    } else {
        g_vr_free[best].base += need;
        g_vr_free[best].size -= need;
    }
    g_vr_free_bytes -= need;
    vr_insert(g_vr_live, &g_vr_live_n, vr_lower(g_vr_live, g_vr_live_n, at), at, need);
    g_vr_live_bytes += need;
    if (g_vr_live_bytes > g_vr_live_peak)
        g_vr_live_peak = g_vr_live_bytes;
    return at;
}

/* Releases a zone region; its size, or 0 if base isn't the start of a live
 * one. Caller holds g_vr_lock. */
static uint32_t vr_give(uint32_t base)
{
    int i = vr_lower(g_vr_live, g_vr_live_n, base);
    uint32_t size;

    if (i == g_vr_live_n || g_vr_live[i].base != base)
        return 0;
    size = g_vr_live[i].size;
    vr_remove(g_vr_live, &g_vr_live_n, i);
    g_vr_live_bytes -= size;
    if (!vr_free_add(base, size))
        InterlockedIncrement(&g_vr_table_full);    /* stays out of use; counted */
    return size;
}

/* NTSTATUS NtAllocateVirtualMemory(PVOID *BaseAddress, ULONG_PTR ZeroBits,
 *     PSIZE_T RegionSize, ULONG AllocationType, ULONG Protect); ret 20.
 * A new region comes from the zone when it fits (above); otherwise the bridge
 * does the work, and this measures the block it got. */
static void vm_NtAllocateVirtualMemory(void)
{
    uint32_t base_va = MEM32(esp + 4), size_va = MEM32(esp + 12), type = MEM32(esp + 16);
    uint32_t hint = base_va ? MEM32(base_va) : 0;
    uint32_t base, size, blk;

    size = size_va ? MEM32(size_va) : 0;
    if (base_va && size && !hint && (type & 0x3000) && size <= 0xFFFFFFFFu - VR_PAGE) {
        coop_thread_t *self = coop_enter();
        AcquireSRWLockExclusive(&g_vr_lock);
        if (!g_vr_zone && !g_vr_zone_failed)
            vr_make_zone();
        base = g_vr_zone ? vr_take((size + VR_PAGE - 1) & ~(VR_PAGE - 1)) : 0;
        ReleaseSRWLockExclusive(&g_vr_lock);
        if (base) {
            memset((void *)XBOX_PTR(base), 0, (size + VR_PAGE - 1) & ~(VR_PAGE - 1));
            MEM32(base_va) = base;
            MEM32(size_va) = size;                     /* the bridge reports the size asked */
            InterlockedIncrement(&g_vr_cut);
            eax = 0;                                   /* STATUS_SUCCESS */
            esp += 24;                                 /* ret 20 */
            coop_leave(self);
            return;
        }
        coop_leave(self);
        if (g_vr_zone && InterlockedIncrement(&g_vr_to_heap) <= 5)
            fprintf(stderr, "[VM] zone has no room for %u bytes: the heap serves it\n", size);
    }
    t_coop_kernel_va = MEM32(VM_NTALLOC_THUNK);
    coop_kernel_call();
    if (eax != 0 || !base_va || !size_va || (hint && !(type & 0x2000)))
        return;                            /* failed, or a commit on an existing region */
    base = MEM32(base_va);
    size = (MEM32(size_va) + 0xFFFu) & ~0xFFFu;
    hg_enter();
    blk = xbox_HeapBlockSize(base);
    hg_leave();
    if (blk < size + VM_WASTE_MIN)
        return;
    AcquireSRWLockExclusive(&g_vm_lock);
    {
        int i;
        for (i = 0; i < VM_WASTE_SLOTS && g_vm_waste[i].base; i++)
            ;
        if (i < VM_WASTE_SLOTS) {
            g_vm_waste[i].base = base;
            g_vm_waste[i].waste = blk - size;
            g_vm_waste_live += blk - size;
        }
        if (blk - size > g_vm_waste_max)
            g_vm_waste_max = blk - size;
        g_vm_waste_events++;
    }
    ReleaseSRWLockExclusive(&g_vm_lock);
}

/* NTSTATUS NtFreeVirtualMemory(PVOID *BaseAddress, PSIZE_T RegionSize, ULONG FreeType); ret 12 */
static void vm_NtFreeVirtualMemory(void)
{
    uint32_t base_va = MEM32(esp + 4), size_va = MEM32(esp + 8), type = MEM32(esp + 12);
    uint32_t base = base_va ? MEM32(base_va) : 0;
    uint32_t blk = 0;
    coop_thread_t *self;
    int release = 0, decommit;

    self = coop_enter();
    decommit = !(type & VM_MEM_RELEASE) && (type & VM_MEM_DECOMMIT) && base;
    AcquireSRWLockExclusive(&g_vr_lock);
    if (vr_in_zone(base)) {
        /* A zone region, or not one to release: an interior address or one
         * released already goes to the bridge, which fails it, as it did
         * before for a block the heap no longer had. */
        if (type & VM_MEM_RELEASE) {
            blk = vr_give(base);
            release = blk != 0;
            if (release)
                InterlockedIncrement(&g_vr_returned);
        }
        ReleaseSRWLockExclusive(&g_vr_lock);
    } else {
        ReleaseSRWLockExclusive(&g_vr_lock);
        hg_enter();
        blk = xbox_HeapBlockSize(base);
        /* A block start: the byte before it isn't in the same block. */
        release = (type & VM_MEM_RELEASE) && blk && xbox_HeapBlockSize(base - 1) != blk + 1;
        if (release)
            xbox_HeapFree(base);
        hg_leave();
    }
    if (!release && !decommit) {
        coop_leave(self);
        InterlockedIncrement(&g_vm_bridged);
        t_coop_kernel_va = MEM32(VM_NTFREE_THUNK);
        coop_kernel_call();
        return;
    }
    if (release) {
        if (size_va)
            MEM32(size_va) = blk;
        InterlockedIncrement(&g_vm_released);
        InterlockedExchangeAdd(&g_vm_released_kb, (LONG)(blk / 1024));
    } else {
        InterlockedIncrement(&g_vm_decommits);
    }
    eax = 0;                               /* STATUS_SUCCESS */
    esp += 16;                             /* ret 12 */
    coop_leave(self);
    if (release)
        vm_waste_forget(base);
    heap_watch();
}

/* ── Guest heap guard; thread TIBs (S7, notes 18.40) ──────── */

/*
 * The runtime's heap (xbox_HeapAlloc/xbox_HeapFree: one block table and a
 * bump pointer, reached from NtAllocateVirtualMemory, the pool, thread
 * stacks and TIBs) has no lock. The game's own locks keep its heap calls
 * apart (no two were ever in flight at once, splash to hub and five level
 * loads), except around its threads:
 *
 *   - A worker's TIB is allocated on the new host thread
 *     (bridge_thread_main -> xbox_AllocThreadTib) while the creator carries
 *     on. At the first level load the loader's TIB and the main thread's next
 *     2.6 MB level block got the same bump address, 0x01B50000.
 *   - PsTerminateSystemThread frees the worker's stack while other threads
 *     allocate.
 *   - The TIB is never freed at all. The heap reuses a freed block whole
 *     (first fit, no split), so a loader's small TIB could keep a freed
 *     1.2 MB D3D block or a 2.6 MB level block live for good: 1,200 to
 *     5,379 KiB more heap per menu -> demo cycle until the fifth demo's load
 *     ran it out (the "640x480x4 block per level load" of 18.34).
 *
 * The runtime's thread code is internal, so this is done at the kernel-call
 * boundary, in twoc/ (like NtFreeVirtualMemory above):
 *   - One guard, a Win32 mutex, around every guest kernel call that touches
 *     that heap (below) and around the hand-written NtAllocate/NtFree paths.
 *     A mutex because a worker's PsTerminateSystemThread never returns: the
 *     guard it holds is abandoned at ExitThread, and the next waiter gets it.
 *     It is taken inside the call (depth 1), where a thread never parks.
 *   - PsCreateSystemThreadEx keeps the guard until the new thread has its
 *     TIB: the heap's live-block count has grown by its stack and TIB, or the
 *     thread has made its first kernel call (after its TIB), with a timeout.
 *     Not for the first call, which runs the game itself (INLINE mode) and
 *     never returns, nor with RECOMP_WORKERS=inline.
 *   - PsTerminateSystemThread on a worker (a TIB other than the main one)
 *     frees its TIB first, then gives back its cooperative-suspend record.
 *     Nothing reads the guest TIB after that: the bridge only logs, frees the
 *     stack and calls ExitThread.
 */
#define HG_PS_CREATE_THUNK     0x0015AA8Cu     /* [thunk] = PsCreateSystemThreadEx (ordinal 255) */
#define HG_PS_TERMINATE_THUNK  0x0015AA88u     /* [thunk] = PsTerminateSystemThread (ordinal 258) */
#define HG_TIB_MAIN            0x00004000u     /* xbox_memory_layout.h XBOX_TIB_MAIN */
#define HG_SPAWN_WAIT_MS       2000

static const uint32_t g_hg_thunks[] = {
    0x0015AB3Cu,        /* ExAllocatePool (14) */
    0x0015AC10u,        /* ExAllocatePoolWithTag (15) */
    0x0015AB98u,        /* ExFreePool (17) */
    0x0015AB38u,        /* IoCreateDevice (65) */
    VM_NTALLOC_THUNK,   /* NtAllocateVirtualMemory (184) */
    VM_NTFREE_THUNK,    /* NtFreeVirtualMemory (199), what the hand-write passes on */
    0x0015AB04u,        /* RtlUnicodeStringToAnsiString (308) */
    0x0015AAD0u,        /* MmFreeContiguousMemory (171), what the hand-write passes on */
};

static HANDLE g_hg_mutex;
static INIT_ONCE g_hg_once = INIT_ONCE_STATIC_INIT;
static volatile LONG g_hg_abandoned, g_hg_tibs_freed, g_hg_spawns, g_hg_spawn_timeouts, g_hg_spawn_max_ms;

static BOOL CALLBACK hg_init(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    (void)once; (void)param; (void)ctx;
    g_hg_mutex = CreateMutexA(NULL, FALSE, NULL);
    return g_hg_mutex != NULL;
}

static void hg_enter(void)
{
    InitOnceExecuteOnce(&g_hg_once, hg_init, NULL, NULL);
    if (WaitForSingleObject(g_hg_mutex, INFINITE) == WAIT_ABANDONED)
        InterlockedIncrement(&g_hg_abandoned);  /* a worker ended in PsTerminateSystemThread holding it */
}

static void hg_leave(void)
{
    ReleaseMutex(g_hg_mutex);
}

/* PsCreateSystemThreadEx (ordinal 255): see above. */
static void hg_ps_create(uint32_t va)
{
    static volatile LONG calls;
    const char *workers = getenv("RECOMP_WORKERS");
    xbox_heap_stats_t s0, s;
    LONG reg0;
    ULONGLONG t0;
    LONG ms;

    if (InterlockedIncrement(&calls) == 1 || (workers && !strcmp(workers, "inline"))) {
        recomp_lookup_kernel(va)();
        return;
    }
    hg_enter();
    xbox_HeapStats(&s0);
    reg0 = g_coop_created;
    recomp_lookup_kernel(va)();
    xbox_HeapStats(&s);
    if (eax == 0 && s.live_blocks > s0.live_blocks) {        /* a stack was taken: a thread was spawned */
        t0 = GetTickCount64();
        while (s.live_blocks < s0.live_blocks + 2 && g_coop_created == reg0) {
            if (GetTickCount64() - t0 >= HG_SPAWN_WAIT_MS) {
                if (InterlockedIncrement(&g_hg_spawn_timeouts) <= 5)
                    fprintf(stderr, "[THREAD] spawn: no TIB seen after %u ms; the guard lets go\n",
                            HG_SPAWN_WAIT_MS);
                break;
            }
            SwitchToThread();
            xbox_HeapStats(&s);
        }
        ms = (LONG)(GetTickCount64() - t0);
        if (ms > g_hg_spawn_max_ms)
            g_hg_spawn_max_ms = ms;
        InterlockedIncrement(&g_hg_spawns);
    }
    hg_leave();
}

/* PsTerminateSystemThread (ordinal 258): see above. */
static void hg_ps_terminate(uint32_t va, coop_thread_t *self)
{
    uint32_t tib = g_fs_base, blk = 0;
    int freed = 0;

    hg_enter();
    if (tib && tib != HG_TIB_MAIN) {
        blk = xbox_HeapBlockSize(tib);
        if (blk && xbox_HeapBlockSize(tib - 1) != blk + 1) {
            xbox_HeapFree(tib);
            freed = 1;
            InterlockedIncrement(&g_hg_tibs_freed);
        }
        fprintf(stderr, "[THREAD] worker %lu ends: TIB %08X (%u byte block) %s\n", GetCurrentThreadId(), tib, blk,
                freed ? "freed" : "not a heap block, kept");
        coop_forget(self);
    }
    recomp_lookup_kernel(va)();            /* a worker ends inside (ExitThread), abandoning the guard */
    hg_leave();
}

/* coop_kernel_call's dispatch for the calls above. 0: not one of them. */
static int hg_kernel_call(uint32_t va, coop_thread_t *self)
{
    int i;

    if (va == MEM32(HG_PS_CREATE_THUNK)) {
        hg_ps_create(va);
        return 1;
    }
    if (va == MEM32(HG_PS_TERMINATE_THUNK)) {
        hg_ps_terminate(va, self);
        return 1;
    }
    for (i = 0; i < (int)(sizeof(g_hg_thunks) / sizeof(g_hg_thunks[0])); i++)
        if (va == MEM32(g_hg_thunks[i])) {
            hg_enter();
            recomp_lookup_kernel(va)();
            hg_leave();
            return 1;
        }
    return 0;
}

/* ── MmAllocateContiguousMemory(Ex) / MmFreeContiguousMemory ── */

/*
 * The runtime serves contiguous memory from a bump arena over the physical
 * window at 0x80000000 (xbox_ContiguousAlloc, 64 MB less 128 KiB of GPU
 * instance memory) and never takes any back: its MmFreeContiguousMemory calls
 * xbox_HeapFree, which never matches a 0x80xxxxxx block (notes 18.27). Every
 * level load takes texture and vertex memory from it, so with the executor
 * off, where the attract demo loads again every ~15 s, the arena ran out at
 * ~36 s and the game faulted after a failed read (18.33).
 *
 * The same kind of hand-write as NtFreeVirtualMemory above, kept in twoc/
 * because the arena's state is internal to the runtime:
 *   MmAllocateContiguousMemory[Ex]: first fit from the blocks the game has
 *     freed (split, aligned, zeroed as the runtime zeroes); if none fits, the
 *     bridge's bump allocator, as before. Either way the block's extent (its
 *     size rounded up to a page; the arena aligns every block to a page) is
 *     recorded by base address.
 *   MmFreeContiguousMemory at a recorded base: the extent goes back on the
 *     free list, merged with free neighbours.
 *   Anything else (a pinned physical address from the Ex path, a size of 0,
 *     an address never recorded, a full table): the bridge, as before.
 * Freed memory is reused only below the arena's high-water mark, so the NV2A
 * executor's test for "contiguous memory this runtime handed out"
 * (xbox_ContiguousAllocatedBytes) still holds for every block.
 */
#define CM_ALLOC_THUNK        0x0015AAC8u      /* [thunk] = MmAllocateContiguousMemory (ordinal 165) */
#define CM_ALLOC_EX_THUNK     0x0015AB18u      /* [thunk] = MmAllocateContiguousMemoryEx (ordinal 166) */
#define CM_FREE_THUNK         0x0015AAD0u      /* [thunk] = MmFreeContiguousMemory (ordinal 171) */
#define CM_QSIZE_THUNK        0x0015AB1Cu      /* [thunk] = MmQueryAllocationSize (ordinal 180) */
#define CM_WINDOW             0x80000000u
#define CM_PAGE               0x1000u
#define CM_LIVE_SLOTS         32768            /* power of two */
#define CM_FREE_MAX           8192

typedef struct { uint32_t base, extent; } cm_block_t;
static cm_block_t g_cm_live[CM_LIVE_SLOTS];     /* by base; base 0: empty, 1: deleted */
static cm_block_t g_cm_free[CM_FREE_MAX];       /* sorted by base, neighbours merged */
static int g_cm_free_n;
static SRWLOCK g_cm_lock = SRWLOCK_INIT;
static uint32_t g_cm_live_bytes, g_cm_free_bytes;
static volatile LONG g_cm_allocs, g_cm_reused, g_cm_frees, g_cm_free_bridged, g_cm_untracked, g_cm_qsize;

static cm_block_t *cm_live_slot(uint32_t base, int insert)
{
    uint32_t i, h = (base >> 12) * 2654435761u;
    cm_block_t *tomb = NULL;

    for (i = 0; i < CM_LIVE_SLOTS; i++) {
        cm_block_t *s = &g_cm_live[(h + i) & (CM_LIVE_SLOTS - 1)];
        if (s->base == base)
            return s;
        if (s->base == 1 && !tomb)
            tomb = s;
        if (s->base == 0)
            return insert ? (tomb ? tomb : s) : NULL;
    }
    return insert ? tomb : NULL;
}

/* Caller holds g_cm_lock. Returns 0 if the table is full. */
static int cm_record(uint32_t base, uint32_t extent)
{
    cm_block_t *s = cm_live_slot(base, 1);

    if (!s)
        return 0;
    s->base = base;
    s->extent = extent;
    g_cm_live_bytes += extent;
    return 1;
}

/* Caller holds g_cm_lock. First fit that honours `align`; the pieces before
 * and after stay on the list. Returns 0 if nothing fits. */
static uint32_t cm_take(uint32_t extent, uint32_t align)
{
    int i;

    for (i = 0; i < g_cm_free_n; i++) {
        uint32_t b = g_cm_free[i].base, e = g_cm_free[i].extent;
        uint32_t a = (b + align - 1) & ~(align - 1);
        if (a < b || a - b > e || e - (a - b) < extent)
            continue;
        {
            uint32_t head = a - b, tail = e - head - extent;
            if (head && tail) {
                if (g_cm_free_n == CM_FREE_MAX)
                    continue;              /* no room to split in three */
                memmove(&g_cm_free[i + 2], &g_cm_free[i + 1], (g_cm_free_n - i - 1) * sizeof(g_cm_free[0]));
                g_cm_free_n++;
                g_cm_free[i].extent = head;
                g_cm_free[i + 1].base = a + extent;
                g_cm_free[i + 1].extent = tail;
            } else if (head) {
                g_cm_free[i].extent = head;
            } else if (tail) {
                g_cm_free[i].base = a + extent;
                g_cm_free[i].extent = tail;
            } else {
                memmove(&g_cm_free[i], &g_cm_free[i + 1], (g_cm_free_n - i - 1) * sizeof(g_cm_free[0]));
                g_cm_free_n--;
            }
        }
        g_cm_free_bytes -= extent;
        return a;
    }
    return 0;
}

/* Caller holds g_cm_lock. Returns 0 if the list is full (the block leaks, as
 * every block did before). */
static int cm_give(uint32_t base, uint32_t extent)
{
    int lo = 0, hi = g_cm_free_n, i;

    while (lo < hi) {                      /* first block above base */
        int mid = (lo + hi) / 2;
        if (g_cm_free[mid].base < base)
            lo = mid + 1;
        else
            hi = mid;
    }
    i = lo;
    if (i > 0 && g_cm_free[i - 1].base + g_cm_free[i - 1].extent == base) {
        g_cm_free[i - 1].extent += extent;             /* merge down */
        if (i < g_cm_free_n && base + extent == g_cm_free[i].base) {
            g_cm_free[i - 1].extent += g_cm_free[i].extent;   /* and up */
            memmove(&g_cm_free[i], &g_cm_free[i + 1], (g_cm_free_n - i - 1) * sizeof(g_cm_free[0]));
            g_cm_free_n--;
        }
    } else if (i < g_cm_free_n && base + extent == g_cm_free[i].base) {
        g_cm_free[i].base = base;                      /* merge up */
        g_cm_free[i].extent += extent;
    } else {
        if (g_cm_free_n == CM_FREE_MAX)
            return 0;
        memmove(&g_cm_free[i + 1], &g_cm_free[i], (g_cm_free_n - i) * sizeof(g_cm_free[0]));
        g_cm_free[i].base = base;
        g_cm_free[i].extent = extent;
        g_cm_free_n++;
    }
    g_cm_free_bytes += extent;
    return 1;
}

/* The shared allocation path: `ret` is the stdcall argument bytes. */
static void cm_alloc(uint32_t thunk, uint32_t size, uint32_t align, uint32_t ret)
{
    uint32_t extent = (size + CM_PAGE - 1) & ~(CM_PAGE - 1), va = 0;
    coop_thread_t *self;

    if (align < CM_PAGE)
        align = CM_PAGE;
    if (size && extent >= size) {
        AcquireSRWLockExclusive(&g_cm_lock);
        va = cm_take(extent, align);
        if (va && !cm_record(va, extent)) {
            cm_give(va, extent);
            va = 0;
        }
        ReleaseSRWLockExclusive(&g_cm_lock);
    }
    if (!va) {                             /* the bridge's bump arena, as before */
        t_coop_kernel_va = MEM32(thunk);
        coop_kernel_call();
        va = eax;
        InterlockedIncrement(&g_cm_allocs);
        if (va >= CM_WINDOW && size && (va & (CM_PAGE - 1)) == 0) {
            int ok;
            AcquireSRWLockExclusive(&g_cm_lock);
            ok = cm_record(va, extent);
            ReleaseSRWLockExclusive(&g_cm_lock);
            if (!ok)
                InterlockedIncrement(&g_cm_untracked);
        } else if (va) {
            InterlockedIncrement(&g_cm_untracked);   /* pinned, or size 0 */
        }
        return;
    }
    self = coop_enter();
    memset((void *)XBOX_PTR(va), 0, size);           /* as xbox_ContiguousAlloc does */
    InterlockedIncrement(&g_cm_allocs);
    InterlockedIncrement(&g_cm_reused);
    eax = va;
    esp += 4 + ret;
    coop_leave(self);
}

/* PVOID MmAllocateContiguousMemory(ULONG NumberOfBytes); ret 4 */
static void cm_MmAllocateContiguousMemory(void)
{
    cm_alloc(CM_ALLOC_THUNK, MEM32(esp + 4), CM_PAGE, 4);
}

/* PVOID MmAllocateContiguousMemoryEx(SIZE_T NumberOfBytes, ULONG_PTR Lowest,
 *     ULONG_PTR Highest, ULONG_PTR Alignment, ULONG Protect); ret 20.
 * A range that pins one physical address goes to the bridge unchanged (it
 * maps it directly, outside the arena); so does an alignment that isn't a
 * power of two. The range is otherwise ignored, as the bridge ignores it. */
static void cm_MmAllocateContiguousMemoryEx(void)
{
    uint32_t size = MEM32(esp + 4), low = MEM32(esp + 8), high = MEM32(esp + 12), align = MEM32(esp + 16);

    if ((low && high >= low && high - low + 1 <= size + CM_PAGE) || (align & (align - 1))) {
        t_coop_kernel_va = MEM32(CM_ALLOC_EX_THUNK);
        coop_kernel_call();
        if (eax)
            InterlockedIncrement(&g_cm_untracked);
        return;
    }
    cm_alloc(CM_ALLOC_EX_THUNK, size, align, 20);
}

/* VOID MmFreeContiguousMemory(PVOID BaseAddress); ret 4 */
static void cm_MmFreeContiguousMemory(void)
{
    uint32_t base = MEM32(esp + 4), extent = 0;
    cm_block_t *s;
    coop_thread_t *self;
    int freed = 0;

    AcquireSRWLockExclusive(&g_cm_lock);
    s = base >= CM_WINDOW ? cm_live_slot(base, 0) : NULL;
    if (s) {
        extent = s->extent;
        s->base = 1;                       /* deleted */
        g_cm_live_bytes -= extent;
        freed = cm_give(base, extent);
    }
    ReleaseSRWLockExclusive(&g_cm_lock);
    if (!s) {
        InterlockedIncrement(&g_cm_free_bridged);
        t_coop_kernel_va = MEM32(CM_FREE_THUNK);
        coop_kernel_call();
        return;
    }
    self = coop_enter();
    InterlockedIncrement(&g_cm_frees);
    if (!freed)
        InterlockedIncrement(&g_cm_untracked);   /* free list full: this block stays leaked */
    d3db_memory_freed(base, extent);       /* the mirror's render targets in it (bridge on) */
    esp += 8;                              /* ret 4 */
    coop_leave(self);
    heap_watch();
}

/* ULONG MmQueryAllocationSize(PVOID BaseAddress); ret 4. The bridge answers
 * from the guest heap only, so a contiguous block read as 0. Answered from
 * the table when the address is a recorded block's base. The only caller seen
 * is DSOUND's contiguous allocator (0x137B5A, 19 calls at boot): it adds the
 * size to its usage counter [0x153254] and zeroes that many bytes, so with 0
 * it counted and zeroed nothing (the arena had zeroed the block anyway). */
static void cm_MmQueryAllocationSize(void)
{
    uint32_t base = MEM32(esp + 4), extent = 0;
    cm_block_t *s = NULL;
    coop_thread_t *self;

    if (base >= CM_WINDOW) {
        InterlockedIncrement(&g_cm_qsize);
        AcquireSRWLockShared(&g_cm_lock);
        s = cm_live_slot(base, 0);
        if (s)
            extent = s->extent;
        ReleaseSRWLockShared(&g_cm_lock);
    }
    if (!s) {
        t_coop_kernel_va = MEM32(CM_QSIZE_THUNK);
        coop_kernel_call();
        return;
    }
    self = coop_enter();
    eax = extent;
    esp += 8;                              /* ret 4 */
    coop_leave(self);
}

/* ── MmGetPhysicalAddress for DSOUND (notes 18.44) ────────── */

/*
 * The APU reads everything DSOUND hands it by physical address. DSOUND's
 * voice array, SGE lists and buffers are contiguous memory (VA 0x80000000 +
 * physical offset), but it also streams from heap buffers, and asks for their
 * physical address page by page (0x13DDB7: a packet's pages into its SGE
 * list). The runtime keeps the contiguous window apart from the RAM at VA 0,
 * so the two ranges overlap as physical addresses: 0x00B80000 is both a
 * voice (window) and part of the image (RAM).
 *
 * So a DSOUND caller asking about a low-RAM address gets it back with
 * PHYS_LOWRAM (bit 26) set, and the emulated APU's accessors (a local change
 * in xboxrecomp's apu_shim.h, apu_phys) read a marked address from RAM and
 * any other from the contiguous window. Bit 26 is above the 64 MB of RAM;
 * DSOUND never masks the answer, and page arithmetic (0x13EF51's contiguity
 * check compares one page's answer with the next) keeps it. Contiguous addresses
 * and every other caller (XPP) still go to the runtime.
 */
#define PA_THUNK              0x0015ABD4u      /* [thunk] = MmGetPhysicalAddress (ordinal 173) */
#define PA_DSOUND_LO          0x00135AA0u      /* the DSOUND section */
#define PA_DSOUND_HI          0x001533E4u
#define PA_LOWRAM             0x04000000u      /* APU_PHYS_LOWRAM (xboxrecomp apu_shim.h, local) */
static volatile LONG g_pa_lowram;

/* ULONG_PTR MmGetPhysicalAddress(PVOID BaseAddress); ret 4 */
static void pa_MmGetPhysicalAddress(void)
{
    uint32_t ret = MEM32(esp), va = MEM32(esp + 4);
    coop_thread_t *self;

    if (va >= CM_WINDOW || ret < PA_DSOUND_LO || ret >= PA_DSOUND_HI) {
        t_coop_kernel_va = MEM32(PA_THUNK);
        coop_kernel_call();
        return;
    }
    self = coop_enter();
    eax = va | PA_LOWRAM;
    if (InterlockedIncrement(&g_pa_lowram) <= 3)
        fprintf(stderr, "[APU] DSOUND low-RAM buffer %08X -> physical %08X (marked for the APU; caller %08X)\n",
                va, eax, ret);
    esp += 8;                              /* ret 4 */
    coop_leave(self);
}

/* ── The speaker setting: surround at 5.1 (notes 18.58) ──── */

/*
 * The game learns its speakers the way every title does: XAPI's
 * XGetAudioFlags (sub_000EDCA2) reads XC_AUDIO from the EEPROM through
 * ExQueryNonVolatileSetting, once for DSOUND's settings (sub_00136DBA, kept
 * at +8: the speaker config its 3D voices are panned for) and once for the
 * game's sound manager (0x427A58 + 0xC, sub_000B99D0). The runtime answers
 * 0, stereo, and DSOUND then pans 3D voices over the front pair only.
 *
 * When the output opened at 5.1 (RECOMP_SPEAKERS=5.1 and a device with those
 * speakers, xboxrecomp apu_xaudio2.c), XC_AUDIO answers
 * XC_AUDIO_FLAGS_SURROUND, the dashboard's "Surround" setting, so the panning
 * reaches the rear bins the 5.1 output plays. Any other index, or stereo
 * output: the runtime, as before. The AV pack (HalBootSMCVideoMode, 0 here)
 * doesn't mask the flags.
 *
 * Step 74: the time zone. Save times in the Load Game list showed UTC. XAPI's
 * GetTimeZoneInformation (sub_000ECD9E, through XQueryValue sub_000EDC0F)
 * reads XC_MAX_OS (0xFF), the EEPROM's whole user section, and takes the
 * bias at +4, the 4-letter names at +8 and +0xC, the standard and daylight
 * dates at +0x18 and +0x1C (month, week of the month, day of the week,
 * hour), their biases at +0x28 and +0x2C, and "daylight saving off" from bit
 * 1 of the flags at +0x58. The runtime zero-fills that index (UTC, no
 * daylight saving). Now XC_MAX_OS and XC_TIMEZONE_BIAS .. XC_TZ_DLT_BIAS
 * (0-6) answer Windows' time zone and its "adjust for daylight saving time"
 * setting, as the dashboard would have stored them; the rest of the section
 * stays zero, as before.
 */
#define NV_QUERY_THUNK        0x0015AB28u      /* [thunk] = ExQueryNonVolatileSetting (ordinal 24) */
#define NV_XC_AUDIO           9u
#define NV_AUDIO_SURROUND     0x00000002u      /* XC_AUDIO_FLAGS_SURROUND */
#define NV_XC_TZ_LAST         6u               /* XC_TIMEZONE_BIAS (0) .. XC_TZ_DLT_BIAS (6) */
#define NV_XC_MAX_OS          0xFFu            /* the user section, 0x60 bytes */
#define NV_USER_SIZE          0x60u
#define NV_MISC_DST_OFF       0x00000002u      /* XC_MISC_FLAG_DSTOFF, at +0x58 */
extern int mcpx_apu_output_channels(int *device_channels);

/* Windows' "Eastern Daylight Time" as the dashboard's 4 letters: the words'
 * initials ("EDT"; "AUS Eastern Standard Time" -> "AEST"). */
static void nv_tz_name(const WCHAR *w, uint8_t out[4])
{
    int n = 0, start = 1;

    memset(out, 0, 4);
    for (; *w && n < 4; w++) {
        if (*w == L' ' || *w == L'.' || *w == L'(' || *w == L')') {
            start = 1;
        } else if (start) {
            out[n++] = *w < 0x80 ? (uint8_t)*w : '?';
            start = 0;
        }
    }
}

/* A rule's date: month, the week of the month it falls in (5: the last),
 * the day of the week, the hour. */
static void nv_tz_date(const SYSTEMTIME *t, uint8_t out[4])
{
    out[0] = (uint8_t)t->wMonth;
    out[1] = (uint8_t)(t->wYear ? (t->wDay + 6) / 7 : t->wDay);     /* an absolute date: its week */
    out[2] = (uint8_t)t->wDayOfWeek;
    out[3] = (uint8_t)t->wHour;
}

/* The user section's time zone fields from Windows (the rest left as it
 * is); returns the misc flags' daylight-saving bit. */
static uint32_t nv_tz_user(uint8_t u[NV_USER_SIZE])
{
    static volatile LONG told;
    TIME_ZONE_INFORMATION tz;
    DYNAMIC_TIME_ZONE_INFORMATION dz;
    int32_t v;
    uint32_t off = 0;

    memset(&tz, 0, sizeof(tz));
    if (GetTimeZoneInformation(&tz) == TIME_ZONE_ID_INVALID)
        return 0;
    memset(&dz, 0, sizeof(dz));
    if (GetDynamicTimeZoneInformation(&dz) != TIME_ZONE_ID_INVALID && dz.DynamicDaylightTimeDisabled)
        off = NV_MISC_DST_OFF;
    v = (int32_t)tz.Bias;
    memcpy(u + 0x04, &v, 4);
    nv_tz_name(tz.StandardName, u + 0x08);
    nv_tz_name(tz.DaylightName, u + 0x0C);
    nv_tz_date(&tz.StandardDate, u + 0x18);
    nv_tz_date(&tz.DaylightDate, u + 0x1C);
    v = (int32_t)tz.StandardBias;
    memcpy(u + 0x28, &v, 4);
    v = (int32_t)tz.DaylightBias;
    memcpy(u + 0x2C, &v, 4);
    if (InterlockedIncrement(&told) == 1)
        fprintf(stderr, "[TIME] the game's time zone: bias %ld min, standard %.4s (%+ld, month %u week %u day %u"
                " %u:00), daylight %.4s (%+ld, month %u week %u day %u %u:00)%s\n", tz.Bias, (const char *)u + 0x08,
                tz.StandardBias, u[0x18], u[0x19], u[0x1A], u[0x1B], (const char *)u + 0x0C, tz.DaylightBias,
                u[0x1C], u[0x1D], u[0x1E], u[0x1F], off ? "; daylight saving off" : "");
    return off;
}

/* NTSTATUS ExQueryNonVolatileSetting(ValueIndex, Type*, Value*, ValueLength, ResultLength*); ret 20 */
static void nv_ExQueryNonVolatileSetting(void)
{
    uint32_t index = MEM32(esp + 4), type = MEM32(esp + 8), value = MEM32(esp + 12);
    uint32_t len = MEM32(esp + 16), result = MEM32(esp + 20);
    coop_thread_t *self;
    static volatile LONG told;

    if (index <= NV_XC_TZ_LAST || index == NV_XC_MAX_OS) {     /* step 74: the time zone */
        static const uint8_t field[7][2] = { { 0x04, 4 }, { 0x08, 4 }, { 0x0C, 4 }, { 0x18, 4 }, { 0x1C, 4 },
                                             { 0x28, 4 }, { 0x2C, 4 } };
        uint8_t u[NV_USER_SIZE];
        uint32_t i, n;

        self = coop_enter();
        memset(u, 0, sizeof(u));
        u[0x58] |= (uint8_t)nv_tz_user(u);
        if (index == NV_XC_MAX_OS) {
            n = len;                       /* as the runtime answers: the length asked, zeros past the section */
            for (i = 0; value && i < n; i++)
                MEM8(value + i) = i < NV_USER_SIZE ? u[i] : 0;
        } else {
            n = len < field[index][1] ? len : field[index][1];
            for (i = 0; value && i < len; i++)
                MEM8(value + i) = i < n ? u[field[index][0] + i] : 0;
        }
        if (type)
            MEM32(type) = 4;               /* as the runtime answers */
        if (result)
            MEM32(result) = n;
        eax = value ? 0 : 0xC000000Du;     /* STATUS_SUCCESS; STATUS_INVALID_PARAMETER without a buffer */
        esp += 24;                         /* ret 20 */
        coop_leave(self);
        return;
    }
    if (index != NV_XC_AUDIO || mcpx_apu_output_channels(NULL) != 6) {
        t_coop_kernel_va = MEM32(NV_QUERY_THUNK);
        coop_kernel_call();
        return;
    }
    self = coop_enter();
    if (value && len >= 4) {               /* as the runtime answers: a dword, success */
        MEM32(value) = NV_AUDIO_SURROUND;
        if (type)
            MEM32(type) = 4;               /* REG_DWORD */
        if (result)
            MEM32(result) = 4;
    }
    if (InterlockedIncrement(&told) <= 2)
        fprintf(stderr, "[AUDIO] the game asked for its speakers (caller %08X): surround (5.1 output)\n",
                MEM32(esp));
    eax = 0;                               /* STATUS_SUCCESS */
    esp += 24;                             /* ret 20 */
    coop_leave(self);
}

/* ── Kernel pool: an arena of its own (notes 18.42) ──────── */

/*
 * The runtime serves ExAllocatePool[WithTag] from the guest heap, and the
 * heap reuses a freed block whole (first fit, no split). The pool is small
 * and allocate-once (splash to hub: 864 allocations, 976 KiB, 19 frees;
 * almost all from DSOUND's helper sub_00137AFB), but it allocates all game
 * long, so its requests kept landing in the multi-MB blocks the game had
 * just freed: at the hub 841 DSOUND blocks asking 970 KiB held 16.5 MB of
 * the heap's 39.8 MB live. An 84-byte request held the freed 7 MB level
 * pool, and a 24-byte one a 2.6 MB level block. Nothing else could use them,
 * so level loads bumped the heap to 93.5% and left a 4.1 MB largest block.
 *
 * So, small requests (up to POOL_SMALL_MAX) come from an arena taken from
 * the heap once, at the first pool call (still bump space, so the arena
 * costs exactly its size): first fit, split, coalesced on free, 16-byte
 * granules as the runtime's pool had. The arena's layout is kept here, not
 * in guest memory. Bigger requests, and small ones once the arena is full,
 * go to the bridge as before.
 *
 *   ExAllocatePool / ExAllocatePoolWithTag: from the arena, zeroed.
 *   ExFreePool: an arena block goes back to it; anything else to the bridge.
 *   ExQueryPoolBlockSize: an arena block's size. The bridge answers from the
 *     heap's block table, which would report the rest of the whole arena for
 *     an address inside it, and DSOUND zeroes that many bytes after every
 *     allocation (and keeps a usage count from it, [0x153258]).
 * They run inside the cooperative boundary, like the other hand-writes.
 */
#define POOL_ALLOC_THUNK      0x0015AB3Cu      /* [thunk] = ExAllocatePool (ordinal 14) */
#define POOL_ALLOC_TAG_THUNK  0x0015AC10u      /* [thunk] = ExAllocatePoolWithTag (ordinal 15) */
#define POOL_FREE_THUNK       0x0015AB98u      /* [thunk] = ExFreePool (ordinal 17) */
#define POOL_QSIZE_THUNK      0x0015AC0Cu      /* [thunk] = ExQueryPoolBlockSize (ordinal 23) */
#define POOL_ARENA_SIZE       (512u * 1024u)   /* ~3x the small-pool peak seen splash to hub */
#define POOL_SMALL_MAX        0x10000u         /* larger requests stay on the heap */
#define POOL_GRANULE          16u
#define POOL_MAX_EXTENTS      8192

typedef struct { uint32_t off, size, used; } pool_extent_t;

/* Extents tile the arena in address order, so neighbours in the array are
 * neighbours in memory. */
static pool_extent_t g_pool_ext[POOL_MAX_EXTENTS];
static int g_pool_n;
static uint32_t g_pool_base;               /* 0 = not made yet */
static int g_pool_failed;                  /* the heap had no room for the arena */
static SRWLOCK g_pool_lock = SRWLOCK_INIT;
static uint32_t g_pool_live, g_pool_live_n, g_pool_peak;
static volatile LONG g_pool_allocs, g_pool_frees, g_pool_to_heap, g_pool_full;

/* Index of the extent starting at off, or of the one containing it if
 * `containing`; -1 if none. Caller holds the lock. */
static int pool_find(uint32_t off, int containing)
{
    int lo = 0, hi = g_pool_n - 1;

    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        pool_extent_t *e = &g_pool_ext[mid];
        if (off < e->off)
            hi = mid - 1;
        else if (off >= e->off + e->size)
            lo = mid + 1;
        else
            return (containing || off == e->off) ? mid : -1;
    }
    return -1;
}

/* The arena's guest address for `size` bytes, or 0. Caller holds the lock. */
static uint32_t pool_take(uint32_t size)
{
    uint32_t need = (size + POOL_GRANULE - 1) & ~(POOL_GRANULE - 1);
    int i;

    if (!need)
        need = POOL_GRANULE;
    for (i = 0; i < g_pool_n; i++) {
        pool_extent_t *e = &g_pool_ext[i];
        if (e->used || e->size < need)
            continue;
        if (e->size > need && g_pool_n < POOL_MAX_EXTENTS) {       /* split */
            memmove(&g_pool_ext[i + 2], &g_pool_ext[i + 1], (g_pool_n - i - 1) * sizeof(g_pool_ext[0]));
            g_pool_ext[i + 1].off = e->off + need;
            g_pool_ext[i + 1].size = e->size - need;
            g_pool_ext[i + 1].used = 0;
            e->size = need;
            g_pool_n++;
        }
        e->used = 1;
        g_pool_live += e->size;
        g_pool_live_n++;
        if (g_pool_live > g_pool_peak)
            g_pool_peak = g_pool_live;
        return g_pool_base + e->off;
    }
    return 0;
}

/* Frees the arena block at va. 0 if va isn't one. Caller holds the lock. */
static int pool_give(uint32_t va)
{
    int i;

    if (!g_pool_base || va < g_pool_base || va >= g_pool_base + POOL_ARENA_SIZE)
        return 0;
    i = pool_find(va - g_pool_base, 0);
    if (i < 0 || !g_pool_ext[i].used)
        return 0;
    g_pool_ext[i].used = 0;
    g_pool_live -= g_pool_ext[i].size;
    g_pool_live_n--;
    if (i + 1 < g_pool_n && !g_pool_ext[i + 1].used) {             /* merge up */
        g_pool_ext[i].size += g_pool_ext[i + 1].size;
        memmove(&g_pool_ext[i + 1], &g_pool_ext[i + 2], (g_pool_n - i - 2) * sizeof(g_pool_ext[0]));
        g_pool_n--;
    }
    if (i > 0 && !g_pool_ext[i - 1].used) {                        /* and down */
        g_pool_ext[i - 1].size += g_pool_ext[i].size;
        memmove(&g_pool_ext[i], &g_pool_ext[i + 1], (g_pool_n - i - 1) * sizeof(g_pool_ext[0]));
        g_pool_n--;
    }
    return 1;
}

/* The shared allocation path: `ret` is the stdcall argument bytes. */
static void pool_alloc(uint32_t thunk, uint32_t size, uint32_t ret)
{
    coop_thread_t *self;
    uint32_t va = 0;

    if (size && size <= POOL_SMALL_MAX) {
        self = coop_enter();
        AcquireSRWLockExclusive(&g_pool_lock);
        if (!g_pool_base && !g_pool_failed) {
            hg_enter();
            g_pool_base = xbox_HeapAlloc(POOL_ARENA_SIZE, POOL_GRANULE);
            hg_leave();
            g_pool_failed = !g_pool_base;
            if (g_pool_base) {
                g_pool_ext[0].off = 0;
                g_pool_ext[0].size = POOL_ARENA_SIZE;
                g_pool_ext[0].used = 0;
                g_pool_n = 1;
            }
            fprintf(stderr, "[POOL] arena %u KiB at %08X%s\n", POOL_ARENA_SIZE / 1024, g_pool_base,
                    g_pool_base ? "" : ": no room, the pool stays on the heap");
        }
        if (g_pool_base)
            va = pool_take(size);
        ReleaseSRWLockExclusive(&g_pool_lock);
        if (va) {
            memset((void *)XBOX_PTR(va), 0, (size + POOL_GRANULE - 1) & ~(POOL_GRANULE - 1));
            InterlockedIncrement(&g_pool_allocs);
            eax = va;
            esp += 4 + ret;
            coop_leave(self);
            return;
        }
        coop_leave(self);
        if (g_pool_base && InterlockedIncrement(&g_pool_full) <= 5)
            fprintf(stderr, "[POOL] arena full: %u bytes go to the heap\n", size);
    }
    InterlockedIncrement(&g_pool_to_heap);
    t_coop_kernel_va = MEM32(thunk);
    coop_kernel_call();
}

/* PVOID ExAllocatePool(SIZE_T NumberOfBytes); ret 4 */
static void pool_ExAllocatePool(void)
{
    pool_alloc(POOL_ALLOC_THUNK, MEM32(esp + 4), 4);
}

/* PVOID ExAllocatePoolWithTag(SIZE_T NumberOfBytes, ULONG Tag); ret 8 */
static void pool_ExAllocatePoolWithTag(void)
{
    pool_alloc(POOL_ALLOC_TAG_THUNK, MEM32(esp + 4), 8);
}

/* VOID ExFreePool(PVOID P); ret 4 */
static void pool_ExFreePool(void)
{
    uint32_t va = MEM32(esp + 4);
    coop_thread_t *self = coop_enter();
    int ours;

    AcquireSRWLockExclusive(&g_pool_lock);
    ours = pool_give(va);
    ReleaseSRWLockExclusive(&g_pool_lock);
    if (!ours) {
        coop_leave(self);
        t_coop_kernel_va = MEM32(POOL_FREE_THUNK);
        coop_kernel_call();
        return;
    }
    InterlockedIncrement(&g_pool_frees);
    esp += 8;                              /* ret 4 */
    coop_leave(self);
}

/* ULONG ExQueryPoolBlockSize(PVOID PoolBlock); ret 4. Like the bridge's
 * answer, an address inside a block gets the rest of that block. */
static void pool_ExQueryPoolBlockSize(void)
{
    uint32_t va = MEM32(esp + 4), size = 0;
    coop_thread_t *self;
    int i = -1;

    AcquireSRWLockShared(&g_pool_lock);
    if (g_pool_base && va >= g_pool_base && va < g_pool_base + POOL_ARENA_SIZE) {
        i = pool_find(va - g_pool_base, 1);
        if (i >= 0 && g_pool_ext[i].used)
            size = g_pool_ext[i].off + g_pool_ext[i].size - (va - g_pool_base);
        else
            i = -1;
    }
    ReleaseSRWLockShared(&g_pool_lock);
    if (i < 0) {
        t_coop_kernel_va = MEM32(POOL_QSIZE_THUNK);
        coop_kernel_call();
        return;
    }
    self = coop_enter();
    eax = size;
    esp += 8;                              /* ret 4 */
    coop_leave(self);
}

static void heap_watch_lines(void);

/* One [HEAPSTAT] line every 2 s, from whichever thread gets there. */
static void heap_watch(void)
{
    static volatile LONG last;
    LONG now = (LONG)GetTickCount(), prev = last;

    if (prev && now - prev < HEAP_WATCH_MS)
        return;
    if (InterlockedCompareExchange(&last, now, prev) != prev)
        return;
    if (!prev)
        return;                            /* start the clock */
    heap_watch_lines();
}

/* The lines, as one write (log_block.h): kept out of heap_watch, which runs
 * at every kernel call, so the 8 KB block costs nothing on that path. */
static __declspec(noinline) void heap_watch_lines(void)
{
    xbox_heap_stats_t s;
    log_block_t lb;

    lb.len = 0;
    xbox_HeapStats(&s);
    log_block_printf(&lb, "[HEAPSTAT] used %u/%u KiB (%.1f%%); live %u KiB in %u blocks; "
                    "free %u KiB in %u blocks, largest %u KiB; VM released %ld (%ld KiB), "
                    "decommits %ld, bridged %ld; reuse slack %u KiB live, max %u KiB, %u events; "
                    "threads: TIBs freed %ld, spawn waits %ld (timeouts %ld, longest %ld ms), guard abandoned %ld\n",
            s.used / 1024, s.total / 1024, s.total ? 100.0 * s.used / s.total : 0.0,
            s.live / 1024, s.live_blocks, s.free / 1024, s.free_blocks, s.largest_free / 1024,
            (long)g_vm_released, (long)g_vm_released_kb, (long)g_vm_decommits, (long)g_vm_bridged,
            g_vm_waste_live / 1024, g_vm_waste_max / 1024, g_vm_waste_events,
            (long)g_hg_tibs_freed, (long)g_hg_spawns, (long)g_hg_spawn_timeouts, (long)g_hg_spawn_max_ms,
            (long)g_hg_abandoned);
    {
        uint32_t live, free_b;
        int free_n;
        AcquireSRWLockShared(&g_cm_lock);
        live = g_cm_live_bytes;
        free_b = g_cm_free_bytes;
        free_n = g_cm_free_n;
        ReleaseSRWLockShared(&g_cm_lock);
        log_block_printf(&lb, "[CONTIGSTAT] arena high-water %u KiB; live %u KiB, free for reuse %u KiB in %d blocks;"
                        " allocs %ld (reused %ld), frees %ld; to the bridge: frees %ld, untracked blocks %ld;"
                        " size queries %ld\n",
                xbox_ContiguousAllocatedBytes() / 1024, live / 1024, free_b / 1024, free_n,
                (long)g_cm_allocs, (long)g_cm_reused, (long)g_cm_frees, (long)g_cm_free_bridged,
                (long)g_cm_untracked, (long)g_cm_qsize);
    }
    {
        uint32_t live, live_n, peak, largest = 0;
        int i;
        AcquireSRWLockShared(&g_pool_lock);
        live = g_pool_live;
        live_n = g_pool_live_n;
        peak = g_pool_peak;
        for (i = 0; i < g_pool_n; i++)
            if (!g_pool_ext[i].used && g_pool_ext[i].size > largest)
                largest = g_pool_ext[i].size;
        ReleaseSRWLockShared(&g_pool_lock);
        log_block_printf(&lb, "[POOLSTAT] arena %u KiB at %08X; live %u KiB in %u blocks, peak %u KiB, largest free %u KiB;"
                        " allocs %ld, frees %ld; to the heap %ld (arena full %ld)\n",
                g_pool_base ? POOL_ARENA_SIZE / 1024 : 0, g_pool_base, live / 1024, live_n, peak / 1024, largest / 1024,
                (long)g_pool_allocs, (long)g_pool_frees, (long)g_pool_to_heap, (long)g_pool_full);
    }
    {
        uint32_t free_b, live_b, peak, largest = 0;
        int free_n, live_n, i;
        AcquireSRWLockShared(&g_vr_lock);
        free_b = g_vr_free_bytes;
        live_b = g_vr_live_bytes;
        peak = g_vr_live_peak;
        free_n = g_vr_free_n;
        live_n = g_vr_live_n;
        for (i = 0; i < g_vr_free_n; i++)
            if (g_vr_free[i].size > largest)
                largest = g_vr_free[i].size;
        ReleaseSRWLockShared(&g_vr_lock);
        log_block_printf(&lb, "[VMSTAT] zone %u KiB at %08X: live %u KiB in %d regions (peak %u KiB), free %u KiB in %d ranges"
                        " (largest %u KiB); regions %ld, released %ld, to the heap %ld, table full %ld;"
                        " heap live less zone free %u KiB\n",
                g_vr_zone_size / 1024, g_vr_zone, live_b / 1024, live_n, peak / 1024, free_b / 1024, free_n,
                largest / 1024, (long)g_vr_cut, (long)g_vr_returned, (long)g_vr_to_heap, (long)g_vr_table_full,
                (s.live - free_b) / 1024);
    }
    log_block_flush(&lb);
}

/* ── Step 73: a title that leaves for the dashboard says why ── */

/*
 * XAPI opens U: (game_files\UDATA\56550003) for writing at startup. When it
 * can't (the folder read-only for this user), it boots to the dashboard with
 * XLD_LAUNCH_DASHBOARD_ERROR / XLD_ERROR_INVALID_HARD_DISK, and here that was a
 * silent exit about a second after launch. The exit still happens; on that
 * path only, the reason goes to the log and a message box first.
 */
#define HAL_RETURN_THUNK      0x0015AAD8u      /* [thunk] = HalReturnToFirmware (ordinal 49) */
#define LAUNCH_PAGE_THUNK     0x0015AACCu      /* [thunk] = &LaunchDataPage (ordinal 164) */

static void hal_HalReturnToFirmware(void)
{
    uint32_t va = MEM32(HAL_RETURN_THUNK);
    uint32_t var = MEM32(LAUNCH_PAGE_THUNK);
    uint32_t page = var ? MEM32(var) : 0;

    /* LAUNCH_DATA_PAGE: type at +0 (1 = the dashboard), LD_LAUNCH_DASHBOARD
     * at +1024 (dwReason 1 = an error, dwParameter1 2 = the hard disk) */
    if (page && MEM32(page) == 1 && MEM32(page + 1024) == 1 && MEM32(page + 1032) == 2) {
        char dir[MAX_PATH], msg[MAX_PATH + 256];
        if (!GetFullPathNameA("game_files\\UDATA\\56550003", sizeof(dir), dir, NULL))
            strcpy(dir, "game_files\\UDATA\\56550003");
        fprintf(stderr, "[SAVE] the game could not open its save folder %s for writing, and quits"
                " (the Xbox's \"hard disk\" error)\n", dir);
        fflush(stderr);
        _snprintf(msg, sizeof(msg), "Crash can't write to its save folder:\n\n%s\n\n"
                  "Make the folder writable (or delete it: the game makes a new one) and start again.", dir);
        msg[sizeof(msg) - 1] = 0;
        MessageBoxA(NULL, msg, "Crash TWOC", MB_ICONERROR | MB_OK);
    }
    t_coop_kernel_va = va;
    coop_kernel_call();
}

/* ── Step 83: the game's save files, for their backups ─────── */

/*
 * saves.c copies the save a few seconds after the game closes a save file it
 * wrote, and never while the game has one open. These taps tell it: each
 * open in a save folder (UDATA\56550003\<save>\..., the path as the game
 * passes it) and the close of a handle one returned. The calls themselves
 * go to the kernel as before; a path elsewhere costs a string search.
 */
#define SV_NTCREATE_THUNK     0x0015AB34u      /* [thunk] = NtCreateFile (ordinal 190) */
#define SV_NTOPEN_THUNK       0x0015AAE8u      /* [thunk] = NtOpenFile (ordinal 202) */
#define SV_NTCLOSE_THUNK      0x0015AAB0u      /* [thunk] = NtClose (ordinal 187) */

/* OBJECT_ATTRIBUTES { HANDLE RootDirectory; PSTRING ObjectName; ULONG Attributes }
 * and STRING { USHORT Length, MaximumLength; PCHAR Buffer } */
static int sv_open_begin(uint32_t oa, uint32_t access, uint32_t share, uint32_t disposition, uint32_t options)
{
    uint32_t name = oa ? MEM32(oa + 4) : 0;
    uint32_t buf = name ? MEM32(name + 4) : 0;
    return buf ? saves_guest_open_begin((const char *)XBOX_PTR(buf), MEM16(name), access, share, disposition, options)
               : 0;
}

/* NTSTATUS NtCreateFile(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
 *                       PLARGE_INTEGER, ULONG FileAttributes, ULONG ShareAccess,
 *                       ULONG CreateDisposition, ULONG CreateOptions); ret 36 */
static void sv_NtCreateFile(void)
{
    uint32_t handle_va = MEM32(esp + 4);
    int t = sv_open_begin(MEM32(esp + 12), MEM32(esp + 8), MEM32(esp + 28), MEM32(esp + 32), MEM32(esp + 36));

    t_coop_kernel_va = MEM32(SV_NTCREATE_THUNK);
    coop_kernel_call();
    if (t)
        saves_guest_open_end(t, eax == 0, handle_va ? MEM32(handle_va) : 0);
}

/* NTSTATUS NtOpenFile(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
 *                     ULONG ShareAccess, ULONG OpenOptions); ret 24 */
static void sv_NtOpenFile(void)
{
    uint32_t handle_va = MEM32(esp + 4);
    int t = sv_open_begin(MEM32(esp + 12), MEM32(esp + 8), MEM32(esp + 20), 1 /* FILE_OPEN */, MEM32(esp + 24));

    t_coop_kernel_va = MEM32(SV_NTOPEN_THUNK);
    coop_kernel_call();
    if (t)
        saves_guest_open_end(t, eax == 0, handle_va ? MEM32(handle_va) : 0);
}

/* NTSTATUS NtClose(HANDLE); ret 4 */
static void sv_NtClose(void)
{
    int t = saves_guest_tracking() ? saves_guest_close_begin(MEM32(esp + 4)) : 0;

    t_coop_kernel_va = MEM32(SV_NTCLOSE_THUNK);
    coop_kernel_call();
    saves_guest_close_end(t);
}

recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    /* Guest kernel calls: cooperative suspension, see above. */
    if (xbox_va >= COOP_KERNEL_VA_LO && xbox_va < COOP_KERNEL_VA_HI) {
        if (xbox_va == MEM32(COOP_NTSUSPEND_THUNK)) return coop_NtSuspendThread;
        if (xbox_va == MEM32(COOP_NTRESUME_THUNK))  return coop_NtResumeThread;
        if (xbox_va == MEM32(VM_NTFREE_THUNK))      return vm_NtFreeVirtualMemory;
        if (xbox_va == MEM32(VM_NTALLOC_THUNK))     return vm_NtAllocateVirtualMemory;
        if (xbox_va == MEM32(CM_ALLOC_THUNK))       return cm_MmAllocateContiguousMemory;
        if (xbox_va == MEM32(CM_ALLOC_EX_THUNK))    return cm_MmAllocateContiguousMemoryEx;
        if (xbox_va == MEM32(CM_FREE_THUNK))        return cm_MmFreeContiguousMemory;
        if (xbox_va == MEM32(CM_QSIZE_THUNK))       return cm_MmQueryAllocationSize;
        if (xbox_va == MEM32(PA_THUNK))             return pa_MmGetPhysicalAddress;
        if (xbox_va == MEM32(NV_QUERY_THUNK))       return nv_ExQueryNonVolatileSetting;
        if (xbox_va == MEM32(POOL_ALLOC_THUNK))     return pool_ExAllocatePool;
        if (xbox_va == MEM32(POOL_ALLOC_TAG_THUNK)) return pool_ExAllocatePoolWithTag;
        if (xbox_va == MEM32(POOL_FREE_THUNK))      return pool_ExFreePool;
        if (xbox_va == MEM32(POOL_QSIZE_THUNK))     return pool_ExQueryPoolBlockSize;
        if (xbox_va == MEM32(HAL_RETURN_THUNK))     return hal_HalReturnToFirmware;
        if (xbox_va == MEM32(SV_NTCREATE_THUNK))    return sv_NtCreateFile;
        if (xbox_va == MEM32(SV_NTOPEN_THUNK))      return sv_NtOpenFile;
        if (xbox_va == MEM32(SV_NTCLOSE_THUNK))     return sv_NtClose;
        if (recomp_lookup_kernel(xbox_va)) {
            t_coop_kernel_va = xbox_va;
            return coop_kernel_call;
        }
    }

    /* XPP input entry points -> xbox_input. See the block comments above
     * their definitions below for the traced ABI. */
    if (xbox_va == 0x001542AB) return sub_001542AB;
    if (xbox_va == 0x0015431A) return sub_0015431A;
    if (xbox_va == 0x0002BCC0) return traced_sub_0002BCC0;

    (void)xbox_va;
    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 */
void recomp_icall_fail_log(uint32_t va)
{
    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu)\n",
            va, (unsigned long long)g_icall_count);

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        int idx = (g_icall_trace_idx - 16 + i) & 15;
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}

/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va)
{
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped %llu time(s) "
                    "(null or wild function pointer, at call #%llu)\n",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count);
    fflush(stderr);
}


/* ── Data the disassembler mistook for code ────────────────── */

/*
 * The tail of the DSOUND section (0x14726F up to the start of XPP) is a data
 * table, not code. tools.disasm's tail_jump_alias heuristic reported two
 * "functions" there: sub_0014726F (50 KB) and sub_0014FF0C (14 KB). Nothing
 * references either address (no call, jump or data xref), and lifting them
 * produced garbage, including `mov dr2, eax`, which does not compile.
 *
 * tools.recomp --exclude-manual reads this file and skips generating their
 * bodies. The dispatch table still names both symbols, so they are defined
 * here as empty functions that should never run.
 */
void sub_0014726F(void) {}
void sub_0014FF0C(void) {}

/* ── D3D interrupt DPC, vblank forced on ───────────────────── */

/*
 * sub_00106E60: the deferred routine of the D3D library's GPU interrupt
 * (KeInitializeDpc(ctx+0x84, 0x106E60, ctx) at 0x1074E4, ctx = 0x10E378). The
 * ISR at 0x106580 queues it. Hand-written from the disassembly (0x106E60 to
 * 0x106F02); the only change is marked VBLANK below.
 *
 *   VOID DeferredRoutine(PKDPC dpc, PVOID ctx, PVOID arg1, PVOID arg2)  ; ret 16
 *
 * It reads PMC_INTR_0 at [[ctx]+0x100] (ctx+0 holds the NV2A base, 0xFD000000)
 * and runs one handler per pending bit, repeating while any handler reports
 * more work. The vblank handler is sub_00106720: it signals ctx+0x1CC and calls
 * the game's callback at [ctx+0x1C8] (0x39910 here, which bumps the frame counter
 * the splash screen waits on).
 *
 * VBLANK: the runtime's tick sets the vblank bit of PMC_INTR_0 in plain memory
 * every 16 ms, but its own NV2A ack thread (xbox_memory_layout.c, NV2A_ACK)
 * clears PMC_INTR_0 in a loop, so by the time this routine runs the bit has
 * nearly always gone and the vblank handler is skipped (16 of 601 in a 25 s run).
 * The ISR is only raised by that tick and only for a vblank, so this routine
 * treats bit 24 as set on its first pass. Later passes read the register as
 * given, as hardware would: the vblank interrupt has been acknowledged by then.
 * Every other branch is unchanged.
 *
 * Bits, from the code: 0x1000 -> sub_00106BF0, 0x100000 -> set [regs+0x9100]=1,
 * 0x1000000 (vblank) -> sub_00106720, 0x100 -> sub_00106CE0, 0x10000 -> re-arm
 * [regs+0x8100] if bit 0 is set. Everything but 0x1000 gates the last four.
 *
 * Registers: ebx/esi/edi/ebp are saved and restored by the original and used here
 * only as C locals; the callees preserve g_ebx/g_esi/g_edi themselves. Arguments
 * to the thiscall handlers go in ecx, with a return address pushed as the
 * lifted callees expect.
 */
void sub_00106E60(void)
{
    uint32_t ctx  = MEM32(esp + 8);          /* the DPC's DeferredContext */
    uint32_t regs = MEM32(ctx);              /* NV2A register base */
    uint32_t more;                           /* edi in the original */
    int first = 1;

    do {
        uint32_t pending = MEM32(regs + 0x100);      /* esi */
        if (first) {
            pending |= 0x1000000u;                   /* VBLANK: see above */
            first = 0;
        }
        more = 0;

        if (pending & 0x1000) {
            ecx = ctx;
            PUSH32(esp, 0x00106E87u); RECOMP_ABI_CALL(0x00106BF0u, sub_00106BF0);
            more = eax;
        }
        if (pending & 0xFFFFEFFFu) {
            if (pending & 0x100000) {
                MEM32(regs + 0x9100) = 1;
                more |= MEM32(regs + 0x9100);
            }
            if (pending & 0x1000000) {
                ecx = ctx;
                PUSH32(esp, 0x00106EB8u); RECOMP_ABI_CALL(0x00106720u, sub_00106720);
                more |= eax;
            }
            if (pending & 0x100) {
                ecx = ctx;
                PUSH32(esp, 0x00106EC9u); RECOMP_ABI_CALL(0x00106CE0u, sub_00106CE0);
                more |= eax;
            }
            if (pending & 0x10000) {
                uint32_t r = MEM32(ctx);
                if (MEM8(r + 0x8100) & 1)
                    MEM32(r + 0x8100) = 1;
            }
        }
    } while (more);

    eax = MEM32(ctx + 0xB4);
    MEM32(ctx + 0xA4) = more;                /* 0 here: the loop only ends on 0 */
    MEM32(regs + 0x140) = eax;               /* PMC_INTR_EN_0 restored */
    esp += 20;                               /* ret 16 */
}

/* ── CRT memcpy/memmove ────────────────────────────────────── */

/*
 * sub_000F5DD0 (0xF5DD0..0xF606A) is the CRT's memmove, which the game also uses
 * as memcpy: `void *memmove(void *dst, const void *src, size_t n)`, cdecl, returns
 * dst. It picks a copy strategy from the low bits of dst and the size and jumps
 * through nine small tables (0xF5E30, 0xF5F18, 0xF5F28, 0xF5FB8, 0xF6060,
 * 0xF60B0 ...) to per-remainder tails that sit in the middle of the function.
 * The lifter turned two of those jumps (0xF5E1D and 0xF5E24, the second indexing
 * the table backwards with a negative ecx) into RECOMP_ITAIL indirect tail calls,
 * and the tails are not function entries, so those copies were skipped: the call
 * failed to resolve 0xF5F30, 0xF606C, 0xF60A7 or 0xF60D8 and returned without
 * copying, silently corrupting whatever was being loaded.
 *
 * The guest address space is one linear host mapping (XBOX_PTR), so the host
 * memmove is correct for every size, alignment and overlap. Register effects
 * match the original: edi and esi are restored, eax = dst, ecx/edx are volatile.
 */
void sub_000F5DD0(void)
{
    uint32_t dst = MEM32(esp + 4);
    uint32_t src = MEM32(esp + 8);
    uint32_t n   = MEM32(esp + 12);

    if (n)
        memmove((void *)XBOX_PTR(dst), (const void *)XBOX_PTR(src), n);
    eax = dst;
    esp += 4;                                /* ret (cdecl: the caller pops the args) */
}

/* ── GetExitCodeThread ─────────────────────────────────────── */

/*
 * sub_000ECB2B (0xECB2B..0xECB77), the XAPI GetExitCodeThread:
 *
 *   BOOL GetExitCodeThread(HANDLE hThread, LPDWORD lpExitCode)  ; ret 8
 *
 * From the disassembly: it resolves hThread through
 * ObReferenceObjectByHandle(hThread, ObThreadObjectType, &Object) -- reusing
 * the argument's own stack slot as the &Object out-parameter, so on return
 * that slot holds the KTHREAD object pointer, not the handle any more. If the
 * status is negative it fails (sets last-error via sub_000EDD61, returns
 * FALSE). Otherwise it reads a "terminated" byte at Object+4: if set,
 * *lpExitCode = *(Object+0x120) (the thread's real exit status); if clear,
 * *lpExitCode = 0x103 (STILL_ACTIVE). Then ObfDereferenceObject(Object) and
 * return TRUE.
 *
 * bridge_ObReferenceObjectByHandle (kernel_bridge.c) always writes Object =
 * NULL, so Object+4 always reads as clear and this always reported
 * STILL_ACTIVE -- confirmed in a diagnostic run (Sep 22 2026 notes, section
 * "start here tomorrow"): every poll during the hang used the same handle
 * token as the one written back for the thread spawned with start-context
 * 0x2BF30, so this is the loop that never ends.
 *
 * Fix: skip the bridge's object layer entirely. Every thread handle this
 * bridge ever hands out is a token over a *real* native HANDLE from
 * CreateThread (see bridge_spawn_thread / s_handle_table in kernel_bridge.c),
 * and PsTerminateSystemThread (ordinal 258) calls the real ExitThread with
 * the game's own exit status for a spawned worker. So the real
 * GetExitCodeThread on that real HANDLE already gives the exact right
 * answer -- STILL_ACTIVE while running, the game's own status once it has
 * called PsTerminateSystemThread -- with no need to reconstruct the KTHREAD
 * object fields by hand.
 *
 * xbox_bridge_resolve_handle (xbox_memory_layout.h) is the new, additive,
 * read-only accessor added for this. A handle that does not resolve (the
 * main thread never gets a real spawned HANDLE; a stray/synthetic token)
 * falls back to the bridge's old answer -- STILL_ACTIVE, success -- so every
 * other call site of this function (sub_0002C020, sub_00087870,
 * sub_000B9100, sub_000EC060) keeps its previous behavior unless the handle
 * is one this fix can actually improve on.
 */
void sub_000ECB2B(void)
{
    uint32_t handle_va     = MEM32(esp + 4);   /* hThread */
    uint32_t exitcode_va   = MEM32(esp + 8);   /* lpExitCode */
    HANDLE   h             = xbox_bridge_resolve_handle(handle_va);
    DWORD    code          = 0x103;            /* STILL_ACTIVE, the old fallback */

    if (h) {
        DWORD real_code;
        if (GetExitCodeThread(h, &real_code))
            code = real_code;
    }

    if (exitcode_va)
        MEM32(exitcode_va) = (uint32_t)code;
    eax = 1;                                   /* TRUE */
    esp += 12;                                 /* ret 8 */
}

/* ── DSOUND's IRQL, made a lock (notes 18.45) ─────────────────── */

/*
 * DSOUND keeps its interrupt-time code apart from everything else by raising
 * IRQL: its guard object (constructor sub_001395F1, destructor sub_00139624;
 * the library's only KfRaiseIrql and KfLowerIrql calls, 67 guarded regions)
 * raises to DISPATCH_LEVEL unless the thread is there already. On the Xbox's
 * one CPU that is exclusion: no DPC and no other thread runs until the guard
 * lowers again, and the ISR only touches what DSOUND leaves safe for it
 * (sub_001399D0 defers an idle while ctx+0x32C counts a list walk).
 *
 * The runtime's IRQL is a per-thread number, its DPCs run on the timer
 * thread, and the APU's ISR now runs on a thread of its own (below). So they
 * ran in the middle of a game thread's guarded code: the ISR turned off a
 * voice a game thread was unlinking, and crashed on the half-unlinked list in
 * sub_0013AAF6 (where 18.42's first voice-off version crashed too).
 *
 * So the outermost raise also takes a recursive host lock, held until the
 * matching lower, and the ISR and DSOUND's DPC (sub_00139F1B) run holding it:
 * DSOUND at DISPATCH_LEVEL runs alone again. A thread holding it counts as
 * holding a lock, so cooperative suspend doesn't park it there. Lock order:
 * g_vw_isr_lock, then this; guarded code only ever try-acquires the former.
 */
static CRITICAL_SECTION g_ds_irql;
static volatile DWORD g_ds_owner;                  /* outermost holder, for the stall report */
static volatile uint32_t g_ds_site;                /* its guest return address */
static volatile ULONGLONG g_ds_since;

static void ds_irql_note(uint32_t site)
{
    coop_thread_t *self;

    if (t_ds_irql++ == 0) {
        g_ds_owner = GetCurrentThreadId();
        g_ds_site = site;
        g_ds_since = GetTickCount64();
        if ((self = coop_self()) != NULL)
            InterlockedExchange(&self->locks, t_cs_n + t_ds_irql);
    }
}

static void ds_irql_enter(uint32_t site)
{
    EnterCriticalSection(&g_ds_irql);
    ds_irql_note(site);
}

/* The interrupt thread never waits for it: a busy lock leaves the interrupt
 * pending for its next look. */
static int ds_irql_try(uint32_t site)
{
    if (!TryEnterCriticalSection(&g_ds_irql))
        return 0;
    ds_irql_note(site);
    return 1;
}

static void ds_irql_leave(void)
{
    coop_thread_t *self;

    if (--t_ds_irql == 0) {
        g_ds_owner = 0;
        if ((self = coop_self()) != NULL)
            InterlockedExchange(&self->locks, t_cs_n);
    }
    LeaveCriticalSection(&g_ds_irql);
}

/* The guards that raised on this thread, innermost last, by the guest address
 * of the guard object (on the stack, so an inner one sits lower). The lock is
 * released by address rather than by the guard's own raised flag, and a guard
 * whose stack frame is gone (below esp) without its destructor is released
 * when that is seen, and logged. */
#define DS_GUARDS 32
static RECOMP_TLS uint32_t t_guard[DS_GUARDS], t_guard_site[DS_GUARDS];
static RECOMP_TLS int t_nguard;
static volatile LONG g_ds_odd;

static void ds_guard_log(const char *what, uint32_t guard, uint32_t site)
{
    if (InterlockedIncrement(&g_ds_odd) <= 10)
        fprintf(stderr, "[DSIRQL] thread %lu: guard at %08X (from guest %08X) %s\n",
                (unsigned long)GetCurrentThreadId(), guard, site, what);
}

static void ds_guard_reap(void)
{
    while (t_nguard > 0 && t_guard[t_nguard - 1] < esp) {
        t_nguard--;
        ds_guard_log("left its frame without its destructor; released", t_guard[t_nguard], t_guard_site[t_nguard]);
        ds_irql_leave();
    }
}

/* sub_001395F1: the guard's constructor (thiscall, the guard; ret). It
 * raised if it recorded 1 at +4. */
extern void sub_001395F1_gen(void);
void sub_001395F1(void)
{
    uint32_t guard = ecx, site = MEM32(esp);

    ds_guard_reap();
    sub_001395F1_gen();
    if (MEM32(guard + 4)) {
        if (t_nguard < DS_GUARDS) {
            t_guard[t_nguard] = guard;
            t_guard_site[t_nguard] = site;
        }
        t_nguard++;
        ds_irql_enter(site);
    }
}

/* sub_00139624: the guard's destructor (thiscall, the guard; ret). */
extern void sub_00139624_gen(void);
void sub_00139624(void)
{
    uint32_t guard = ecx, site = MEM32(esp);
    int raised = MEM32(guard + 4) != 0, i;

    ds_guard_reap();
    for (i = (t_nguard < DS_GUARDS ? t_nguard : DS_GUARDS) - 1; i >= 0 && t_guard[i] != guard; i--)
        ;
    sub_00139624_gen();
    if (i >= 0) {
        if (!raised)
            ds_guard_log("had its raised flag cleared before its destructor; released", guard, t_guard_site[i]);
        for (; i + 1 < t_nguard && i + 1 < DS_GUARDS; i++) {
            t_guard[i] = t_guard[i + 1];
            t_guard_site[i] = t_guard_site[i + 1];
        }
        t_nguard--;
        ds_irql_leave();
    } else if (raised) {
        if (t_nguard > DS_GUARDS) {          /* an untracked overflow entry */
            t_nguard--;
            ds_irql_leave();
        } else {
            ds_guard_log("was destroyed raised, but never recorded", guard, site);
        }
    }
}

/* sub_00139F1B: DSOUND's DPC (stdcall, 4 arguments; ret 16), which runs at
 * DISPATCH_LEVEL and walks the voice lists unguarded. */
extern void sub_00139F1B_gen(void);
void sub_00139F1B(void)
{
    ds_irql_enter(0x00139F1Bu);
    sub_00139F1B_gen();
    ds_irql_leave();
}

/* ── DSOUND voice-off: the APU interrupt it waits for (notes 18.42) ── */

/*
 * A released voice is "stopping" ([voice+0x12] & 0x8000, set by
 * sub_0013B76E) until it is turned off. DSOUND polls it through
 * sub_0013ADB8: from the wait loop sub_0013B682, and from its per-voice
 * dispatchers sub_0013C64D and sub_0013D174 when the game asks for status
 * (level setup waits for every voice and stream to stop). sub_0013ADB8 forces
 * the voice off once 5 s have passed since the stop stamp at +0x80 (that is
 * DSOUND's count at [0x1531A8]): the 3-5 s full stops at voice lines and at
 * every level change (notes 18.40, 18.41).
 *
 * On hardware the voice is turned off from DSOUND's APU interrupt (KINTERRUPT
 * 0x153260, ISR 0x13A0FB) within a frame or so of the release finishing: the
 * voice processor idles the voice, the front end traps SE2FE_IDLE_VOICE
 * (method 0x8000, the voice's handle in FEDECPARAM) and interrupts, and the
 * ISR's sub_00139F42 -> sub_00139EC1 -> sub_001399D0(handle) calls
 * sub_0013AAF6(1), which clears 0x8000.
 *
 * The emulated APU (DSOUND sets FETFORCE1 bit 15, so the trap is on) gets as
 * far as the trap and no further:
 *   - it never raises ISTS for it (the flag it sets for that is never read),
 *     and nothing delivers an interrupt on this vector, so the front end
 *     stays TRAPPED and its frame thread stops running the voice processor;
 *   - its VOICE_OFF doesn't take a voice out of its list. The first voice
 *     turned off stays at the head of the list, linked to itself, so from
 *     then on every frame's walk traps on that voice again and never reaches
 *     the others: a voice released later goes inactive in the model but is
 *     never reported (seen: handle 0x45 inactive, every trap naming 0x44).
 *
 * So whenever a stopping voice is polled, this does what the interrupt would
 * have done by then, and then runs the lifted body as before:
 *   - FECTL trapped on an idle voice that is real (FEDECMETH 0x8000; the
 *     voice DSOUND has on that handle, [context + 0x330 + handle * 4], is
 *     stopping; and the model has idled it): write 0 to ISTS, then call the
 *     ISR with DSOUND's KINTERRUPT and context, the way the runtime calls a
 *     connected ISR for vblank. ISTS is write-one-to-clear, so the write
 *     clears nothing; the model's ISTS write path raises FETINTSTS for the
 *     trap, and GINTSTS if IEN enables it, as the trap should have.
 *   - Trapped on anything else, the stale entry: DSOUND never sees it (a
 *     first version handed it on, and DSOUND turned off the playing voice
 *     that had been given the handle, then crashed in sub_0013AAF6). The
 *     front end resumes the way DSOUND's own handler resumes it,
 *     sub_0013EE94(0) then (1), so the voice processor runs again.
 *   - The polled voice inactive in the model (PAR_STATE ACTIVE_VOICE clear in
 *     its voice block, [0x153310] + handle * 0x80): report its idle as its
 *     trap would, sub_001399D0(handle) with the ISR's context. That checks
 *     the voice itself, as it does for a real trap.
 * The ISR runs for nothing else: FECTL keeps the trap reason after a resume,
 * so it would read the stale method again. One thread at a time (a CPU
 * doesn't take an interrupt twice); a thread that finds it busy just carries
 * on. The 5 s force-off stays as the fallback.
 */
#define VW_KINTERRUPT         0x00153260u      /* DSOUND's KINTERRUPT: routine, context, vector */
#define VW_ISR                0x0013A0FBu
#define VW_RET                0x0013B694u      /* in the wait loop, where the interrupt lands */
#define VW_VOICE_BLOCKS       0x00153310u      /* [ ] = the APU's voice blocks, 0x80 bytes each */
#define VW_FORCED_OFF         0x001531A8u      /* DSOUND's count of 5 s force-offs */
#define APU_ISTS_VA           0xFE801000u
#define APU_IEN_VA            0xFE801004u
#define APU_FECTL_VA          0xFE801100u
#define APU_FEDECMETH_VA      0xFE801300u
#define APU_FEDECPARAM_VA     0xFE801304u
#define APU_FETFORCE1_VA      0xFE801504u
#define VW_IDLE_VOICE         0x8000u          /* SE2FE_IDLE_VOICE */
#define APU_ISTS_GINTSTS      0x1u
#define APU_FECTL_TRAPPED     0xE0u
#define APU_PAR_STATE         0x54u
#define APU_PAR_STATE_ACTIVE  (1u << 21)

extern uint32_t xbox_GetConnectedInterrupt(uint32_t vector);

static SRWLOCK g_vw_isr_lock = SRWLOCK_INIT;
static volatile LONG g_vw_isr_runs, g_vw_idles, g_vw_freed, g_vw_stale;

/* DSOUND's KINTERRUPT, if it's connected; 0 if not (yet). */
static uint32_t vw_kinterrupt(void)
{
    uint32_t kint = VW_KINTERRUPT;

    if (MEM32(kint) != VW_ISR || xbox_GetConnectedInterrupt(MEM32(kint + 8)) != kint)
        return 0;
    return kint;
}

/* Whether the model has idled the voice with this hardware handle. */
static int vw_model_idle(uint32_t handle)
{
    uint32_t blocks = MEM32(VW_VOICE_BLOCKS);

    return blocks && handle < 0x100 && !(MEM32(blocks + handle * 0x80 + APU_PAR_STATE) & APU_PAR_STATE_ACTIVE);
}

/* Whether DSOUND still has the voice on one of its lists (its entry at +0x4C:
 * next, prev), i.e. whether an idle can still be reported for it. On the
 * hardware an idle arrives once, as a trap. Here it can come twice, from this
 * poll's direct report and from the APU interrupt (18.45), and the second
 * sub_001399D0 unlinked an unlinked voice: its own check (next == the entry)
 * misses the null prev that sub_0013AAF6 leaves, and sub_0013AAF6 crashed
 * reading through it. */
static int vw_voice_linked(uint32_t voice)
{
    uint32_t next = MEM32(voice + 0x4C), prev = MEM32(voice + 0x50);

    return next && prev && next != voice + 0x4C;
}

/* A pending front-end trap, handled; then the voice's idle, if the model has
 * idled it. Caller holds g_vw_isr_lock. */
static void vw_apu_interrupt(uint32_t kint, uint32_t voice)
{
    uint32_t ctx = MEM32(kint + 4), handle, trapped_voice;

    if ((MEM32(APU_FECTL_VA) & APU_FECTL_TRAPPED) == APU_FECTL_TRAPPED) {
        handle = MEM32(APU_FEDECPARAM_VA) & 0xFFFF;
        trapped_voice = MEM32(APU_FEDECMETH_VA) == VW_IDLE_VOICE && handle < 0x100
                        ? MEM32(ctx + 0x330 + handle * 4) : 0;
        if (trapped_voice && (MEM16(trapped_voice + 0x12) & 0x8000) && vw_model_idle(handle)
                && vw_voice_linked(trapped_voice)) {
            MEM32(APU_ISTS_VA) = 0;        /* raises the trap's status */
            if (MEM32(APU_ISTS_VA) & APU_ISTS_GINTSTS) {
                PUSH32(esp, ctx);
                PUSH32(esp, kint);
                PUSH32(esp, VW_RET);
                RECOMP_ABI_CALL(VW_ISR, sub_0013A0FB);     /* ret 8 */
                InterlockedIncrement(&g_vw_isr_runs);
            }
        } else {
            /* A stale trap: the model's list still holds a voice turned off
             * earlier (whose handle may now be another, playing voice's).
             * DSOUND never sees it; the front end resumes as DSOUND's own
             * handler resumes it, sub_0013EE94(0) then (1). */
            ecx = ctx + 8;
            PUSH32(esp, 0);
            PUSH32(esp, VW_RET);
            RECOMP_ABI_CALL(0x0013EE94u, sub_0013EE94);     /* ret 4 */
            ecx = ctx + 8;
            PUSH32(esp, 1);
            PUSH32(esp, VW_RET);
            RECOMP_ABI_CALL(0x0013EE94u, sub_0013EE94);
            InterlockedIncrement(&g_vw_stale);
        }
    }
    if (!(MEM16(voice + 0x12) & 0x8000))
        return;
    handle = MEM16(voice + 0xA + 2 * MEM8(voice + 0x64));
    if (vw_model_idle(handle) && vw_voice_linked(voice)) {
        ecx = ctx;
        PUSH32(esp, handle);
        PUSH32(esp, VW_RET);
        RECOMP_ABI_CALL(0x001399D0u, sub_001399D0); /* ret 4 */
        InterlockedIncrement(&g_vw_idles);
    }
}

/* sub_0013ADB8 (thiscall, the voice; ret): see above. */
extern void sub_0013ADB8_gen(void);
void sub_0013ADB8(void)
{
    static LONG last_forced = -1;
    uint32_t voice = ecx, kint;
    LONG freed, forced;

    if ((MEM16(voice + 0x12) & 0x8001) == 0x8001 && (kint = vw_kinterrupt()) != 0
            && TryAcquireSRWLockExclusive(&g_vw_isr_lock)) {
        if (g_vw_idles == 0 && g_vw_isr_runs == 0)
            fprintf(stderr, "[VOICEOFF] first stopping voice %08X: FECTL %08X ISTS %08X IEN %08X FETFORCE1 %08X\n",
                    voice, MEM32(APU_FECTL_VA), MEM32(APU_ISTS_VA), MEM32(APU_IEN_VA), MEM32(APU_FETFORCE1_VA));
        ds_irql_enter(0x0013ADB8u);        /* an interrupt: DSOUND's IRQL lock (18.45) */
        vw_apu_interrupt(kint, voice);
        ds_irql_leave();
        ReleaseSRWLockExclusive(&g_vw_isr_lock);
        if (!(MEM16(voice + 0x12) & 0x8000)) {
            freed = InterlockedIncrement(&g_vw_freed);
            if (freed <= 10 || freed % 50 == 0)
                fprintf(stderr, "[VOICEOFF] voice %08X off at its poll; %ld so far (ISR runs %ld, idles reported %ld, stale traps %ld),"
                                " DSOUND 5 s force-offs %u\n",
                        voice, (long)freed, (long)g_vw_isr_runs, (long)g_vw_idles, (long)g_vw_stale,
                        MEM32(VW_FORCED_OFF));
        }
    }
    ecx = voice;
    sub_0013ADB8_gen();
    forced = (LONG)MEM32(VW_FORCED_OFF);
    if (forced != last_forced && forced > 0) {
        last_forced = forced;
        fprintf(stderr, "[VOICEOFF] DSOUND forced a voice off after 5 s (%ld so far)\n", (long)forced);
    }
}

/* ── The APU's interrupt, delivered (notes 18.45) ─────────────── */

/*
 * The emulated APU records its interrupts and never raises them: a voice
 * notifier (stream packet done, voice off) sets FEVINTSTS/FENINTSTS in ISTS, a
 * trapped front-end method (SE2FE_IDLE_VOICE, which DSOUND asks for) sets
 * FECTL to TRAPPED, and both set a set_irq flag nothing reads. xemu's frame
 * thread runs update_irq after every such frame and the PCI line reaches the
 * CPU at once. Here nothing does, so:
 *   - streams stopped a few seconds in: DSOUND hands the next packet to a
 *     stream from its DPC, when the ISR sees the packet-done notifier, and
 *     the ISR never ran (the [VOICEOFF] lines' "ISR runs 0");
 *   - a trap halts the whole voice processor (the frame thread skips
 *     se_frame while FECTL is trapped) until 18.42's voice-off poll happens
 *     to clear it, which only a released voice's poll does. (Since
 *     xboxrecomp 1551271, notes 18.48, the VP renders through a trap and
 *     only a halted front end stops it: each trap was a gap of silence as
 *     long as this interrupt took.)
 *
 * DSOUND's ISR (0x13A0FB -> 0x13A05F; KINTERRUPT 0x153260, context
 * [0x153264]) reads ISTS and acts only with GINTSTS set: it writes the status
 * back (write-one-to-clear), ORs it into ctx+0x760, runs the trap handler
 * sub_00139F42 for FETINTSTS (bit 4: an idle-voice trap goes to
 * sub_001399D0(handle), then the front end is resumed), and queues its DPC
 * (ctx+0x770, routine 0x139F1B) for FEVINTSTS (bit 6). The DPC services every
 * voice and stream's notifiers (sub_001398CD): packets complete, the next
 * are queued.
 *
 * This thread is the interrupt line. Every millisecond it looks at ISTS, IEN
 * and FECTL; when the APU has something pending and IEN enables it, it writes
 * 0 to ISTS (write-one-to-clear, so nothing is cleared, but the model's write
 * path runs update_irq: FETINTSTS for a trap, GINTSTS if IEN has it) and calls
 * the ISR with DSOUND's KINTERRUPT and context, as the runtime calls a
 * connected ISR for vblank. The DPC goes to the runtime's queue and the timer
 * thread runs it.
 *
 * A trap is checked first, as the voice-off poll checks it: an idle-voice trap
 * whose handle the model still has playing, or that no DSOUND voice holds, is
 * a stale list entry (18.42): the ISR doesn't see it, and the front end is
 * resumed the way DSOUND's own handler resumes it, sub_0013EE94(0) then (1).
 * A real one, released or not (a voice that reached its end on its own), goes
 * to the ISR like any other. (The model keeps running PIO methods while it is
 * trapped, which the hardware queues, so a game thread's next method can
 * overwrite FEDECMETH before this looks: such a trap is resumed as stale, and
 * the voice processor traps on the idle voice again next frame.)
 *
 * The thread has a guest stack from the heap (xbox_AllocThreadStack, not a
 * worker slice: those sit inside the game's static data, 0x780000+) and a TIB
 * of its own. It shares g_vw_isr_lock with the voice-off poll, so one ISR runs
 * at a time, and runs the ISR holding DSOUND's IRQL lock (above), as an
 * interrupt on the one CPU would. It only ever tries both locks: waiting for
 * DSOUND's while holding g_vw_isr_lock would leave a game thread that holds
 * DSOUND's and polls a stopping voice (which only tries g_vw_isr_lock)
 * polling forever. A busy lock leaves the interrupt pending for the next
 * millisecond. It does nothing until DSOUND has connected its interrupt.
 */
#define AI_POLL_MS            1
#define APU_FECTL_MODE        0xE0u

extern uint32_t xbox_AllocThreadStack(void);
extern uint32_t xbox_AllocThreadTib(void);
extern RECOMP_TLS uint32_t g_fs_base;

static volatile LONG g_ai_isr_runs, g_ai_claimed, g_ai_traps, g_ai_stale, g_ai_cleared;

/*
 * A trap's status left over once it is handled (Sea Shell's pause crash). The
 * model sets FETINTSTS at every ISTS write while the front end is trapped
 * (update_irq), and DSOUND's ISR writes the status back to clear it before
 * its trap handler resumes the front end: so the write sets it again, and it
 * is still set, with GINTSTS, after the trap is handled. FECTL keeps the trap
 * reason and FEDECMETH/FEDECPARAM the trapped method, so the next look ran
 * the ISR again on them: sub_00139F42 reported the same idle voice a second
 * time, sub_001399D0(handle). Within a millisecond the voice is usually
 * still as the first report left it (its list entry pointing at itself) and
 * DSOUND skips it, but the pause screen stops and frees every voice at once:
 * the second report found a freed voice (next and prev 0), and
 * sub_0013AAF6's unlink read through the null prev (fault at 0xFFFFFFBE,
 * prev - 0x4C + 0xA). In xemu the line stays asserted and the ISR runs again
 * at once, before any thread can free the voice. A trap interrupts once, so
 * the status of a trap no longer pending is cleared (write-one-to-clear; the
 * model sets it again only while trapped) instead of being delivered.
 */
#define APU_ISTS_FETINTSTS    0x10u

/* One pending interrupt, if any. Caller holds g_vw_isr_lock. */
static void ai_service(uint32_t kint)
{
    uint32_t ctx = MEM32(kint + 4), handle, trapped_voice;
    uint32_t ists = MEM32(APU_ISTS_VA), ien = MEM32(APU_IEN_VA), fectl = MEM32(APU_FECTL_VA);
    int trapped = (fectl & APU_FECTL_MODE) == APU_FECTL_TRAPPED;

    if (!trapped && (fectl & APU_FECTL_MODE) == 0 && (ists & APU_ISTS_FETINTSTS)) {
        MEM32(APU_ISTS_VA) = APU_ISTS_FETINTSTS;
        ists = MEM32(APU_ISTS_VA);
        InterlockedIncrement(&g_ai_cleared);
    }
    if (!(ien & APU_ISTS_GINTSTS) || (!trapped && !(ists & ien & ~APU_ISTS_GINTSTS)))
        return;
    if (trapped) {
        handle = MEM32(APU_FEDECPARAM_VA) & 0xFFFF;
        trapped_voice = MEM32(APU_FEDECMETH_VA) == VW_IDLE_VOICE && handle < 0x100
                        ? MEM32(ctx + 0x330 + handle * 4) : 0;
        if (!trapped_voice || !vw_model_idle(handle) || !vw_voice_linked(trapped_voice)) {
            ecx = ctx + 8;
            PUSH32(esp, 0);
            PUSH32(esp, VW_RET);
            RECOMP_ABI_CALL(0x0013EE94u, sub_0013EE94);     /* ret 4 */
            ecx = ctx + 8;
            PUSH32(esp, 1);
            PUSH32(esp, VW_RET);
            RECOMP_ABI_CALL(0x0013EE94u, sub_0013EE94);
            if (InterlockedIncrement(&g_ai_stale) <= 5)
                fprintf(stderr, "[APU-IRQ] stale trap: method %08X handle %02X, DSOUND voice %08X; front end resumed\n",
                        MEM32(APU_FEDECMETH_VA), handle, trapped_voice);
            return;
        }
        InterlockedIncrement(&g_ai_traps);
    }
    MEM32(APU_ISTS_VA) = 0;                /* update_irq: FETINTSTS for a trap, GINTSTS */
    if (!(MEM32(APU_ISTS_VA) & APU_ISTS_GINTSTS))
        return;
    PUSH32(esp, ctx);
    PUSH32(esp, kint);
    PUSH32(esp, VW_RET);
    RECOMP_ABI_CALL(VW_ISR, sub_0013A0FB);          /* ret 8 */
    if (InterlockedIncrement(&g_ai_isr_runs) <= 3)
        fprintf(stderr, "[APU-IRQ] ISR run %ld: ISTS %08X IEN %08X FECTL %08X -> %s\n",
                (long)g_ai_isr_runs, ists, ien, fectl, (eax & 1) ? "claimed" : "declined");
    if (eax & 1)
        InterlockedIncrement(&g_ai_claimed);
}

static DWORD WINAPI ai_thread(LPVOID unused)
{
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    LARGE_INTEGER due;
    ULONGLONG next_log = 0;
    uint32_t kint;
    int stall_told = 0;

    (void)unused;
    if (!timer)
        timer = CreateWaitableTimerW(NULL, FALSE, NULL);
    esp = xbox_AllocThreadStack();
    g_fs_base = xbox_AllocThreadTib();
    if (!timer || !esp || !g_fs_base) {
        fprintf(stderr, "[APU-IRQ] no timer, guest stack or TIB; the APU interrupt is not delivered\n");
        return 0;
    }
    due.QuadPart = -10000LL * AI_POLL_MS;
    for (;;) {
        SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE);
        WaitForSingleObject(timer, INFINITE);
        if ((kint = vw_kinterrupt()) == 0)
            continue;
        if (GetTickCount64() >= next_log) {
            if (next_log)
                fprintf(stderr, "[APU-IRQ] ISR runs %ld (claimed %ld), idle-voice traps %ld, stale traps resumed %ld,"
                                " handled traps' status cleared %ld\n",
                        (long)g_ai_isr_runs, (long)g_ai_claimed, (long)g_ai_traps, (long)g_ai_stale,
                        (long)g_ai_cleared);
            next_log = GetTickCount64() + 30000;
        }
        {
            DWORD owner = g_ds_owner;
            ULONGLONG held = owner ? GetTickCount64() - g_ds_since : 0;
            if (held > 2000 && held < 3000 && !stall_told++)
                fprintf(stderr, "[APU-IRQ] DSOUND's IRQL lock held %llu ms by thread %lu (from guest %08X)\n",
                        held, (unsigned long)owner, g_ds_site);
            if (!owner)
                stall_told = 0;
        }
        if ((MEM32(APU_FECTL_VA) & APU_FECTL_MODE) != APU_FECTL_TRAPPED &&
            !(MEM32(APU_ISTS_VA) & ~APU_ISTS_GINTSTS))
            continue;                      /* nothing pending: no locks */
        if (!TryAcquireSRWLockExclusive(&g_vw_isr_lock))
            continue;                      /* the voice-off poll is interrupting */
        if (ds_irql_try(VW_ISR)) {
            ai_service(kint);
            ds_irql_leave();
        }
        ReleaseSRWLockExclusive(&g_vw_isr_lock);
    }
}

/* RECOMP_APU_IRQ=0 leaves the interrupt undelivered, as before 18.45 (a
 * switch for comparing runs, 18.46): streams then stop a few seconds in. */
void twoc_apu_irq_init(void)
{
    const char *v = getenv("RECOMP_APU_IRQ");

    InitializeCriticalSection(&g_ds_irql);
    if (v && !strcmp(v, "0")) {
        fprintf(stderr, "[APU-IRQ] RECOMP_APU_IRQ=0: the APU interrupt is not delivered\n");
        return;
    }
    CloseHandle(CreateThread(NULL, 0, ai_thread, NULL, 0, NULL));
}

/* ── DSOUND 3D-listener maintenance: null-interface guard ─────────────── */

/*
 * Update (notes 18.25): the pointer was never a listener. 0x427A58 is the
 * sound manager and its first dword is the IDirectSound object, written by
 * DirectSoundCreate (sub_00137A06) through the manager's `this`. That is why
 * no literal-address write exists. It stayed null because DirectSoundCreate
 * returned DSERR_NODRIVER with no AC'97 codec reported. run_twoc.ps1 now sets
 * RECOMP_AC97_READY, the pointer is real, and this guard is inert. It stays
 * as a safety net for runs without that variable.
 *
 * sub_000B9890 (0xB9890..0xB9899, 9 bytes, one caller: sub_000EC470):
 *
 *   mov eax, [ecx]        ; ecx = 0x427A58 (a static "3D listener" cache --
 *                          ; see notes 18.10-18.12); eax = the cached
 *                          ; IDirectSound3DListener/Buffer-adjacent interface
 *                          ; pointer this whole subsystem is built around
 *   push eax
 *   call sub_00136D09
 *   ret
 *
 * sub_00136D09 adjusts eax by -8 (null-safe) into a C++ object pointer
 * ("manager") and calls sub_001367B9, which reads BigObject = *(manager+0xc)
 * and eventually calls sub_00139A36 -> sub_001388D6 -> sub_0013A35E, which
 * writes a 40-byte block through *(BigObject+0x58) unconditionally once its
 * own internal guard bit passes.
 *
 * Traced end to end (18.10-18.12): *(0x427A58) is still 0 by the time this
 * runs. No code anywhere in the binary (all 7 disassembled sections
 * checked, not just game code) writes it via a literal address, and
 * sub_0013A107 -- the DSOUND/APU hardware init that connects the two
 * interrupt vectors this library uses -- does not wait or block on
 * anything; it connects, runs a short synchronous loop and returns, so it
 * does not look like it is what is stuck. What real DSOUND call is
 * supposed to populate 0x427A58 (a QueryInterface off a primary buffer
 * created with DSBCAPS_CTRL3D, going by the shape of it) was not found in a
 * time-boxed search of this pass.
 *
 * With eax == 0, the null-safe -8 adjuster still produces manager == 0 (0
 * - 8, masked to 0 by the same idiom that also zeroes it for a genuinely
 * null interface), so BigObject ends up reading near guest address 0, and
 * *(BigObject+0x58) becomes MEM32(0x58) -- whatever unrelated bytes happen
 * to sit at that low, but still mapped, address. That fixed value is what
 * the whole chain was writing 40 bytes of a rotation matrix through.
 *
 * Fix (fallback -- see 18.13 in the notes for why the preferred fix, the
 * real setup call or an interrupt pump, was not written instead): guard
 * here, at the single, narrowest entry point into this whole chain. If the
 * interface pointer is still null, treat this call as "no 3D listener yet,
 * nothing to update" and return immediately -- a no-op, not a crash. Every
 * other line above sub_00136D09 down to sub_0013A35E is untouched; this is
 * the only place in the chain that changes.
 *
 * Known limitation this introduces: 3D audio positioning silently does not
 * update for as long as 0x427A58 stays null. If the real cause turns out to
 * be transient (the listener gets created a few seconds into a real level,
 * say), this guard stops being relevant on its own once that happens --
 * this call is reached every frame, so nothing needs to re-enable it. If
 * the interface is never created at all in this recomp, 3D positional
 * audio simply never updates; the sound itself (music, 2D/non-positional
 * SFX) is unaffected, since none of that goes through this chain (see
 * section 14.4: DSOUND's other ~35 game-side entry points, e.g. the
 * 0xB8E00-0xB9C00 wrapper cluster this sits inside, are untouched by this
 * guard).
 */
void sub_000B9890(void)
{
    uint32_t interfacePtr = MEM32(ecx);

    if (interfacePtr) {
        PUSH32(esp, interfacePtr);
        PUSH32(esp, 0x000B9898u); RECOMP_ABI_CALL(0x00136D09u, sub_00136D09);
    }
    esp += 4;                                  /* this function's own bare ret */
}

/* ── DSOUND: GP DSP command handshake ───────────────────────────────── */

/*
 * sub_001391DE (0x1391DE..0x139353, one caller: sub_0013944B, reached from
 * DirectSoundDownloadEffectsImage sub_00136605 during the sound manager's
 * init at 0xB9A3B). It relocates the effects image (sfx\dsstdfx.bin) into
 * the GP DSP's scratch block, writes command 3 to scratch+0x810, then spins
 * on that word until the DSP clears it:
 *
 *   0x139343  mov [ebx], eax          ; eax = 3
 *   0x139345  cmp dword [ebx], 0
 *   0x139348  jne 0x139345
 *
 * The emulated APU has no DSP. It is stubbed to passthrough and ignores the
 * GP/EP regions entirely (apu_core.c). So nothing clears the word, and with
 * RECOMP_AC97_READY set audio init hung here forever (notes 18.25).
 *
 * Fix: the lifted function unchanged, except that the spin becomes the
 * DSP's answer: clear the word and go on. The effects image is not run (no
 * reverb or other GP effects). Voices still mix through the APU's
 * passthrough. It is not the only DSP wait in DSOUND: sub_0013937A below
 * waits on the same word before posting command 2 (notes 18.30).
 */
void sub_001391DE(void)
{
    uint32_t ebp = 0;
    ebp = g_ebp;  /* prologue saves caller's frame */
    int _flags = 0; /* fallback flag var */
    uint32_t _fa = 0, _fb = 0;
    int32_t _fas = 0, _fbs = 0;
    (void)_fa; (void)_fb; (void)_fas; (void)_fbs;

loc_001391DE: ;
    PUSH32(esp, ebp);
    ebp = esp;
    g_ebp = ebp; /* publish frame for frameless callees */
    g_seh_ebp = ebp;
    _fb = (uint32_t)(0x1C) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* sub source, before the write */
    esp = esp - 0x1C;
    _fa = (uint32_t)(esp) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* sub result */
    PUSH32(esp, ebx);
    PUSH32(esp, esi);
    PUSH32(esp, edi);
    edi = ecx;
    eax = MEM32(edi + 8);
    eax = MEM32(eax + 0x10);
    ebx = MEM32(eax);
    eax = MEM32(ebx + 0x80C);
    _fb = (uint32_t)(MEM32(ebx + 0x804)) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    eax = eax + MEM32(ebx + 0x804);
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */
    ecx = MEM32(edi + 0x20);
    esi = ebx + eax * 4 + 0x818;
    eax = MEM32(esi);
    eax--;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fb = (_fa == 0x7FFFFFFFu); /* dec result/SF/OF; CF unchanged */
    eax = eax << 5;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* shift result */
    _fb = (uint32_t)(0x28) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    eax = eax + 0x28;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */
    _fa = (uint32_t)(ecx) & 0xFFFFFFFFu; _fb = (uint32_t)(ecx) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* test ecx, ecx (32-bit) */
    MEM32(ebp + -20) = edi;
    MEM32(ebp + -16) = eax;
    if (TEST_Z(_fa, _fb)) goto loc_00139227; /* je: equal / zero */

loc_0013921A: ;
    PUSH32(esp, ecx);
    g_ebp = ebp; /* frame stays current across calls */
    g_seh_ebp = ebp;
    PUSH32(esp, 0x00139220u); RECOMP_ABI_CALL(0x00137C29u, sub_00137C29); /* call 0x00137C29 */

loc_00139220: ;
    MEM32(edi + 0x20) = MEM32(edi + 0x20) & 0;
    _fa = (uint32_t)(MEM32(edi + 0x20)) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* and result */
    eax = MEM32(ebp + -16);

loc_00139227: ;
    PUSH32(esp, 1);
    PUSH32(esp, eax);
    PUSH32(esp, 0x626F5344);
    g_ebp = ebp; /* frame stays current across calls */
    g_seh_ebp = ebp;
    PUSH32(esp, 0x00139234u); RECOMP_ABI_CALL(0x00137BC1u, sub_00137BC1); /* call 0x00137BC1 */

loc_00139234: ;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(eax) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* test eax, eax (32-bit) */
    MEM32(edi + 0x20) = eax;
    if (TEST_NZ(_fa, _fb)) goto loc_00139245; /* jne: not equal / not zero */

loc_0013923B: ;
    eax = 0x8007000Eu;
    goto loc_0013934C;

loc_00139245: ;
    PUSH32(esp, MEM32(esi + 4));
    ecx = MEM32(edi + 8);
    g_ebp = ebp; /* frame stays current across calls */
    g_seh_ebp = ebp;
    PUSH32(esp, 0x00139250u); RECOMP_ABI_CALL(0x0013DD2Eu, sub_0013DD2E); /* call 0x0013DD2E */

loc_00139250: ;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(eax) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* test eax, eax (32-bit) */
    if (TEST_S(_fas, _fbs)) goto loc_0013934C; /* jl: less (signed <) */

loc_00139258: ;
    eax = ebx + 0x818;
    MEM32(ebp + -4) = eax;
    eax = MEM32(ebp + -16);
    _fb = (uint32_t)(esi) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    eax = eax + esi;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */
    MEM32(ebp + -8) = eax;
    eax = ebp + -28;
    PUSH32(esp, eax);
    g_ebp = ebp; /* frame stays current across calls */
    g_seh_ebp = ebp;
    PUSH32(esp, 0x00139272u); RECOMP_ABI_CALL(0x0013DE7Fu, sub_0013DE7F); /* call 0x0013DE7F */

loc_00139272: ;
    eax = MEM32(esi);
    PUSH32(esp, 0);
    PUSH32(esp, MEM32(ebp + -8));
    eax = eax << 3;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* shift result */
    PUSH32(esp, eax);
    PUSH32(esp, MEM32(ebp + -8));
    eax = ebp + -28;
    PUSH32(esp, eax);
    g_ebp = ebp; /* frame stays current across calls */
    g_seh_ebp = ebp;
    PUSH32(esp, 0x00139289u); RECOMP_ABI_CALL(0x0013DEACu, sub_0013DEAC); /* call 0x0013DEAC */

loc_00139289: ;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(eax) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* test eax, eax (32-bit) */
    if (CMP_LE(_fas & _fbs, 0)) goto loc_00139299; /* jle: less or equal (signed <=) */

loc_0013928D: ;
    eax = eax & 0xFFFF;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* and result */
    eax = eax | 0x80070000u;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* or result */
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(eax) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* test eax, eax (32-bit) */

loc_00139299: ;
    if (TEST_S(_fas, _fbs)) goto loc_0013934C; /* jl: less (signed <) */

loc_0013929F: ;
    MEM32(ebp + -12) = MEM32(ebp + -12) & 0;
    _fa = (uint32_t)(MEM32(ebp + -12)) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* and result */
    _fa = (uint32_t)(MEM32(esi)) & 0xFFFFFFFFu; _fb = (uint32_t)(0) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp MEM32(esi), 0 (32-bit) */
    if (CMP_BE(_fa, _fb)) goto loc_00139327; /* jbe: below or equal (unsigned <=) */

loc_001392A8: ;
    eax = MEM32(ebp + -8);
    MEM32(ebp + -8) = eax;
    edi = esi + 8;

loc_001392B1: ;
    PUSH32(esp, 0);
    PUSH32(esp, MEM32(ebp + -4));
    PUSH32(esp, MEM32(edi + 4));
    PUSH32(esp, MEM32(ebp + -4));
    PUSH32(esp, MEM32(ebp + -8));
    g_ebp = ebp; /* frame stays current across calls */
    g_seh_ebp = ebp;
    PUSH32(esp, 0x001392C4u); RECOMP_ABI_CALL(0x0013DEACu, sub_0013DEAC); /* call 0x0013DEAC */

loc_001392C4: ;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(eax) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* test eax, eax (32-bit) */
    if (CMP_LE(_fas & _fbs, 0)) goto loc_001392D4; /* jle: less or equal (signed <=) */

loc_001392C8: ;
    eax = eax & 0xFFFF;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* and result */
    eax = eax | 0x80070000u;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* or result */
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(eax) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* test eax, eax (32-bit) */

loc_001392D4: ;
    if (TEST_S(_fas, _fbs)) goto loc_0013934C; /* jl: less (signed <) */

loc_001392D6: ;
    eax = MEM32(edi + 4);
    _fb = (uint32_t)(eax) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    MEM32(ebp + -4) = MEM32(ebp + -4) + eax;
    _fa = (uint32_t)(MEM32(ebp + -4)) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */
    eax = MEM32(ebp + -20);
    ecx = MEM32(eax + 0xC);
    ecx = MEM32(ecx + 4);
    _fb = (uint32_t)(0xFE836000u) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    MEM32(edi + 0x10) = MEM32(edi + 0x10) + 0xFE836000u;
    _fa = (uint32_t)(MEM32(edi + 0x10)) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */
    _fb = (uint32_t)(8) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    MEM32(ebp + -8) = MEM32(ebp + -8) + 8;
    _fa = (uint32_t)(MEM32(ebp + -8)) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */
    _fb = (uint32_t)(0x17C6818) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* sub source, before the write */
    ecx = ecx - 0x17C6818;
    _fa = (uint32_t)(ecx) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* sub result */
    _fb = (uint32_t)(ecx) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    MEM32(edi) = MEM32(edi) + ecx;
    _fa = (uint32_t)(MEM32(edi)) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */
    ecx = 0xFFA0BE7Au;
    _fb = (uint32_t)(MEM32(ebx + 0x804)) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* sub source, before the write */
    ecx = ecx - MEM32(ebx + 0x804);
    _fa = (uint32_t)(ecx) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* sub result */
    _fb = (uint32_t)(0x20) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    edi = edi + 0x20;
    _fa = (uint32_t)(edi) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */
    ecx = ecx << 2;
    _fa = (uint32_t)(ecx) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* shift result */
    _fb = (uint32_t)(ecx) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    MEM32(edi + -24) = MEM32(edi + -24) + ecx;
    _fa = (uint32_t)(MEM32(edi + -24)) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */
    eax = MEM32(eax + 8);
    eax = MEM32(eax + 0x14);
    _fb = (uint32_t)(0xC000) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* sub source, before the write */
    eax = eax - 0xC000;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* sub result */
    _fb = (uint32_t)(eax) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    MEM32(edi + -8) = MEM32(edi + -8) + eax;
    _fa = (uint32_t)(MEM32(edi + -8)) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */
    MEM32(ebp + -12) = MEM32(ebp + -12) + 1;
    _fa = (uint32_t)(MEM32(ebp + -12)) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fb = (_fa == 0x80000000u); /* inc result/SF/OF; CF unchanged */
    eax = MEM32(ebp + -12);
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(MEM32(esi)) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp eax, MEM32(esi) (32-bit) */
    if (CMP_B(_fa, _fb)) goto loc_001392B1; /* jb: below (unsigned <) */

loc_00139324: ;
    edi = MEM32(ebp + -20);

loc_00139327: ;
    ecx = MEM32(ebp + -16);
    edi = MEM32(edi + 0x20);
    eax = ecx;
    ecx = ecx >> 2;
    _fa = (uint32_t)(ecx) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* shift result */
    if (!g_df) { uint8_t *_d = (uint8_t*)XBOX_PTR(edi), *_s = (uint8_t*)XBOX_PTR(esi); uint32_t _n = ecx * 4;
      if (_d + _n <= _s || _s + _n <= _d) memcpy(_d, _s, _n);
      else { uint32_t _i; for (_i = 0; _i < ecx; _i++) MEM32(edi + _i*4) = MEM32(esi + _i*4); }
      esi += ecx * 4; edi += ecx * 4; }
    else { uint32_t _i; for (_i = 0; _i < ecx; _i++) MEM32(edi - _i*4) = MEM32(esi - _i*4); esi -= ecx * 4; edi -= ecx * 4; }
    ecx = 0; /* rep movsd */
    ecx = eax;
    PUSH32(esp, 3);
    POP32(esp, eax);
    ecx = ecx & eax;
    _fa = (uint32_t)(ecx) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* and result */
    _fb = (uint32_t)(0x810) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    ebx = ebx + 0x810;
    _fa = (uint32_t)(ebx) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */
    if (!g_df) { uint8_t *_d = (uint8_t*)XBOX_PTR(edi), *_s = (uint8_t*)XBOX_PTR(esi); uint32_t _n = ecx;
      if (_d + _n <= _s || _s + _n <= _d) memcpy(_d, _s, _n);
      else { uint32_t _i; for (_i = 0; _i < _n; _i++) _d[_i] = _s[_i]; }
      esi += ecx; edi += ecx; }
    else { uint32_t _i; for (_i = 0; _i < ecx; _i++) MEM8(edi - _i) = MEM8(esi - _i); esi -= ecx; edi -= ecx; }
    ecx = 0; /* rep movsb */
    MEM32(ebx) = eax;

    /* 0x139345: `cmp dword [ebx], 0 / jne 0x139345` -- spin until the GP DSP
     * clears the command word just written. Nothing runs the DSP here, so
     * give the answer it would: command taken, word cleared. */
    fprintf(stderr, "[DSP] GP command %u at 0x%08X acknowledged"
            " (no DSP; effects image not run)\n", MEM32(ebx), ebx);
    MEM32(ebx) = 0;

loc_0013934A: ;
    eax = 0; /* xor self */
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* xor result */

loc_0013934C: ;
    POP32(esp, edi);
    POP32(esp, esi);
    POP32(esp, ebx);
    esp = ebp;
    POP32(esp, ebp); /* leave */
    esp += 8; return; /* ret 4 */

}

/*
 * sub_0013937A (0x13937A..0x1393DF, thiscall, ret 8): post a GP DSP command
 * 2 through the same scratch command word, scratch+0x810. One caller,
 * sub_00139A36 at 0x139AE2, with both args 0, so the size and offset come
 * from the pending update at this+0x18/this+0x1C. It waits for the previous
 * command to be taken first:
 *
 *   0x1393A8  cmp dword [eax+0x10], 0 ; eax = scratch+0x800
 *   0x1393AC  jne 0x1393A8
 *
 * then writes the command block at scratch+0x800/+0x808/+0x80C and 2 at
 * +0x810. With no DSP, the first command 2 (in the intro) stays posted, and
 * the next call spins forever: the hub hang after its load (notes 18.30),
 * reached from the sound manager sub_000B9700 via sub_00137516. 18.25 missed
 * this wait: the loop polls [eax+0x10], not the word's own address.
 *
 * Fix: the same function in C, with the same answer as sub_001391DE: a
 * command still pending is taken as consumed, the word cleared. Registers
 * and memory otherwise as the original (eax = scratch+0x800, edx = size>>2
 * on the posting path).
 */
void sub_0013937A(void)
{
    static uint64_t answered;
    uint32_t self = ecx, arg1 = MEM32(esp + 4), arg2 = MEM32(esp + 8);
    uint32_t start, size, mbox;

    edx = arg2;
    if (arg2) {
        start = arg1;
        size = arg2;
    } else {
        start = MEM32(self + 0x18);
        edx = size = MEM32(self + 0x1C);
        if (start == 0x8000 || !size) {
            esp += 12;                             /* ret 8 */
            return;
        }
    }
    mbox = MEM32(MEM32(MEM32(self + 8) + 0x10)) + 0x800;
    eax = mbox;
    if (MEM32(mbox + 0x10)) {
        if (++answered <= 4 || (answered & (answered - 1)) == 0)
            fprintf(stderr, "[DSP] GP command %u at 0x%08X taken as done"
                    " before the next post (no DSP; %llu so far)\n",
                    MEM32(mbox + 0x10), mbox + 0x10, (unsigned long long)answered);
        MEM32(mbox + 0x10) = 0;
    }
    MEM32(mbox) = (start >> 2) - MEM32(mbox + 4) - 0x206;
    MEM32(mbox + 8) = start;
    MEM32(mbox + 0xC) = edx = size >> 2;
    MEM32(mbox + 0x10) = 2;
    MEM32(self + 0x1C) = 0;
    MEM32(self + 0x18) = 0x8000;
    esp += 12;                                     /* ret 8 */
}

/* ── XPP input: poll and vibration entry points -> xbox_input ─────────── */

/*
 * Rate-limited entry trace, same shape as recomp_icall_not_code_log: report
 * at 1, 10, 100, 1000 ... calls, so a single line means "reached once" and a
 * growing count means "reached every frame", without spamming a 90 s run.
 * Temporary diagnostic for this step; remove once input is confirmed live.
 */
static void trace_hit(const char *tag, uint64_t *counter)
{
    uint64_t n = ++(*counter);
    while (n >= 10 && n % 10 == 0)
        n /= 10;
    if (n == 1)
        fprintf(stderr, "[XPP] %s hit #%llu\n", tag, (unsigned long long)*counter);
}

/* ── XPP input: open/close, handles are opaque per-port tokens ─────── */

/*
 * Traced in notes 18.22. The value XInputOpen returns lands in controller
 * slot +0 (0x427598, stride 0xF0) and game code only ever null-tests it and
 * passes it back to XPP: poll (sub_001542AB), vibration (sub_0015431A) and
 * close (sub_0015429F). All three are hand-written here, so the handle never
 * has to be a real device object. That lets open skip the whole USB stack
 * below it (sub_001554A1 -> sub_00154B69's enumerated-node table ->
 * sub_00155C28, the URB submit into the OHCI driver, and the completion
 * wait), none of which this recomp can drive.
 *
 * Token = 0xC0DE0000 | port. Deliberately unmapped guest space: if anything
 * unexpected ever dereferences a handle, it faults at an address that names
 * itself instead of silently reading fabricated data.
 */
#define XPP_TOKEN_BASE  0xC0DE0000u
#define XPP_GAMEPAD_TYPE 0x001534ACu    /* gamepad class-descriptor type key */

static int xpp_token_port(uint32_t handle, DWORD *port)
{
    if ((handle & 0xFFFF0000u) != XPP_TOKEN_BASE ||
        (handle & 0xFFFFu) >= XBOX_MAX_CONTROLLERS)
        return 0;
    *port = handle & 0xFFFFu;
    return 1;
}

/*
 * sub_00154249 (0x154249..0x15429F), XInputOpen:
 *
 *   HANDLE Open(type, portKey, slotFlag, cfg)  ; stdcall, ret 0x10
 *
 * portKey is the caller's [0x1B0AC8 + 4*port], a .data table verified from
 * the XBE to be {0,1,2,3} and never written (only two readers, both loads),
 * so portKey is the port number. slotFlag/cfg only select the real open's
 * pipe configuration, which this replacement doesn't build.
 *
 * Returns a token if the type is the gamepad and the host reports a
 * controller on that port, else 0 -- the same value the real failure path
 * returns (its SetLastError is skipped; the one caller, sub_000EB5A0,
 * discards GetLastError).
 */
void sub_00154249(void)
{
    uint32_t type    = MEM32(esp + 4);
    uint32_t portKey = MEM32(esp + 8);
    uint32_t handle  = 0;

    if (type == XPP_GAMEPAD_TYPE && portKey < XBOX_MAX_CONTROLLERS &&
        dev_port_connected(portKey))
        handle = XPP_TOKEN_BASE | portKey;

    fprintf(stderr, "[XPP] open type=0x%08X portKey=%u -> handle=0x%08X\n",
            type, portKey, handle);
    eax = handle;
    esp += 20;                                     /* ret 0x10 */
}

/*
 * sub_0015429F (0x15429F..0x1542AB), XInputClose: `ret 4`, one arg.
 * The real body (sub_00155158) dereferences handle+0/+0xA3 and pushes the
 * handle onto the [0x4350A0] free list -- memory corruption for a token.
 * Tokens own nothing, so closing one is a no-op.
 */
void sub_0015429F(void)
{
    fprintf(stderr, "[XPP] close handle=0x%08X\n", MEM32(esp + 4));
    esp += 8;                                      /* ret 4 */
}

/*
 * sub_001542AB (0x1542AB..0x154317, 111 bytes), the XPP poll entry:
 *
 *   DWORD Poll(void *devicePtr, XBOX_INPUT_STATE_shaped *outBuf)  ; ret 8
 *
 * Traced from both callers (sub_00036330, sub_000369F0) and the callee body:
 *
 *   arg1 (edx) = the device object pointer (both callers pass eax = their
 *     [ebp+0], the field they null-check a few lines earlier to decide
 *     whether a controller is open at all).
 *   arg2       = pointer to a 0x16 (22)-byte output buffer. Both callers
 *     build it by rep-movsd'ing 20+2 bytes of a local scratch area right
 *     before the call, so its shape is whatever this function writes: 4
 *     bytes at +0, then a variable-length copy at +4 whose length comes from
 *     device[+0xa3]->[+8]->byte. That length is 0x16 in practice (22 - 4 =
 *     18), and 18 bytes is exactly WORD wButtons + BYTE bAnalogButtons[8] +
 *     4x SHORT thumb axes -- i.e. the output buffer is byte-for-byte
 *     XBOX_INPUT_STATE (xinput_xbox.h): dwPacketNumber, then Gamepad.
 *
 * The original body: raises IRQL, reads ecx = device[+0xa3] (a dispatch
 * context), and checks ecx[+4] against the fixed address 0x153500 -- an
 * "is any driver actually bound to this port" sentinel (0x153500 sits at
 * the very start of the XPP section, the address of the always-present
 * default/no-op dispatch routine). If unbound: returns 0x57 with no copy.
 * A second check (ecx[+0]==0, or bit 1 of ecx[+4] clear) sets a 0x48F
 * (ERROR_DEVICE_NOT_CONNECTED) result code but still falls through to copy
 * whatever is there. Then: *outBuf = device[+8] (a running packet/sequence
 * dword), and the next dispatch-context-derived length of bytes are copied
 * from device[+0x14] (the live gamepad-state sub-object embedded in the
 * device) to outBuf+4.
 *
 * Replacement: skip the device-object/dispatch-table layer entirely and
 * poll the host controller directly. `devicePtr` is now always one of the
 * port tokens handed out by the hand-written open (sub_00154249 below), so
 * the port comes straight from it.
 */
/*
 * Step 54 (notes 18.57): the settings overlay's controller gate. The overlay
 * takes the keyboard and mouse only. Held, the game doesn't poll while it is
 * open; running behind it (step 57, pause off) it does, and every poll from
 * Esc until the overlay has closed gets an idle pad (counted). And whatever
 * button or trigger is held at the first poll after the overlay closes stays
 * masked until it is released, so a press made while it was open never
 * reaches the game; the keyboard's keys count, as they are part of the pad
 * by then. The sticks pass. The overlay never opened: nothing here changes
 * the state.
 */
extern int d3dh_overlay_input(unsigned *closes);

static void overlay_gate(DWORD port, XBOX_INPUT_STATE *s)
{
    static unsigned seen[XBOX_MAX_CONTROLLERS], polled_open;
    static uint16_t mask_btn[XBOX_MAX_CONTROLLERS];
    static uint8_t mask_an[XBOX_MAX_CONTROLLERS][8];
    unsigned closes, i;

    if (port >= XBOX_MAX_CONTROLLERS)
        return;
    if (d3dh_overlay_input(&closes)) {
        memset(&s->Gamepad, 0, sizeof(s->Gamepad));
        polled_open++;
        return;
    }
    if (closes != seen[port]) {         /* closed since this port's last poll */
        seen[port] = closes;
        mask_btn[port] = s->Gamepad.wButtons;
        for (i = 0; i < 8; i++)
            mask_an[port][i] = s->Gamepad.bAnalogButtons[i] >= XBOX_ANALOG_BUTTON_THRESHOLD;
        fprintf(stderr, "[XPP] port %u after the settings overlay: %u polls answered idle while it was open;"
                " held buttons=0x%04X analog=%u%u%u%u%u%u%u%u masked until released\n", (unsigned)port, polled_open,
                mask_btn[port], mask_an[port][0], mask_an[port][1], mask_an[port][2], mask_an[port][3],
                mask_an[port][4], mask_an[port][5], mask_an[port][6], mask_an[port][7]);
        polled_open = 0;
    }
    mask_btn[port] &= s->Gamepad.wButtons;      /* released: no longer masked */
    s->Gamepad.wButtons &= (uint16_t)~mask_btn[port];
    for (i = 0; i < 8; i++)
        if (mask_an[port][i]) {
            if (s->Gamepad.bAnalogButtons[i] < XBOX_ANALOG_BUTTON_THRESHOLD)
                mask_an[port][i] = 0;
            else
                s->Gamepad.bAnalogButtons[i] = 0;
        }
}

/*
 * Step 82 (notes 18.85): the controller chord. View + Menu (Back + Start,
 * as the game's pad has them after the controller map) held together
 * CHORD_MS opens the settings overlay, and again closes it, so a handheld
 * needs no keyboard. While the overlay is closed the game's poll sees it
 * (chord_game): with View held, Start is held back from the game, so the
 * chord doesn't open the game's pause menu too; Start released before the
 * chord completes reaches the game then as a press of CHORD_TAP_MS, and View
 * let go first lets the Start still held through. Start alone (View up)
 * goes through at once, as before. While the overlay is open the game is
 * held (no poll) or gets an idle pad, so the overlay's frames read the pad
 * themselves (twoc_pad_overlay): the chord again closes it, and the D-pad
 * or the left stick, A, B, LB and RB drive it (overlay.cpp). After the
 * chord acts, View and Start count again only once both are up. No pad, or
 * no chord: the state reaches the game as before.
 *
 * Step 86 (notes 18.89): a controller's Start (Menu, Options) held alone
 * START_HOLD_MS opens the overlay too, and closes it. So a press of the
 * controllers' Start (`pad_start`: XInput, the SDL pads and the stand-in,
 * after the controller map, before the keyboard and the input file) is held
 * back from the game from the moment it goes down; let go sooner, the game
 * gets it then, as a press of CHORD_TAP_MS. The pause menu therefore opens
 * when Start is let go, not when it is pressed. The chord and the hold share
 * g_chord_release: whichever acts first, the other doesn't until both
 * buttons are up. A Start from the keyboard or the input file goes through
 * at once, as before.
 */
#define CHORD_MS 700
#define CHORD_TAP_MS 100
#define START_HOLD_MS 1000
static volatile LONG64 g_pad_seen_ms;   /* step 86: when port 0's poll last read a controller */

/* Step 86: a controller (not the keyboard or the input file) answered the
 * game's poll in the last 1.5 s, or the pads thread sees one (SDL mode: an
 * SDL pad or XInput user 0, before the game polls at all, on the splash):
 * the startup message's "hold Start" line. */
int twoc_pad_connected(void)
{
    static ULONGLONG asked;             /* the pads thread's answer, asked at most every 500 ms (one caller: */
    static int seen;                    /* the presenting thread) */
    ULONGLONG now = GetTickCount64();
    LONG64 t = g_pad_seen_ms;

    if (t && (LONG64)now - t < 1500)
        return 1;
    if (now - asked >= 500) {
        asked = now;
        seen = pads_mode() == PADS_SDL && (pads_xinput_present(0) == 1 || pads_others() > 0);
    }
    return seen;
}
extern void d3dh_overlay_pad_open(void);
static volatile LONG g_chord_release;   /* the chord or the hold acted: View and Start up before they count again */

static void chord_game(XBOX_GAMEPAD *g, int pad_start)
{
    static ULONGLONG both_since, tap_until, hold_since;
    static int held_back, start_seen, pad_prev;
    ULONGLONG now = GetTickCount64();
    int v = (g->wButtons & XBOX_GAMEPAD_BACK) != 0, s = (g->wButtons & XBOX_GAMEPAD_START) != 0;
    int pad_down = pad_start && !pad_prev;
    unsigned closes;

    pad_prev = pad_start;
    if (!d3db_host_on || d3dh_overlay_input(&closes)) {
        both_since = 0;                 /* no overlay here, or it is open: the game's pad is idle meanwhile */
        held_back = 0;
        tap_until = 0;
        hold_since = 0;
        start_seen = s;
        if (!v && !s)
            InterlockedExchange(&g_chord_release, 0);
        return;
    }
    if (g_chord_release) {
        if (!v && !s)
            InterlockedExchange(&g_chord_release, 0);
        both_since = 0;
        held_back = 0;
        hold_since = 0;
        start_seen = s;
        return;
    }
    if (pad_down && !hold_since)        /* step 86: the controller's Start: held back until let go or held long */
        hold_since = now ? now : 1;
    if (v && s) {
        if (!both_since) {
            both_since = now ? now : 1;
            held_back = !start_seen;    /* Start came while View was down: the game hasn't had it */
        }
        if (now - both_since >= CHORD_MS) {
            fprintf(stderr, "[XPP] View + Menu held %u ms: the settings overlay opens%s\n", CHORD_MS,
                    held_back || hold_since ? " (Start held back from the game)" : "");
            InterlockedExchange(&g_chord_release, 1);
            both_since = 0;
            hold_since = 0;
            d3dh_overlay_pad_open();
            g->wButtons &= (WORD)~XBOX_GAMEPAD_START;
            start_seen = 0;
            return;
        }
    } else if (both_since) {
        if (held_back && !s && !hold_since) /* Start let go before the chord: the game gets its press now */
            tap_until = now + CHORD_TAP_MS;
        both_since = 0;
        held_back = 0;
    }
    if (hold_since) {
        if (!pad_start) {               /* let go before the hold: the game gets its press now */
            fprintf(stderr, "[XPP] Start pressed %llu ms: the game gets it now, as a press of %u ms\n",
                    (unsigned long long)(now - hold_since), CHORD_TAP_MS);
            tap_until = now + CHORD_TAP_MS;
            hold_since = 0;
        } else if (now - hold_since >= START_HOLD_MS) {
            fprintf(stderr, "[XPP] Start held %u ms: the settings overlay opens (Start held back from the game)\n",
                    START_HOLD_MS);
            InterlockedExchange(&g_chord_release, 1);
            hold_since = 0;
            both_since = 0;
            held_back = 0;
            d3dh_overlay_pad_open();
            g->wButtons &= (WORD)~XBOX_GAMEPAD_START;
            start_seen = 0;
            return;
        }
    }
    if ((held_back && s) || (hold_since && pad_start))
        g->wButtons &= (WORD)~XBOX_GAMEPAD_START;
    if (now < tap_until)
        g->wButtons |= XBOX_GAMEPAD_START;
    start_seen = (g->wButtons & XBOX_GAMEPAD_START) != 0;
}

/* Step 82: port 0's pad for the open overlay's frame (any thread): XInput
 * user 0 (or its stand-in), every other pad (SDL mode), the controller map
 * and the input file, as the poll joins them, without the keyboard (it
 * reaches the overlay as keys). *nav gets OVL_PAD_* bits (pads present:
 * OVL_PAD_ON), *lx, *ly the left stick. Returns 1 when the chord asks the
 * overlay to close. */
int twoc_pad_overlay(unsigned *nav, short *lx, short *ly)
{
    static ULONGLONG xi_retry, both_since, start_since;
    static dev_pad_file_t xi_d, in_d;
    XBOX_INPUT_STATE st;
    pads_src_t others;
    unsigned pk;
    int n = 0, have = 0, v, s, close = 0;
    DWORD r = ERROR_DEVICE_NOT_CONNECTED;
    ULONGLONG now = GetTickCount64();

    *nav = 0;
    *lx = *ly = 0;
    memset(&st, 0, sizeof(st));
    if (g_dev_xi_file)
        r = xi_get(0, &st, &xi_d, NULL);
    else if (pads_mode() == PADS_SDL ? pads_xinput_present(0) != 0 : now >= xi_retry) {
        r = xbox_InputGetState(0, &st);
        if (pads_mode() != PADS_SDL)
            xi_retry = r == ERROR_SUCCESS ? 0 : now + 1000;     /* XInput is slow to say none is there */
    }
    if (r != ERROR_SUCCESS)
        memset(&st, 0, sizeof(st));
    have = r == ERROR_SUCCESS;
    if (pads_mode() == PADS_SDL) {
        n = pads_read(&others, &pk);
        if (have && pads_skip_xinput0())
            memset(&st.Gamepad, 0, sizeof(st.Gamepad));
        have |= n > 0;
    }
    if (have)
        controls_apply_pad(&st.Gamepad, n ? &others : NULL);
    if (g_dev_input_file) {
        XBOX_GAMEPAD f;
        int i;
        dev_pad_file_read(g_dev_input_file, &in_d, NULL, &f);
        st.Gamepad.wButtons |= f.wButtons;
        for (i = 0; i < 8; i++)
            if (f.bAnalogButtons[i] > st.Gamepad.bAnalogButtons[i])
                st.Gamepad.bAnalogButtons[i] = f.bAnalogButtons[i];
        if (f.sThumbLX) st.Gamepad.sThumbLX = f.sThumbLX;
        if (f.sThumbLY) st.Gamepad.sThumbLY = f.sThumbLY;
        have = 1;
    }
    if (!have)
        return 0;
    *nav = OVL_PAD_ON;
    if (st.Gamepad.wButtons & XBOX_GAMEPAD_DPAD_UP) *nav |= OVL_PAD_UP;
    if (st.Gamepad.wButtons & XBOX_GAMEPAD_DPAD_DOWN) *nav |= OVL_PAD_DOWN;
    if (st.Gamepad.wButtons & XBOX_GAMEPAD_DPAD_LEFT) *nav |= OVL_PAD_LEFT;
    if (st.Gamepad.wButtons & XBOX_GAMEPAD_DPAD_RIGHT) *nav |= OVL_PAD_RIGHT;
    if (st.Gamepad.bAnalogButtons[XBOX_BUTTON_A] >= XBOX_ANALOG_BUTTON_THRESHOLD) *nav |= OVL_PAD_A;
    if (st.Gamepad.bAnalogButtons[XBOX_BUTTON_B] >= XBOX_ANALOG_BUTTON_THRESHOLD) *nav |= OVL_PAD_B;
    if (st.Gamepad.bAnalogButtons[XBOX_BUTTON_BLACK] >= XBOX_ANALOG_BUTTON_THRESHOLD) *nav |= OVL_PAD_LB;
    if (st.Gamepad.bAnalogButtons[XBOX_BUTTON_WHITE] >= XBOX_ANALOG_BUTTON_THRESHOLD) *nav |= OVL_PAD_RB;
    v = (st.Gamepad.wButtons & XBOX_GAMEPAD_BACK) != 0;
    s = (st.Gamepad.wButtons & XBOX_GAMEPAD_START) != 0;
    if (v) *nav |= OVL_PAD_VIEW;
    if (s) *nav |= OVL_PAD_MENU;
    *lx = st.Gamepad.sThumbLX;
    *ly = st.Gamepad.sThumbLY;
    if (g_chord_release) {
        if (!v && !s)
            InterlockedExchange(&g_chord_release, 0);
        both_since = 0;
        start_since = 0;
    } else if (v && s) {
        if (!both_since)
            both_since = now ? now : 1;
        start_since = 0;
        if (now - both_since >= CHORD_MS) {
            fprintf(stderr, "[XPP] View + Menu held %u ms: the settings overlay closes\n", CHORD_MS);
            InterlockedExchange(&g_chord_release, 1);
            both_since = 0;
            close = 1;
        }
    } else if (s) {                     /* step 86: Start alone held closes it too */
        both_since = 0;
        if (!start_since)
            start_since = now ? now : 1;
        if (now - start_since >= START_HOLD_MS) {
            fprintf(stderr, "[XPP] Start held %u ms: the settings overlay closes\n", START_HOLD_MS);
            InterlockedExchange(&g_chord_release, 1);
            start_since = 0;
            close = 1;
        }
    } else {
        both_since = 0;
        start_since = 0;
    }
    return close;
}

void sub_001542AB(void)
{
    static uint64_t hits;
    uint32_t outBuf = MEM32(esp + 8);
    XBOX_INPUT_STATE state;
    DWORD result, port;

    trace_hit("poll sub_001542AB", &hits);

    if (!xpp_token_port(MEM32(esp + 4), &port)) {
        eax = 0x48F;                               /* ERROR_DEVICE_NOT_CONNECTED */
        esp += 12;                                 /* ret 8 */
        return;
    }
    if (kb_port(port)) {
        /* Step 57: the keyboard's port. A controller that isn't there is asked
         * for again once a second, not at every poll (XInput is slow to say
         * so), and the game gets the keyboard's pad meanwhile. */
        static ULONGLONG retry_ms;
        ULONGLONG now = GetTickCount64();
        result = ERROR_DEVICE_NOT_CONNECTED;
        if (now >= retry_ms) {
            result = xi_get(port, &state, &g_xi_poll, "XInput stand-in");
            retry_ms = result == ERROR_SUCCESS ? 0 : now + 1000;
        }
        if (result != ERROR_SUCCESS) {
            memset(&state, 0, sizeof(state));
            result = ERROR_SUCCESS;
        }
    } else if (port == 0 && pads_xinput_present(0) == 0 && !g_dev_xi_file) {
        memset(&state, 0, sizeof(state));   /* step 70: the pads thread saw no XInput user 0: not asked */
        result = ERROR_DEVICE_NOT_CONNECTED;
    } else
        result = xi_get(port, &state, &g_xi_poll, "XInput stand-in");
    /* Step 70: every other pad joins port 0 (pads.h): SDL's, and XInput users
     * 1-3, by position, under the same map. Steam Input's virtual pad on user
     * 0 is left out while the pad behind it is read directly. None: as before. */
    pads_src_t others;
    unsigned others_packets = 0;
    int nothers = 0;
    if (port == 0 && pads_mode() == PADS_SDL) {
        nothers = pads_read(&others, &others_packets);
        if (result == ERROR_SUCCESS && pads_skip_xinput0())
            memset(&state.Gamepad, 0, sizeof(state.Gamepad));
        if (result != ERROR_SUCCESS && nothers) {
            memset(&state, 0, sizeof(state));
            result = ERROR_SUCCESS;
        }
    }
    if (result == ERROR_SUCCESS) {
        static DWORD kb_packets;
        controls_apply_pad(&state.Gamepad, nothers ? &others : NULL);  /* step 57: the PC controller's map; as is by default */
        pad_start = (state.Gamepad.wButtons & XBOX_GAMEPAD_START) != 0;    /* step 86: the controllers' Start */
        if (kb_port(port) && controls_apply_keys(&state.Gamepad))
            kb_packets++;                           /* the keyboard's changes count as packets */
        state.dwPacketNumber += kb_packets + others_packets;
    }
    dev_autostart_poll(port, &state, &result);
    if (result == ERROR_SUCCESS && port == 0)
        chord_game(&state.Gamepad, pad_start);  /* step 82: View + Menu opens the overlay; step 86: Start held */
    if (result == ERROR_SUCCESS)
        overlay_gate(port, &state);     /* step 54 */
    if (result == ERROR_SUCCESS) {
        /* Log digital/analog button changes (not thumbsticks, which drift),
         * so a run shows whether real player input reached the game. */
        static uint8_t last[XBOX_MAX_CONTROLLERS][10];
        uint8_t now[10];
        memcpy(now, &state.Gamepad.wButtons, 2);
        memcpy(now + 2, state.Gamepad.bAnalogButtons, 8);
        if (memcmp(now, last[port], sizeof(now)) != 0) {
            memcpy(last[port], now, sizeof(now));
            fprintf(stderr, "[XPP] port %u input: buttons=0x%04X analog=%02X %02X %02X %02X %02X %02X %02X %02X\n",
                    (unsigned)port, state.Gamepad.wButtons,
                    now[2], now[3], now[4], now[5], now[6], now[7], now[8], now[9]);
        }
    }
    if (outBuf) {
        /* XBOX_INPUT_STATE is 22 bytes with no padding (DWORD, WORD,
         * BYTE[8], 4x SHORT all fall on their natural alignment) -- the
         * same 22 bytes the original copied from the device object. */
        memcpy((void *)XBOX_PTR(outBuf), &state, sizeof(state));
    }
    eax = (result == ERROR_SUCCESS) ? 0 : 0x48F;  /* ERROR_DEVICE_NOT_CONNECTED */
    esp += 12;                                     /* ret 8 */
}

/*
 * sub_0015431A (0x15431A..0x154350, 54 bytes), the XPP vibration entry:
 *
 *   DWORD SetVibration(void *devicePtr, void *payload)  ; ret 8
 *
 * Traced from its one caller, sub_000362F0, and the callee body:
 *
 *   arg1 = ecx from the caller = *devicePtrWrapper (the real device object,
 *     one dereference deeper than the wrapper struct sub_000362F0 itself
 *     received).
 *   arg2 = payload = (wrapper + 0x30), a sub-struct inside the *caller's*
 *     wrapper (not the device object). The caller writes the two motor
 *     values into this same wrapper just before the call, at wrapper+0x72
 *     and wrapper+0x74 -- i.e. payload+0x42 (left) and payload+0x44
 *     (right), each already replicated from a 0-255 byte to a 0-65535 word
 *     (`imul v, 0x101`, the classic v | (v<<8)).
 *
 * The original body does the same dispatch-context/sentinel check as the
 * poll entry (device[+0xa3]->[+4] against 0x153500 -> 0x57 if unbound),
 * then, if bound, clears a status byte at payload+0x40, writes a derived
 * status byte at payload+0x41, and calls sub_001551F9 (the actual
 * IRP-queue/USB-write) to push the motor values already sitting in
 * *payload out to hardware. It does not re-read the motor words itself --
 * they're only ever consumed downstream, by the call we're replacing.
 *
 * Replacement: read the two motor words straight out of the caller's
 * payload struct (their offsets are the only part of this we actually
 * need) and hand them to xbox_InputSetState, on the port named by the
 * handle token (arg1).
 */
void sub_0015431A(void)
{
    static uint64_t hits;
    uint32_t payload = MEM32(esp + 8);
    DWORD port;

    trace_hit("vibration sub_0015431A", &hits);

    if (payload && xpp_token_port(MEM32(esp + 4), &port)) {
        XBOX_VIBRATION vib;
        vib.wLeftMotorSpeed  = MEM16(payload + 0x42);
        vib.wRightMotorSpeed = MEM16(payload + 0x44);
        if (!(port == 0 && pads_skip_xinput0()))
            xbox_InputSetState(port, &vib);
        if (port == 0)
            pads_rumble(vib.wLeftMotorSpeed, vib.wRightMotorSpeed);    /* step 70: every other pad (SDL's rumble) */
        {   /* Step 70: each rumble's start and end (the game sets the motors every frame, ramps included) */
            static int on[XBOX_MAX_CONTROLLERS];
            static unsigned peak[XBOX_MAX_CONTROLLERS][2];
            int now_on = vib.wLeftMotorSpeed || vib.wRightMotorSpeed;
            if (port < XBOX_MAX_CONTROLLERS) {
                if (now_on && !on[port])
                    fprintf(stderr, "[XPP] port %u vibration on: left %u right %u\n", (unsigned)port,
                            vib.wLeftMotorSpeed, vib.wRightMotorSpeed);
                if (now_on) {
                    peak[port][0] = vib.wLeftMotorSpeed > peak[port][0] ? vib.wLeftMotorSpeed : peak[port][0];
                    peak[port][1] = vib.wRightMotorSpeed > peak[port][1] ? vib.wRightMotorSpeed : peak[port][1];
                } else if (on[port]) {
                    fprintf(stderr, "[XPP] port %u vibration off (peak left %u right %u)\n", (unsigned)port,
                            peak[port][0], peak[port][1]);
                    peak[port][0] = peak[port][1] = 0;
                }
                on[port] = now_on;
            }
        }
    }
    eax = 0;
    esp += 12;                                     /* ret 8 */
}

/* ── XPP input: port-active gate, primed from a simulated insertion ───── */

/*
 * Time-boxed search first (this step), before writing anything below: is
 * [0x1534B0]/[0x153434] serviced by an unserviced interrupt/DPC, the same
 * shape as the D3D vblank gap (sub_00106E60 above)?
 *
 * Traced the ISR that would arm them: sub_00153600 (XPP's DriverEntry-like
 * IoCreateDevice loop) calls xbox_KeInitializeInterrupt/xbox_KeConnectInterrupt
 * at 0x153E96/0x153E9D with ServiceRoutine = sub_001596F4. That ISR reads a
 * real OHCI HcInterruptStatus-shaped register pair (device[+0xc]/[+0x10]),
 * tests bit 0x80000000 (master enable) and bit 0x20 (root-hub status change --
 * the actual "something got plugged in" hardware bit), and only then queues
 * the DPC that would eventually reach sub_00154AAD's gate fields. So this is
 * not a case of "a DPC exists but nothing pumps it" like the vblank fix --
 * it is gated behind a real root-hub-status-change interrupt that would have
 * to come from this recomp's OHCI/USB host-controller emulation, which
 * (14.4/15.1) does not walk the controller far enough to ever raise it.
 * Servicing this faithfully means building real OHCI root-hub-status
 * emulation, not writing a few lines here -- so per the "if not found
 * quickly" fallback, this step does not attempt that and instead drives the
 * game's own existing, already-correctly-lifted per-port insertion handler
 * directly, once, from the host controller's real connection state.
 *
 * sub_000EB270 (0xEB270..0xEB29A, 42 bytes, called every frame from
 * sub_00086D30 whenever the poll path is not already live -- see 18.15) is
 * the read side of the gate: it scans the 4-slot table at 0x427598 (step
 * 0xF0) for a slot whose first dword is nonzero and returns that slot's
 * address in eax, or 0 if none are active. That returned pointer is stored
 * into [0x23c2d8] and then dereferenced *again* one level deeper by
 * sub_00036330 (ebp = *eax; then dword ptr [ebp], [ebp+0xa3], [ebp+0xd4] ...),
 * so the slot's first dword has to be a real device-object pointer with a
 * populated dispatch-context field, not just a truthy sentinel -- writing a
 * bare 1 there (the originally-sketched override) would make sub_00036330
 * dereference address 0x1 and crash. sub_000EB5A0 is exactly the function
 * that builds that real device object (via sub_00154249/sub_001547DA, the
 * same lookup the driver-init table at 0x153404-0x15340C feeds): it is the
 * per-port insertion handler the notification pump would have called had the
 * hardware interrupt fired.
 *
 * Fix: reimplement sub_000EB270's own trivial scan faithfully (unchanged
 * behavior), but on its first call, if the host reports a controller
 * connected on port 0, first hand-call the real sub_000EB5A0(0) to populate
 * the slot the same way a genuine insertion notification would. If
 * xbox_InputIsConnected(0) is false, the scan below simply finds nothing:
 * no crash either way, this can only make the gate open, never open it
 * incorrectly. (sub_000EB5A0's open, sub_00154249, is hand-written too --
 * see "open/close" above -- because the real one can never succeed here.)
 */
extern void sub_000EB5A0(void);

/* Step 70: port 0 as XGetDeviceChanges (sub_00154AAD, below) last told the
 * game; set by the prime here, so nothing is reported before it ran. */
static int g_xpp_primed, g_xpp_port0;

void sub_000EB270(void)
{
    static int primed;

    if (!primed) {
        primed = 1;
        int connected = dev_port_connected(0);
        g_xpp_primed = 1;
        g_xpp_port0 = connected;
        fprintf(stderr, "[XPP] sub_000EB270 primed: xbox_InputIsConnected(0)=%d\n",
                xbox_InputIsConnected(0));
        if (connected && !xbox_InputIsConnected(0))
            fprintf(stderr, "[%s] %s: no pad on port 0, using a virtual one\n",
                    g_dev_autostart || g_dev_input_file ? "DEV" : "KEY",
                    g_dev_autostart ? "autostart" : g_dev_input_file ? "input file" : "keyboard");
        if (connected) {
            PUSH32(esp, 0);            /* port 0, the one stack arg (cdecl) */
            PUSH32(esp, 0x000EB270u);  /* synthetic return address; unread */
            RECOMP_ABI_CALL(0x000EB5A0u, sub_000EB5A0);
            esp += 4;                  /* cdecl: pop our own pushed arg */
            fprintf(stderr, "[XPP] sub_000EB5A0(0) returned, slot[0]=0x%08X\n",
                    MEM32(0x427598u));
        }
    }

    /* Original body, unchanged: first slot (step 0xF0) with a nonzero first
     * dword, else 0. */
    eax = 0;
    for (uint32_t slot = 0x427598u; slot < 0x427958u; slot += 0xF0u) {
        if (MEM32(slot) != 0) {
            eax = slot;
            break;
        }
    }
    /* The original's `ret`: pop the return address the caller pushed. Missing
     * until notes 18.41, so every call left the guest stack 4 bytes low. A
     * caller with a frame pointer hid it (mov esp, ebp), but the front-end
     * loop sub_0002D950 pops edi/esi/ebx before that, so it handed its caller
     * garbage callee-saved registers: sub_00087BF0's esi, the 7 MB level-pool
     * size, came back as ~180 KB, the pool was reallocated that small with
     * its end still at base + 7 MB, and the level spilled into heap blocks
     * DSOUND then took and zeroed (the first attract demo's crash, 18.40). */
    esp += 4;
}

/*
 * Step 70 (notes 18.73): hot-plug. sub_00154AAD (0x154AAD..0x154B18),
 * XGetDeviceChanges:
 *
 *   BOOL GetDeviceChanges(type, DWORD *insertions, DWORD *removals)  ; ret 0xC
 *
 * The device type object holds the ports' mask now (+0), a "changed" flag
 * (+4) and the mask last reported (+8); +4 is set only by the USB
 * enumeration this recomp never runs (18.16), so the lifted body always says
 * "nothing changed". The game's pump sub_000EB6C0 asks it every frame for
 * the gamepad type (0x1534AC) and the memory units (0x153430), closes and
 * clears a removed port's slot (sub_0015429F, slot +0 = 0) and opens an
 * inserted one (sub_000EB5A0, the insertion handler the prime calls).
 *
 * Replacement: port 0 for the gamepad type, from dev_port_connected (a pad
 * on XInput user 0 or any other, the keyboard, the dev switches), against
 * what the game was last told; nothing for every other type and port (the
 * lifted body's answer). Silent until sub_000EB270's one-shot prime has
 * armed port 0 or found nothing to arm, so a run starts as before; after
 * it, a pad plugged in arms the slot and the last one unplugged disarms it
 * (once it has been gone 1 s).
 * A port without its pad isn't polled, so XInput user 0 is looked for here:
 * through the pads thread in SDL mode, else once every 2 s (XInput is slow
 * to say a user isn't there).
 */
void sub_00154AAD(void)
{
    uint32_t type = MEM32(esp + 4), pins = MEM32(esp + 8), prem = MEM32(esp + 12);
    uint32_t ins = 0, rem = 0;

    if (type == XPP_GAMEPAD_TYPE && g_xpp_primed) {
        int now = dev_port_connected(0);
        if (!now) {
            static ULONGLONG probe_ms;
            ULONGLONG t = GetTickCount64();
            int p = pads_xinput_present(0);
            if (p > 0 || (p < 0 && t - probe_ms >= 2000)) {
                XBOX_INPUT_STATE st;
                probe_ms = t;
                xbox_InputGetState(0, &st);     /* updates xbox_InputIsConnected(0) */
                now = dev_port_connected(0);
            }
        }
        {   /* A port gone is told after 1 s gone, so a pad that drops out for a
             * moment (a Bluetooth hiccup, SDL restarting when the controller
             * input setting changes) doesn't take the game's slot with it. */
            static ULONGLONG gone_ms;
            ULONGLONG t = GetTickCount64();
            if (now || !g_xpp_port0)
                gone_ms = 0;
            else if (!gone_ms)
                gone_ms = t;
            if (!now && g_xpp_port0 && t - gone_ms < 1000)
                now = 1;
        }
        if (now != g_xpp_port0) {
            g_xpp_port0 = now;
            if (now)
                ins = 1;
            else
                rem = 1;
            fprintf(stderr, "[XPP] port 0 %s (XGetDeviceChanges): XInput user 0 %s, other pads %s, keyboard %s\n",
                    now ? "plugged in: the game arms its slot" : "unplugged: the game closes its slot",
                    xbox_InputIsConnected(0) ? "there" : "not there", pads_others() ? "there" : "none",
                    kb_port(0) ? "on" : "off");
        }
    }
    if (pins)
        MEM32(pins) = ins;
    if (prem)
        MEM32(prem) = rem;
    eax = (ins | rem) != 0;
    esp += 16;                                     /* ret 0xC */
}

/* ── D3D mirror bridge wrappers (Option B) ─────────────────── */

/*
 * Notes 18.31 (the plan) and 18.32 (S1). Each wrapper runs the lifted body,
 * sub_X_gen, unchanged. With RECOMP_D3D_BRIDGE set, d3d_bridge.c also sees
 * the arguments before the call and eax after it; in S1 that feeds a
 * log-only census and nothing else. The lifter emits the original body as
 * sub_X_gen for each `extern void sub_X_gen(void);` below (manual_scan.py,
 * _WRAP_RE), and every direct and indirect caller reaches the wrapper.
 * These have to be spelled out: the lifter's scan reads this file as text.
 */

/* The 15 entries the bridge wraps. */
extern void sub_000FD6E0_gen(void);
void sub_000FD6E0(void) { D3DB_CALL(D3DB_CREATE_DEVICE, sub_000FD6E0_gen); }
extern void sub_00100EA0_gen(void);
void sub_00100EA0(void) { D3DB_CALL(D3DB_CLEAR, sub_00100EA0_gen); }
extern void sub_00100C40_gen(void);
void sub_00100C40(void) { D3DB_CALL(D3DB_SWAP, sub_00100C40_gen); }
extern void sub_001019C0_gen(void);
void sub_001019C0(void) { D3DB_CALL(D3DB_DRAW_UP, sub_001019C0_gen); }
extern void sub_00101B20_gen(void);
void sub_00101B20(void) { D3DB_CALL(D3DB_DRAW, sub_00101B20_gen); }
extern void sub_00101BC0_gen(void);
void sub_00101BC0(void) { D3DB_CALL(D3DB_DRAW_INDEXED, sub_00101BC0_gen); }
extern void sub_00101EC0_gen(void);
void sub_00101EC0(void) { D3DB_CALL(D3DB_BEGIN, sub_00101EC0_gen); }
extern void sub_00101E20_gen(void);
void sub_00101E20(void) { D3DB_DATA_CALL(D3DB_DATA2F, sub_00101E20_gen); }
extern void sub_00101E60_gen(void);
void sub_00101E60(void) { D3DB_DATA_CALL(D3DB_DATA4F, sub_00101E60_gen); }
extern void sub_00101F00_gen(void);
void sub_00101F00(void) { D3DB_CALL(D3DB_END, sub_00101F00_gen); }
extern void sub_000FF580_gen(void);
void sub_000FF580(void) { D3DB_CALL(D3DB_COPYRECTS, sub_000FF580_gen); }
extern void sub_00102AA0_gen(void);
void sub_00102AA0(void) { D3DB_CALL(D3DB_VS_CONSTANT, sub_00102AA0_gen); }
extern void sub_00103C80_gen(void);
void sub_00103C80(void) { D3DB_CALL(D3DB_TEX_LOCK, sub_00103C80_gen); }
extern void sub_00103E00_gen(void);
void sub_00103E00(void) { D3DB_CALL(D3DB_SURF_LOCK, sub_00103E00_gen); }
extern void sub_000FEC00_gen(void);
void sub_000FEC00(void) { D3DB_CALL(D3DB_FAST_TEXTURE, sub_000FEC00_gen); }

/* Census-only probes (S1): the step 29 identities marked "probable", the
 * vblank callback and the state-block pair. Log-only like the rest; they can
 * go once the census has answered. */
extern void sub_000FD680_gen(void);
void sub_000FD680(void) { D3DB_CALL(D3DB_PROBE_FD680, sub_000FD680_gen); }
extern void sub_000FEC90_gen(void);
void sub_000FEC90(void) { D3DB_CALL(D3DB_PROBE_FEC90, sub_000FEC90_gen); }
extern void sub_00039910_gen(void);
void sub_00039910(void) { D3DB_CALL(D3DB_PROBE_VBLANK_CB, sub_00039910_gen); }
extern void sub_001012CC_gen(void);
void sub_001012CC(void) { D3DB_CALL(D3DB_BEGIN_SB, sub_001012CC_gen); }
extern void sub_0010191E_gen(void);
void sub_0010191E(void) { D3DB_CALL(D3DB_END_SB, sub_0010191E_gen); }
extern void sub_00101705_gen(void);
void sub_00101705(void) { D3DB_CALL(D3DB_APPLY_SB, sub_00101705_gen); }
extern void sub_0010188D_gen(void);
void sub_0010188D(void) { D3DB_CALL(D3DB_DELETE_SB, sub_0010188D_gen); }
extern void sub_000FDC90_gen(void);
void sub_000FDC90(void) { D3DB_CALL(D3DB_PROBE_DXT1NOISE, sub_000FDC90_gen); }
/* The splash routine (notes 18.1): marks where the splash's draws start and end. */
extern void sub_000875A0_gen(void);
void sub_000875A0(void) { D3DB_CALL(D3DB_PROBE_SPLASH, sub_000875A0_gen); }
