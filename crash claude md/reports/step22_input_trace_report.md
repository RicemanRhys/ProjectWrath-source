# Step 22: where the input goes -- the title screen is frozen on a missing audio stream

Date: 2026-09-23. Notes section 18.24. Trace/diagnostic only: every change
listed here was temporary and has been reverted (source restored to commit
`b9323b8`, generated code re-lifted, clean rebuild).

## Answer in one paragraph

The slot fields **do** update on a press: `+0xC8` = buttons held this frame
(A -> `0x40`, Start -> `0x800`), `+0xCC` = last frame's, `+0xD0` = just
pressed. So this is not candidate 1 as framed. The game is on its title
screen, but no menu is open to consume input. The title animation only opens
the main menu (menu 0) when it finishes. It never finishes because it's
frozen at speed 0: each frame the game sets its speed to 0 until the title
music's DSOUND **stream** reports "playing", and that stream object has no
DSOUND interface, so it never does. **Proven by experiment:**
1. Forcing that one status query to "playing" made the animation run.
2. The main menu then opened on its own.
3. Two injected presses drove the real menu: **Start opened menu 4 and A
   picked "start game."**
4. The game then loaded a new level and read `levels\b\intro\introchars.dat`,
   a file never loaded before (77 file loads vs the 65 ceiling).
5. It then crashed with an access violation during that level's setup.

This is the first time the game has responded to input with a state change,
but the input was synthetic and it needed a temporary audio workaround. On
screen, nothing visible changed (NV2A gap).

## Method

- `RECOMP_PEEK` on `0x427598+0xC8/+0xD0/+0xD4`, `[0x23c2d8]`, `[0x23b750]`
  as asked. It only samples at the ~10 s GPU reports, far too coarse for a
  button press, so I added:
- a temporary per-frame diff inside the hand-written poll. `outBuf == slot+4`,
  so the poll knows the slot. It logged every change in slot `+0x30..+0xF0`
  and in a growing list of globals, plus timestamps.
- **Synthetic presses.** The first run of this step had no human presses at
  all, and the controller (wireless) later went offline entirely. So the
  temporary poll code injected A/Start on a fixed schedule (hold 0.5 s every
  5 s from 20 s). Injected presses go through exactly the same path as real
  ones: host state -> poll -> slot buffer -> game.
- Temporary prints in the generated code (`sub_0006AF20` set-menu,
  `sub_0005D4B0` stream status), and in the final run a temporary force of one
  status query plus a faked "controller connected" on port 0.

## Findings, in order

**1. The slot fields update (per-frame diff, injected presses):**

```
[XPP] port 0 input: buttons=0x0000 analog=FF ...     (A down)
f342 slot+0xC8 00000000 -> 00000040
f343 slot+0xCC 00000000 -> 00000040   slot+0xD0 00000000 -> 00000040
f345 slot+0xD0 00000040 -> 00000000
[XPP] port 0 input: buttons=0x0010 ...               (Start down)
f514 slot+0xC8 00000000 -> 00000800   ... +0xD0 -> 00000800
```
`[0x23c2d8]` stays `0x427598` and `[0x23b750]` stays 0. Also measured: the
main loop runs at ~17.6 frames/s (300 polls per ~17 s).

**2. Where the game is.** `sub_00087BF0` is the game's main function
(`[0x19c068]` = level, 0x25 here). It calls the level loop `sub_0002D950(0)`
with `[0x17be64]=0`, the title/intro run. Consumers of the slot fields in
that loop:
- `0x2DB67` `test [slot+0xD0], 0x840` (A|Start skip): only when
  `[0x17be64] != 0`. Off here.
- `sub_00086D30` Start -> pause (menu 5): only when `[0x17be64] == -1`
  (in a level). Off here.
- `sub_0006E130(menu=0x5607C0, slot)`, the 10.8 KB menu handler, reads
  `+0xC8/+0xD0` (`0x840` accept, `0x1000/0x4000` up/down ...). **But it
  returns immediately when menu `+0x6E == 0xFF` (no menu open).**

**3. Menu timeline** (set-menu trace, `sub_0006AF20(menu, id, ...)`):
```
set_menu id=2  cur=FF ret=0002C55B   title setup (sub_0002C480) opens menu 2
   fade-in [0x1a097c] 0xFF -> 0 by frame 65
set_menu id=-1 cur=02 ret=0006F372   frame 68: menu 2's own case closes itself, no input
   then plays sound [0x8f4e80]=0xAC (sub_000B50A0) -- the title music
```
After that the menu stays `0xFF`, so all input is ignored, and the level
loop's exit (fade-out `[0x1a097c]` back to 0xFF) never starts.

**4. What should open the next menu.** `sub_0002C020` (title setup) creates
the title animation object `[0x1ba928]` and registers `sub_0002BCC0` as its
end-of-animation callback (`sub_00078AB0` stores it at `obj+0x90`).
`sub_0002BCC0` = `set_menu(0)`, the main menu. The animation engine (`0x7AA35`)
calls `obj+0x90` once `obj+0x70` (time) >= `[[obj+0x58]+8]` (end = 961.0);
time advances by `obj+0x74` (speed) per update.

**5. Why it never finishes.** Peeks: speed `+0x74 = 0`, time stuck at 300.0.
Each frame `sub_0002C330` (called from the level loop) does: while
`[0x1baa2c]` (title sound started) is set, ask `sub_000AA590(4)` -> `sub_000EC770`
-> `sub_000B9370` -> `sub_0005D4B0`, "is sound channel 4 playing?"; if not,
**set the animation speed to 0**. Channel 4 maps at runtime to stream voice 1
of the sound manager at `0x427A58` (`0x427A58+0xA480+1*0x44 = 0x431F1C`).
`sub_0005D4B0` returns "playing" only if the stream's DSOUND interface
(`+4`) exists and reports playing. **All six stream voices had `+4 == 0` at
every check** (both init-time polls, and every title-time query of voice 1). So the title music stream never gets a DSOUND stream object, and
the title waits for it forever.

**6. Proof (temporary patch, then reverted).** `sub_0005D4B0` was patched to
return "playing" only for the title query (return address `0xB939B`, only
while `[0x1baa2c]==1`). A first, broader version (any call on that voice)
hung startup: audio init polls the same function waiting for streams to
*stop*. Result, 480 s run, dumps in `dumps\step22f\`:
- speed became 0.5 and time advanced ~2.2 units/s (300 -> 961 takes ~300 s
  here vs ~22 s at 60 fps on hardware; the loop is ~17.6 fps and the
  animation is clearly not stepping every frame);
- `set_menu id=0 ret=0002BCDB`: **main menu opened by the real callback**;
- injected Start (log line 17801) -> `set_menu id=4 ret=0006F10F` on the very
  next line (menu handler's accept path);
- injected A (line 18090) -> `set_menu id=-1 ret=0006F423` next line: the
  "start game" branch (`[0x8f4db4]=1`, fade-out);
- the level loop exited, the next level began loading: `particle.raw`,
  `intro.ptl`, `general.ptl`, `hubcubemap.dds` ..., then **new:
  `levels\b\intro\introchars.dat`** (77 file loads, the old ceiling was 65);
- then **crash**: access violation reading guest `0xFFFFDFC8` (a null base
  minus `0x2038`), guest stack `0x17BBF8 <- 0x958E9 <- 0x2BDB0 (sub_0002BD80)
  <- 0x2C18B (sub_0002C020) <- 0x2DA42 (level loop's level-load)`.
- On screen: only 3 frames not seen in earlier runs, all variants of the
  spiral pattern (the title animation now moving). No visible menu, as
  expected with the NV2A 3D gap.

**Dead end checked:** the input-record/replay buffer (`[0x1baba8] =
0x4F26E0`) is idle (mode `+4 = 0`), so the slot's `+0xD4 = 4` isn't demo
playback.

## What this means

- **Input delivery (18.23) is correct and now proven to drive the game's own
  menu logic.** It stays committed (`b9323b8`).
- The real blocker past `logos2.cut` is **audio, not input**: the DSOUND
  stream voices never get stream objects. It's the same library as the
  18.10-18.13 3D-listener gap (whose `*(0x427A58)` is the first dword of this
  same sound manager), but the link between the two isn't proven.
- Two more issues are queued behind it: the new-level crash above, and the
  title animation running roughly 14x slower than on hardware (estimated
  from 0.5 per frame at 60 fps, not measured on hardware).

## Suggested next steps

1. **Real fix:** find why the sound manager's stream voices never get a
   DSOUND stream (probably `DirectSoundCreateStream` or equivalent failing or
   never called in this recomp's DSOUND path). A stream that really exists
   and reports "playing" unblocks the title for real.
2. **Stopgap, if (1) is big:** a narrow hand-written override so the title
   query (`sub_0005D4B0` via `sub_000EC770`) reports "playing" while a title
   sound is active. It's the same patch used here. It's unfaithful, but it
   reaches the main menu, and it must *not* affect audio init's
   wait-for-stop polls.
3. Then the level-setup crash (fault `0xFFFFDFC8` under `sub_0002BD80`).
