# Robustness sweep (step 73, notes 18.76)

Unattended level/scenario runs, the endurance cycle and their summaries. Copy this folder to a scratch folder and
run it from there: the scripts write `runs/` next to themselves and expect two more folders beside them.

Set-up (once, in the copy):

- `exe/crash_twoc.exe`: a copy of `twoc/build/Release/crash_twoc.exe`. No `crash_twoc.ini` beside it, so the
  user's settings are untouched.
- `root/game_files/`: `default.xbe`, `crash_analysis.json`, `TDATA/` and `UDATA/` copied from the repo's
  `game_files`, and `Crashdat` as a junction (`mklink /J root\game_files\Crashdat <repo>\game_files\Crashdat`).
  `launch.py` runs the game with `root/` as its working directory, so saves (`UDATA\56550003`) land there, never
  in the repo.
- `WGCGRAB`: the path of `wgcgrab.exe` (built by `../wgcgrab_build.bat`) if not the default in `act.py`/`ctl.py`/`play.py`.
- Git Bash for the `.sh` files; the repo's `.venv` Python (memsnap needs numpy).

| File | What it does |
|---|---|
| `launch.py TAG SECS [--exe=] [--root=] [ENV=VAL...]` | Host-only launch (fast title, input file port 0), logs to `runs/TAG.*`. |
| `newgame2.py TAG` | Menu grab, NEW GAME named WOMBAT (everything open), intro grab, skip to the hub. |
| `gopad2.py TAG LEVEL ROOM:PAD` | From the hub spawn: set that pad's level byte (`0x19A7F0`), walk onto it (placed on it if the walk stalls). |
| `gopad6.py TAG PAD [LEVEL]` | Room 6 (bonus) the natural way: the central lift upstairs, the gate, pad PAD. |
| `play.py TAG SECS [GRABEVERY]` | Scripted play (run, turn, jump, spin, crouch), samples the frame counter `0x25A660` and Crash's position every 0.5 s, grabs `TAG_pNN`; prints a summary line (frames/s, longest stall, distance, crash lines, alive). |
| `sw.sh TAG LEVEL ROOM:PAD` | One level: launch, new game, gopad, 20 s, grab, `play.py` 60 s, Start + grab, kill. `PASS=1` 16:9 + Frame rate: Display; `PASS=2` 4:3, 60. `PRE="script args"` runs after the new game. |
| `sx.sh TAG LEVEL ROOM:PAD "ACT STEPS"` | A scenario: as `sw.sh`, then `act.py` steps (`pad:`, `wait:`, `grab:`, `burst:NAME:N:GAP`, `x:script:args`). LEVEL `u` = room 6 by the lift. |
| `m.sh TAG cb\|av\|up` | Step 75 (notes 18.78): where a frame's time goes with Frame rate: Display at 4 sub-frames (adaptive off), in cast_bug's start, avalanch or the upstairs room, 16:9, HUD at the edges, `RS` (default native). Needs an `exe/` built with `../step75_frame_profile.patch` (and `/MAP` for `SAMP=1`, which runs `hs2.py` 15 s). `STEPS="x:play.py:TAG:30"` plays meanwhile. Writes `runs/TAG.fprof.csv` (+ `.gpu`); `fpsum.py CSV FROM TO` summarises it. `wr.py TAG TEXT` writes `runs/TAG.trig` (`_` for spaces) for the trigger-file switches. |
| `glide.py TAG SECS Z0 Z1 SPEED`, `glideb.py` | Step 76 (notes 18.79): move Crash back and forth along z by writing his position (no input), so the camera moves as in play and nobody has to play; the same every run. cast_bug: `-3 7 3`. `glideb.py` starts it in the background (for `SAMP=1`, so `hs2.py` samples while he glides). |
| `tab.py TAG...` | Step 76: the step-75 table (game, mirror, recording, pairing, each replay, presents, frame, busy) per run, over the last 1000 swaps less 30, 4-sub-frame frames only. |
| `meas.sh` | Step 76: the measurement set interleaved by exe (`../B`, `../A1`, `../A` beside this folder, each a `step75_frame_profile.patch` build): cast_bug start, cast_bug gliding, avalanch, upstairs, B A1 A then A A1 B. |
| `chk.sh PFX EXEDIR`, `ichk.sh PFX EXEDIR` | Step 76: with a `../step76_old_way_check*.patch` build, `REPLAY_CHECK` runs (old way at once, new way replayed) and `TMP_ICHECK` runs (every interpolated sub-frame both ways) in cast_bug (gliding), avalanch and upstairs; prints the `frames compared, N differ` lines. |
| `batch.sh PREFIX main.txt`, `batchx.sh PREFIX scen.txt` | Run a list (`LEVEL ROOM:PAD` lines; `TAG|LEVEL|ROOM:PAD|ACT|PRE|WAIT|SECS` lines). `scen.txt`: bosses with their intro bit cleared, room 6, the pause pages, game over, both endings. |
| `endur.sh TAG` + `cycle.py`, `procmon.py` | Endurance: new game, then for `MIN` minutes a level on pad 1:1, 120 s of play, pause > WARP ROOM > YES, 30 s in the hub; `procmon.py` writes handles, threads, private bytes and the latest stat lines every 60 s to `runs/TAG.mon.txt`; `monan.py` tabulates it. |
| `summ.py TAG...` (or `"a*"`) | One line a run: levels loaded, play summary, `[CRASH` count, exit code, interpolation. |
| `overview.py`, `cs.py`, `stack.py`, `sh.sh`, `ov1.txt` | Contact sheets. |
| `upst.py TAG` | Step 74: ride the hub's central lift upstairs (the heaviest room of steps 60-62) and stay; for `sx.sh` as `PRE="upst.py TAG"` with LEVEL `-`. |
| `adsum.py TAG...` (or `"m*"`) | Step 74: per run, the level, play fps, and after the level load the game frames by sub-frames allowed (4/2/1), the changes and their direction, mean ms apart and busy (`[D3DB] host interpolation sub-frames` lines). |
| `rmw.py`, `peek.py`, `poke.py` | Guest memory read-modify-write, read, write. |
| `t2.sh EXEDIR PFX`, `t3.sh`, `t4.sh`, `t180.sh` | Step 77 (notes 18.80): the frame-rate targets at 240 Hz (upstairs standing: 120, 90, 90 with `RECOMP_VRR=1` and `=measure`, 144, display; cast_bug gliding with sub-frame dumps), the adaptive ladder (cast_bug at display and 120, upstairs), avalanch + the overlay + `id.sh`, and the display at 180 Hz. They expect exe copies in `exe_item2`, `exe_item3` and (with `../step77_camera_log.patch`) `exe_cam` beside them. |
| `glsub.py TAG SECS Z0 Z1 SPEED AT N` | Step 77: `glide.py`, and AT s into it the sub-frame dump trigger (`runs/TAG/sub.trig`, for `RECOMP_D3D_BRIDGE_SUBDUMP=@` with forward-slash paths: a backslash before the file name was lost on the way in step 77's first try). |
| `subev.py DIR PREFIX` | Step 77: the shift of the picture between consecutive sub-frame dumps by phase correlation (`SUBEV_TOP=1`: the top quarter, the walls, away from the fruit and Coco, which bob and animate). Read with care: the glide's own speed varies from game frame to game frame. |
| `camev.py ERRLOG [N]` | Step 77: a `step77_camera_log.patch` run's camera log: off-the-line error and motion per present. |
| `esc.py` | Step 77: posts Esc to the bridge window (the settings overlay); grab it with act.py's `grab:`, at `RS=2560x1440` (at 1280x720 the 225% overlay doesn't fit). |
| `id.sh A B`, `idcmp.sh A B` | Steps 76-77: identity, `drive.py` 150 s bridge off and host-only with `sw/A` and `sw/B`'s exes, then CdRom opens, `[PATH]`, exits, host device and pacing lines and the first 8 host dumps compared (edit `M`, the scratch folder, first). |

Traps: a race level (`west_a`) needs throttle the play script doesn't press; random play reaches game over (WOMBAT
starts with 4 lives) and from the title or hub can walk onto other pads; cycle.py's pad trigger sometimes needs a
second try ("loading NOTHING"); story cutscenes (level 40) can't be forced by poking the cutscene id
(`0x17BED4`): `sub_0002CC30` resets it at every level change, and level 40 with id -1 hangs (an artefact of the
poke, not reachable in play).
