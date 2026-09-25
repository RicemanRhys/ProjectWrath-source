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
}

/* A port the game may open: really connected, or port 0 under autostart or
 * the input file. */
static int dev_port_connected(DWORD port)
{
    return xbox_InputIsConnected(port)
        || ((g_dev_autostart || g_dev_input_file) && port == 0);
}

/* The input file's pad, reread at most every 50 ms. Returns 1 if it changed
 * since the last call, so the caller can advance the packet number. */
static int dev_input_file_read(XBOX_GAMEPAD *pad)
{
    static const char *const analog[8] = { "a", "b", "x", "y", "black", "white", "lt", "rt" };
    static XBOX_GAMEPAD cur;
    static ULONGLONG last_ms;
    static char last_line[256];
    ULONGLONG now = GetTickCount64();
    char line[256] = "";
    int changed = 0;

    if (now - last_ms >= 50) {
        FILE *f = fopen(g_dev_input_file, "r");
        last_ms = now;
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
            fprintf(stderr, "[DEV] input file (+%.1f s): \"%s\"\n",
                    (now - g_dev_start_ms) / 1000.0, line);
            changed = 1;
        }
    }
    *pad = cur;
    return changed;
}

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
 */
#define COOP_NTSUSPEND_THUNK  0x0015AA7Cu      /* [thunk] = NtSuspendThread (ordinal 231) */
#define COOP_NTRESUME_THUNK   0x0015AA80u      /* [thunk] = NtResumeThread (ordinal 224) */
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
    HANDLE        wake;                /* manual-reset, signaled while suspend == 0 */
} coop_thread_t;

static coop_thread_t g_coop[COOP_MAX_THREADS];
static SRWLOCK g_coop_lock = SRWLOCK_INIT;
static volatile LONG g_coop_logged;
static RECOMP_TLS coop_thread_t *t_coop;
static RECOMP_TLS uint32_t t_coop_kernel_va;   /* set by the lookup, read by the call */
static RECOMP_TLS int t_coop_depth;            /* nesting of kernel calls */

static const char *const coop_state_name[] = { "in guest code", "in a kernel call", "parked" };

/* The record for a thread id, created if asked. NULL if the table is full. */
static coop_thread_t *coop_find(DWORD tid, int create)
{
    coop_thread_t *t = NULL;
    int i;

    AcquireSRWLockExclusive(&g_coop_lock);
    for (i = 0; i < COOP_MAX_THREADS && g_coop[i].tid; i++)
        if (g_coop[i].tid == tid) {
            t = &g_coop[i];
            break;
        }
    if (!t && create && i < COOP_MAX_THREADS) {
        t = &g_coop[i];
        t->suspend = 0;
        t->state = COOP_GUEST;
        t->wake = CreateEventA(NULL, TRUE, TRUE, NULL);
        t->tid = tid;
    }
    ReleaseSRWLockExclusive(&g_coop_lock);
    return t;
}

static coop_thread_t *coop_self(void)
{
    if (!t_coop)
        t_coop = coop_find(GetCurrentThreadId(), 1);
    return t_coop;
}

/* Called only at depth 0, where this thread holds no host lock. */
static void coop_park(coop_thread_t *t)
{
    if (t->suspend <= 0)
        return;
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

/* Every guest kernel call, via recomp_lookup_manual. */
static void coop_kernel_call(void)
{
    uint32_t va = t_coop_kernel_va;
    coop_thread_t *self = coop_enter();

    recomp_lookup_kernel(va)();            /* the lookup also selects this thread's dispatch slot */
    coop_leave(self);
    heap_watch();
}

static void coop_log(const char *what, DWORD tid, LONG prev, uint32_t ret, const char *state)
{
    if (InterlockedIncrement(&g_coop_logged) > COOP_LOG_MAX)
        return;
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
        ULONGLONG until = GetTickCount64() + COOP_SETTLE_MS;
        LONG st;
        while ((st = InterlockedCompareExchange(&t->state, 0, 0)) == COOP_GUEST &&
               t->suspend > 0 && GetTickCount64() < until)
            Sleep(1);
        coop_log("suspend", tid, prev, ret,
                 st == COOP_GUEST ? "still in guest code, parks at its next kernel call"
                                  : coop_state_name[st]);
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
 * than it asked for is recorded here until that block is released.
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

/* NTSTATUS NtAllocateVirtualMemory(PVOID *BaseAddress, ULONG_PTR ZeroBits,
 *     PSIZE_T RegionSize, ULONG AllocationType, ULONG Protect); ret 20.
 * The bridge does the work; this only measures the block it got. */
static void vm_NtAllocateVirtualMemory(void)
{
    uint32_t base_va = MEM32(esp + 4), size_va = MEM32(esp + 12), type = MEM32(esp + 16);
    uint32_t hint = base_va ? MEM32(base_va) : 0;
    uint32_t base, size, blk;

    t_coop_kernel_va = MEM32(VM_NTALLOC_THUNK);
    coop_kernel_call();
    if (eax != 0 || !base_va || !size_va || (hint && !(type & 0x2000)))
        return;                            /* failed, or a commit on an existing region */
    base = MEM32(base_va);
    size = (MEM32(size_va) + 0xFFFu) & ~0xFFFu;
    blk = xbox_HeapBlockSize(base);
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
    uint32_t blk = xbox_HeapBlockSize(base);
    coop_thread_t *self;

    /* A block start: the byte before it isn't in the same block. */
    int release = (type & VM_MEM_RELEASE) && blk && xbox_HeapBlockSize(base - 1) != blk + 1;
    int decommit = !(type & VM_MEM_RELEASE) && (type & VM_MEM_DECOMMIT) && base;

    if (!release && !decommit) {
        InterlockedIncrement(&g_vm_bridged);
        t_coop_kernel_va = MEM32(VM_NTFREE_THUNK);
        coop_kernel_call();
        return;
    }
    self = coop_enter();
    if (release) {
        xbox_HeapFree(base);
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

/* One [HEAPSTAT] line every 2 s, from whichever thread gets there. */
static void heap_watch(void)
{
    static volatile LONG last;
    LONG now = (LONG)GetTickCount(), prev = last;
    xbox_heap_stats_t s;

    if (prev && now - prev < HEAP_WATCH_MS)
        return;
    if (InterlockedCompareExchange(&last, now, prev) != prev)
        return;
    if (!prev)
        return;                            /* start the clock */
    xbox_HeapStats(&s);
    fprintf(stderr, "[HEAPSTAT] used %u/%u KiB (%.1f%%); live %u KiB in %u blocks; "
                    "free %u KiB in %u blocks, largest %u KiB; VM released %ld (%ld KiB), "
                    "decommits %ld, bridged %ld; reuse slack %u KiB live, max %u KiB, %u events\n",
            s.used / 1024, s.total / 1024, s.total ? 100.0 * s.used / s.total : 0.0,
            s.live / 1024, s.live_blocks, s.free / 1024, s.free_blocks, s.largest_free / 1024,
            (long)g_vm_released, (long)g_vm_released_kb, (long)g_vm_decommits, (long)g_vm_bridged,
            g_vm_waste_live / 1024, g_vm_waste_max / 1024, g_vm_waste_events);
    fflush(stderr);
}

recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    /* Guest kernel calls: cooperative suspension, see above. */
    if (xbox_va >= COOP_KERNEL_VA_LO && xbox_va < COOP_KERNEL_VA_HI) {
        if (xbox_va == MEM32(COOP_NTSUSPEND_THUNK)) return coop_NtSuspendThread;
        if (xbox_va == MEM32(COOP_NTRESUME_THUNK))  return coop_NtResumeThread;
        if (xbox_va == MEM32(VM_NTFREE_THUNK))      return vm_NtFreeVirtualMemory;
        if (xbox_va == MEM32(VM_NTALLOC_THUNK))     return vm_NtAllocateVirtualMemory;
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
    result = xbox_InputGetState(port, &state);
    dev_autostart_poll(port, &state, &result);
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
        xbox_InputSetState(port, &vib);
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

void sub_000EB270(void)
{
    static int primed;

    if (!primed) {
        primed = 1;
        int connected = dev_port_connected(0);
        fprintf(stderr, "[XPP] sub_000EB270 primed: xbox_InputIsConnected(0)=%d\n",
                xbox_InputIsConnected(0));
        if (connected && !xbox_InputIsConnected(0))
            fprintf(stderr, "[DEV] %s: no pad on port 0, using a virtual one\n",
                    g_dev_autostart ? "autostart" : "input file");
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
void sub_00101E20(void) { D3DB_CALL(D3DB_DATA2F, sub_00101E20_gen); }
extern void sub_00101E60_gen(void);
void sub_00101E60(void) { D3DB_CALL(D3DB_DATA4F, sub_00101E60_gen); }
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
