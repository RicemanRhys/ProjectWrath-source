# Step 23: DSOUND streams fixed for real -- the title opens the main menu on its own and a real controller drives it

Date: 2026-09-23. Notes section 18.25. Code change committed as `dc99be3`;
all diagnostics listed below were temporary and have been reverted (source =
`dc99be3`, re-lifted, clean rebuild).

## Answer in one paragraph

The stream voices never got a DSOUND stream because **DirectSound itself never
initialised**: `DirectSoundCreate` (`sub_00137999`, called at the top of the
sound manager's init `sub_000B99D0`) returned `0x88780078` =
`DSERR_NODRIVER`, because the AC'97 codec-ready bit at `0xFEC00130` was never
set (`sub_0013F5F8`). The init bails at its first step, so no voices, no
stream thread (`sub_0005DA30`), and `*(0x427A58)` stays null. **That null is
the same pointer as the 18.10-18.13 "3D listener" gap: it is the manager's
`IDirectSound*`, written by `DirectSoundCreate` through `this`, which is why
no literal-address write to it exists.** One root cause for both. The runtime
already had the fix behind an opt-in variable (`RECOMP_AC97_READY`, which is
section 15.1's original plan) that `run_twoc.ps1` never set. Setting it
exposed two more small gaps, both fixed in `twoc/`. After that the title
music stream really exists, reports "playing", the title animation runs to
the end, **the real title-end callback opens the main menu, and a real wired
controller drove the menu (Start/A -> menu 4 -> "start game") twice**. No
stopgap was needed, so step 2 was skipped.

The level-setup crash was **not** reached: with real audio there is now a
new hang earlier in the same level load, in DSOUND's stream-stop path. It is
described below. The crash's null base is therefore not yet known (see
"Crash" at the end).

## Step 1: why the streams were never created

Method: temporary prints in the generated C (stream thread, stream open,
`DirectSoundCreate`, the title's status query), 40-120 s runs.

1. `sub_000B99D0` (manager init, `this=0x427A58`, from `0xAA1F5`):
   `sub_00137A06(0, this, 0)` = DirectSoundCreate. Its callee
   `sub_00137999` returned **`0x88780078` DSERR_NODRIVER**, and `[this]` stayed 0.
   The failure returns before the voice setup loops and before
   `CreateThread(sub_0005DA30, this+0xA480)` at `0xB9AE9`. So the stream
   thread never existed: `[STRM] thread start` never printed, and only 2
   guest threads were ever created.
2. The NODRIVER comes from `sub_0013FD2B` -> `sub_0013F5F8`, the AC'97 cold
   reset: set `0xFEC0012C` bit 1, then poll `0xFEC00130` for bit 8
   ("primary codec ready") 1000 x 20 us.
3. `xbox_memory_layout.c` already sets that bit and traps the APU's registers
   into the emulated APU (`mcpx_apu_init_standalone`, wired in `main.c`)
   when `RECOMP_AC97_READY` is set. Its own comment says it was left opt-in
   because the DSP handshake behind it was unanswered.

### Fix, three parts, all `twoc/`

| Change | Why |
|---|---|
| `run_twoc.ps1`: `RECOMP_AC97_READY=1` | Codec reported ready; DirectSoundCreate returns S_OK, `[0x427A58]` = `0x0178103C`. |
| `sub_001391DE` hand-written (`recomp_manual.c`) | `DirectSoundDownloadEffectsImage` (`sub_00136605`) writes GP DSP command 3 to `scratch+0x810` (`0x80BC8810`) and spins `cmp [ebx],0 / jne` (`0x139345`) until the DSP clears it. The emulated APU has no DSP (stubbed passthrough), so audio init hung forever there. The override is the lifted body verbatim, except that the spin is replaced by the DSP's answer: the word is cleared once. It is the only such wait in DSOUND. The effects image (reverb and the like) is not run. |
| `main.c`: APU `+0x30000..+0x80000` (GP/EP DSP memory) back to plain RAM | DSOUND then block-copies DSP X-memory (`0xFE830200`) with `rep movsd`, which is lifted to `memcpy`. The MMIO trap's decoder can't step AVX `vmovdqu` (`[APU] MMIO decode fail ... C5 FE 6F`), so it hit an access violation. `apu_core.c` ignores GP/EP entirely (writes dropped, reads 0), so plain memory is no less faithful. |

Also committed: a trace wrapper on the title-end callback `sub_0002BCC0`
(reached indirectly, so via `recomp_lookup_manual`, not excluded from the
lift) that prints `[GAME] MAIN MENU OPEN -- press Start, then A` to stdout
and stderr.

Alternative noted, not used: the runtime has `RECOMP_APU_DSP_ACK=<addr>`
(`apu_dsp.c`), which clears given dwords every APU frame. It would work with
`0x80BC8810` (the same address in every run), but it depends on a heap
address and keeps clearing a dword that DSOUND owns for the whole session.
The override clears it exactly once, at the moment of the handshake.

### Result (`[STRM]` trace)

```
[DSI] DirectSoundCreate hr=0x00000000 [this]=0x0178103C
[DSP] GP command 3 at 0x80BC8810 acknowledged (no DSP; effects image not run)
[STRM] thread start arg=0x00431ED8
[STRM] file=d:\Crashdat\SFX\VCE_ENG\LOGOS.wav
[STRM] media-object hr=0x00000000
[STRM] CreateStream hr=0x00000000 voice+4=0x03A1C334
[STRM] title query #1 -> PLAYING
```

18.13's guard (`sub_000B9890`) is now inert (the pointer is real) and stays
as a safety net for runs without the variable.

## Step 3: real controller, main menu, next level

Clean re-lift (4650 translated, 0 failed, 12 hand-written exclusions, was
11) and rebuild.

| Run | Input | Result |
|---|---|---|
| 1 (900 s, window, `dumps\step23`) | human, wired pad port 0 | Banner at ~745 s. Press 1 -> `set_menu id=4 ret=0x6F10F`; press 2 -> `set_menu id=-1 ret=0x6F423` ("start game"). Presses landed ~50 s before the end, so the fade-out didn't finish. |
| 2 (1500 s, window, `dumps\step23b`) | human | Same menu sequence, earlier. **Next level began loading**: `particle.raw`, `intro.ptl`, `general.ptl`, `hubcubemap.dds` (not found, as before), `Droplet1.dds`, `WaterCubeMap.dds`. Then all progress stopped. |
| 3-5 (`dumps\step23c..e`) | injected A at +5 s / +20 s after the banner (temporary) | Same stop, instrumented (below). |

Notes:
- Both human presses logged as A (`analog[0]=FF`); the menu accepts A or Start.
- The title takes **~12.5 min** to reach the menu. That is slower than 18.24's
  forced-status run: the animation is now paced by a real stream, and the
  ~14x slow title issue remains.
- The runtime watchdog (`RECOMP_WATCHDOG_SECS`) never printed in any of
  these long runs; the script's hard kill ended them. Not investigated.

## New blocker: DSOUND stream-stop never completes

Evidence (runs 3-5, a temporary sampler thread writing via `WriteFile` so it
can't be blocked by the CRT; `[CS]` contention logs; kernel summaries):

- "Start game" -> `sub_0002C020` (level setup) -> `sub_000EC870` =
  `sub_000B9180(0x427A58)`, the sound manager's "wait until every voice and
  stream has stopped" loop. It first asks the stream thread to stop stream 1
  (the title music): stream 1 ends as `obj=03A1C704 play=00 stop=01`.
- The **stream thread** (stack top `0x01B4EFF0`, `esp=0x01B4EF34`, only 0xBC
  deep) then calls `KeQuerySystemTime` (ordinal 128) **~370-430 million
  times**, while holding DSOUND's global lock `0x152BB0` (taken at
  `0x135B7D`, reached from its stop/release path `0x5DA88 -> sub_0005D420 ->
  0x13628D`).
- The **main thread** blocks on that lock inside the wait loop (`0xB91B7 ->
  sub_000B8F40 -> 0x1366B9`). Its registers and the global indirect-call
  count freeze. The loop itself never iterates past pass 1, so a `Sleep(1)`
  yield in it (run 4) changes nothing.
- A time-based spin in DSOUND with the lock held fits "wait for the hardware
  voice to go idle". `sub_0013B682` (`while (voice->flags & 0x8000)
  sub_0013ADB8(voice)`, with a 5 s forced-off fallback) was the first
  candidate, but a temporary 6 s breaker in that loop (run 5) **never fired**,
  so the spin is elsewhere. Other DSOUND `KeQuerySystemTime` sites:
  `0x139B97` (`sub_00139B74`, timed-event list walk) and `0x139FA9`
  (`sub_00139F89`). Not yet pinned.
- Probable class, not proven: DSOUND's stop path waits for something the
  emulated APU never does, most likely a voice-off notification or interrupt.
  That is the "DSOUND interrupt-servicing gap" first suspected in 18.11.
  18.13 set it aside because DSOUND never initialised then.

**Next action:** sample the *stream thread's* guest stack (the sampler only
read the main thread's TLS registers). Its stack is in guest RAM at
`0x01B4EF34..0x01B4EFF0` and the address is the same every run, so a sampler
that dumps that range names the spinning function at once. Then decide
between answering what it waits for (an APU voice-off or interrupt) and a
narrow override like the DSP ack.

## Crash (`0xFFFFDFC8` under `sub_0002BD80`): not reached, partial static read

The new hang happens before the code that crashed in 18.24 runs. What can be
said from 18.24's log plus the disassembly:

- `sub_0002C020` case with `[0x1BAA84]=1` (the intro level) copies
  `levels\b\intro\introchars.dat` to `0x8F4E00` (`0x2C177`) and calls
  `sub_0002BD80(eax=0x17BBF8, arg=0x17BAA8)` (`0x2C186`), the intro
  character setup.
- `sub_0002BD80` then calls `sub_000982C0(0x8F4E00, 0, 0)` (load that file),
  returning to `0x2BDB0`: the innermost frame 18.24 recorded that is certainly
  live. `0x958E9` (after the 2nd `sub_0009E300` in `sub_000958D0`) was
  probably stale: the scan over-reports.
- So the fault is most likely inside the `introchars.dat` load
  (`sub_000982C0` or `sub_00097560`/`sub_0002B850`, which consume its
  result), reading at "null - 0x2038". No instruction in `.text` has a literal
  `-0x2038`/`+0xFFFFDFC8` displacement, so the offset is computed (an index
  into a null table). **Which base should be non-null is not determined
  yet**. 18.24 didn't record the fault registers, and the crash wasn't reached
  this time. Once the stream-stop hang is past, the existing VEH dump
  (registers + guest stack) gives it directly.

## Reusable temporary diagnostics (not committed)

Synthetic presses, for runs with nobody at the pad. Set a timestamp in
`traced_sub_0002BCC0` (`if (!g_diag_menu_ms) g_diag_menu_ms =
GetTickCount();`), then in `sub_001542AB` just before the `if (outBuf)` copy:

```c
if (g_diag_menu_ms) {
    DWORD dt = GetTickCount() - g_diag_menu_ms;
    if ((dt >= 5000 && dt < 5500) || (dt >= 20000 && dt < 20500)) {
        result = ERROR_SUCCESS;
        state.Gamepad.bAnalogButtons[0] = 0xFF;   /* A */
    }
}
```

Hang sampler that can't be blocked by the CRT: a host thread started next to
`xbox_WatchdogStart()`, handed `&g_esp`/`&g_eax`... from the guest thread,
writing esp/regs, `g_icall_count`, the `g_icall_trace` ring, code-range words
from the guest stack and the stream-voice flags (`0x431ED8 + i*0x44`: `+4`
obj, `+0x24` playing, `+0x38` stop, `+0x39` start) to `hang_sampler.log` with
`WriteFile` every 5 s. For the open blocker, extend it to dump the stream
thread's stack range `0x01B4EF34..0x01B4EFF0`.

## Files changed (committed, `dc99be3`)

- `twoc/run_twoc.ps1`: sets `RECOMP_AC97_READY`.
- `twoc/src/main.c`: GP/EP DSP windows left as plain memory.
- `twoc/src/recomp_manual.c`: hand-written `sub_001391DE` (DSP handshake),
  `sub_0002BCC0` menu banner, 18.13 comment updated.
- Notes 18.25 and "Start here tomorrow"; this report.

Logs from runs 1-5 are in the session scratch folder only; the framebuffer
dumps are in `dumps\step23*` (gitignored).
