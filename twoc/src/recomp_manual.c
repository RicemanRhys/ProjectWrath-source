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
#include <string.h>

/* The generated-code headers: register aliases (eax, ecx, esp ...), MEM32/MEM8,
 * PUSH32, RECOMP_ABI_CALL and the declarations of every lifted function. The
 * hand-written sub_ functions below need them; they are only active with
 * RECOMP_GENERATED_CODE defined. */
#define RECOMP_GENERATED_CODE
#include "recomp/gen/recomp_funcs.h"

/* ── ICALL trace ring buffer ───────────────────────────────── */

/*
 * The RECOMP_ICALL macro (recomp_types.h) writes g_icall_trace, g_icall_trace_idx
 * and g_icall_count on every indirect call; recomp_icall_fail_log() dumps the
 * last 16 targets. The runtime owns them (xbox_kernel, src/kernel/
 * xbox_memory_layout.c) and recomp_types.h declares them extern, so nothing is
 * defined or declared here: a second definition is a duplicate symbol (LNK2005).
 */

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
recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    /*
     * TODO: Add your overrides here. Examples:
     *
     * if (xbox_va == 0x00012345) return traced_sub_00012345;
     * if (xbox_va == 0x00067890) return stub_00067890;
     * if (xbox_va == 0x000ABCDE) return fixed_sub_000ABCDE;
     */

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
