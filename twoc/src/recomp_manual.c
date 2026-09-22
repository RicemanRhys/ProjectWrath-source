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

/* xbox_bridge_resolve_handle: resolves a guest handle token back to the real
 * native HANDLE, for the hand-written GetExitCodeThread below. */
#include <xbox/xboxrecomp.h>

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
extern void sub_001542AB(void);
extern void sub_0015431A(void);

recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    /* XPP input entry points -> xbox_input. See the block comments above
     * their definitions below for the traced ABI. */
    if (xbox_va == 0x001542AB) return sub_001542AB;
    if (xbox_va == 0x0015431A) return sub_0015431A;

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
 * poll the host controller directly. Known simplification, not yet needed
 * to fix: this hardcodes port 0 (xbox_input's XBOX_MAX_CONTROLLERS is 4,
 * but nothing in the traced ABI cheaply tells us which port `devicePtr`
 * is -- that lives in the enumeration cluster this step is not touching).
 * The game is single-player, so this is expected to be enough.
 */
void sub_001542AB(void)
{
    static uint64_t hits;
    uint32_t outBuf = MEM32(esp + 8);
    XBOX_INPUT_STATE state;
    DWORD result;

    trace_hit("poll sub_001542AB", &hits);

    result = xbox_InputGetState(0, &state);
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
 * need) and hand them to xbox_InputSetState. Same port-0 simplification as
 * the poll entry, same reasoning.
 */
void sub_0015431A(void)
{
    static uint64_t hits;
    uint32_t payload = MEM32(esp + 8);

    trace_hit("vibration sub_0015431A", &hits);

    if (payload) {
        XBOX_VIBRATION vib;
        vib.wLeftMotorSpeed  = MEM16(payload + 0x42);
        vib.wRightMotorSpeed = MEM16(payload + 0x44);
        xbox_InputSetState(0, &vib);
    }
    eax = 0;
    esp += 12;                                     /* ret 8 */
}
