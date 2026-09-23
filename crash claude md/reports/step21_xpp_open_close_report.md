# Step 21: hand-written XInputOpen/XInputClose, port-aware poll/vibration

Date: 2026-09-23. Notes section 18.23. Plan traced in 18.22.

## Result in one line

The controller now opens and real button presses reach the game's own poll
buffer every frame (A and Start were both captured). **The game did not
visibly react**: no new file loads, and no frame that earlier no-input runs
hadn't already produced. So this is not yet the "game reacts to the player"
moment. Input delivery is proven; whether the game consumes it at the screen
it's parked on is now the open question.

## What changed (`twoc/src/recomp_manual.c` only)

| Function | Was | Now |
|---|---|---|
| `sub_00154249` XInputOpen (stdcall, 4 args, `ret 0x10`) | lifted; always failed (no class-descriptor or enumerated-device entry) | returns token `0xC0DE0000 \| port` if `type == 0x1534AC` and `xbox_InputIsConnected(port)`, else 0 |
| `sub_0015429F` XInputClose (`ret 4`) | lifted; would dereference the handle and push it onto the `[0x4350A0]` free list | no-op (tokens own nothing) |
| `sub_001542AB` poll | hard-coded port 0 | port decoded from the token; non-token handle -> `0x48F`. Also logs every change in digital/analog buttons (thumbsticks excluded) |
| `sub_0015431A` vibration | hard-coded port 0 | port decoded from the token |

The token deliberately points at unmapped guest memory. Game code only ever
null-tests the handle and passes it back to these four hand-written functions
(18.22), so a stray dereference would fault at a self-identifying address. No
fault occurred.

**Port table check (done first, as planned).** `[0x1B0AC8]` read straight from
`default.xbe`'s `.data`: `{0, 1, 2, 3}` (followed by `{0,1,0,1}` at
`0x1B0AD8`, the memory-unit slot table). Its only references in the whole
binary are the two loads at `0xEB2E0`/`0xEB5C0`; nothing writes it. So
`portKey == port`.

Pipeline: `twoc\lift.ps1` -> "Excluding 11 functions" (9 before + 2 new),
4651/4662 lifted, 0 failed; `cmake --build` clean, no warnings from
`recomp_manual.c`.

## Run

Pre-check: host XInput probe showed a controller on port 0 only (ports 1-3
return 1167). Run: `run_twoc.ps1 -Seconds 120 -DumpDir dumps\step21 -Window`.
Ended by the watchdog at 125 s (exit code 3, normal), no crash, no ICALL
failure. 21 framebuffer dumps in `dumps\step21\` (gitignored).

### Stage 1: slot armed. PASS

```
[XPP] sub_000EB270 primed: xbox_InputIsConnected(0)=1
[XPP] open type=0x001534AC portKey=0 -> handle=0xC0DE0000
[XPP] sub_000EB5A0(0) returned, slot[0]=0xC0DE0000
```

First time in the project that a controller slot has been armed. No `close`
line appeared (expected: the re-insert and removal paths never fire).

### Stage 2: poll firing every frame. PASS (poll), vibration not exercised

- `poll sub_001542AB hit #1` right after `logos2.cut` loads (~t=17 s), then
  #10, #100, #1000 (~t=75 s). Never reached #10000 in 125 s. That's roughly
  17-18 polls/s, from log position against the ~10 s periodic GPU reports.
  `sub_00086D30` calls the poll unconditionally once per main-loop iteration
  (18.15), so this reads as "every frame at the game's actual loop rate."
  I didn't independently measure the loop rate to confirm it.
- Vibration: zero hits. Expected. It only runs when the game asks for rumble,
  and nothing on the current screen does.

### Stage 3: does the game respond on screen? NO VISIBLE RESPONSE

Someone pressed buttons during the run, all captured by the poll between the
3rd and 4th periodic reports (~t=30-40 s):

```
[XPP] port 0 input: buttons=0x0000 analog=FF 00 ...   A pressed
[XPP] port 0 input: buttons=0x0000 analog=00 00 ...   A released
[XPP] port 0 input: buttons=0x0010 analog=00 00 ...   Start pressed
[XPP] port 0 input: buttons=0x0000 ...                 released
[XPP] port 0 input: buttons=0x0010 ...                 Start pressed again
[XPP] port 0 input: buttons=0x0000 ...                 released
```

These values went into the game's own output buffer (slot `+4`, the 22-byte
state `sub_00036330` copies into its `+0x1A` previous-state field).

What the game did:
- **Files:** 65 `[PATH]` loads, last one `logos2.cut`, identical to 18.14-18.17.
  Nothing new loaded after the presses.
- **Frames:** 5 distinct images across the 20 numbered dumps. Sequence by
  dump time: splash pair alternating (t≈3 s) -> dark grey (t≈19 s, the only
  image not seen in 18.17's dumps, but ~10 s *before* any press) ->
  `f4d48eee` (t≈29 s) -> `b169c5ae`, the purple/white curved shape (t≈39 s and
  49 s, after the presses) -> `f4d48eee` x6 -> `b169c5ae` again (t≈119 s, with
  no press nearby). Both `f4d48eee` and `b169c5ae` also appear in the 18.17
  dumps (`dumps\`, input not working then). So nothing on screen can be
  attributed to the presses.
- **GPU:** draw/triangle growth unchanged, ~108k triangles per 10 s, 91%
  fully off-surface. Same as before.

## Reading

The fix does what it was designed to do. The XPP stack, from the slot gate
down to live host input, now works for real, on any of the 4 ports. What is
**not** established is that the game is currently on a screen that reads
input. 18.14's "idling in the hub/menu waiting on input" was an inference, and
this run weakens it: correct A/Start input arrived and changed nothing. Other
candidates, none tested:

1. The game is still in the logo/attract sequence, or stuck loading, rather
   than at an input-driven menu. There are no file loads past `logos2.cut`,
   and the visible content (spinner/cubemap-like) recurs rather than
   progressing.
2. The game reacted internally, but the NV2A 3D gap (91% of triangles
   off-surface) hides it on screen. File loads rule this out for any level or
   menu transition, but not for in-place menu changes.
3. The game uses input edges from other slot fields (`+0xC8`, `+0xD0`,
   `+0xD4`, set by other code) that this path doesn't drive.

## Suggested next step

Find out what state the main loop is in at t≈30-60 s, e.g. `RECOMP_PEEK` on
the slot's game fields (`0x427598 + 0xC8/0xD0/0xD4`) and on
`[0x23c2d8]`/`[0x23b750]` while pressing buttons. If `+0x1A..` updates but
nothing downstream consumes it, the game isn't at an input screen yet, and the
blocker is back to "why doesn't it advance past the logos."
