# Developing Project Wrath

How the port is put together and where things are. Building it: `CONTRIBUTING.md`.

## What it is

Project Wrath runs the original Xbox program of Crash Bandicoot: The Wrath of Cortex (USA) on Windows by **static
recompilation**: the toolkit's pipeline (`xboxrecomp/tools`) disassembles the player's own `default.xbe`, finds its
functions and lifts every one to C (`twoc/src/recomp/gen/`, generated, never committed); that C is compiled with the
toolkit's runtime (an Xbox kernel replacement, memory layout, threads, files) into one Windows program. On top of
that, this repository adds what the game needs to run well on a PC.

## The repository

| Path | What |
|---|---|
| `twoc/src/main.c` | Start-up: the data folder, the first-run setup, the settings, the audio DLL, the XBE loaded and mapped, the kernel, the game's entry point. |
| `twoc/src/recomp_manual.c` | Hand-written versions of game functions the lifter can't translate faithfully, and taps on the game's kernel calls (cooperative thread suspension, the heap and pool, the save files). |
| `twoc/src/d3d_bridge.c` | The mirror bridge: the game's own Direct3D 8 code keeps running; wrappers at its entry points record each draw and hand it to the toolkit's D3D8-to-D3D11 translator. |
| `twoc/src/d3d_host.c` | The host side: the window and swap chain, widescreen, render size, frame pacing, frames drawn between the game's (interpolation), anti-aliasing (`aa.c`), ambient occlusion (`ao.c`), the render thread, the overlay's frame. |
| `twoc/src/overlay.cpp`, `settings.c`, `controls.c`, `pads.c` | The F1 menu (Dear ImGui), the settings file, the controller and keyboard mapping, every controller through SDL 3. |
| `twoc/src/setup.c`, `setup_ui.cpp`, `xdvdfs.c` | The first-run setup: reads the player's disc image (XDVDFS) or folder, checks every file against `twoc/data/disc_usa.tsv`, copies safely (journal, verify, swap, recovery). |
| `twoc/src/saves.c`, `userdata.c` | The save's backups; the per-user data folder (`%LOCALAPPDATA%\ProjectWrath`) and the save opens sent there. |
| `twoc/apu_dll/` | The emulated audio chip (from xemu, LGPL) built as `xemu_apu.dll`. |
| `twoc/tools/package/` | The Share package (`make_package.ps1 -Target Share`), its audit, the audio DLL's source zip, the release (`make_release.ps1`). |
| `twoc/lift.ps1`, `analyze.ps1`, `tools/lift_*.py`, `tools/wrap_callers.py` | Regenerating the translated code from `default.xbe`. |
| `toolkit/` | The toolkit's pinned upstream commit and this project's patches (`docs/TOOLKIT.md`). |

## Things worth knowing

- **The generated code is the game's program**, so it stays out of the repository; every developer regenerates it
  from their own disc. A change to the lifter or its fixes means regenerating (`lift.ps1`).
- **The lifter's two fixes** (`tools/lift_flag_join.py`, `tools/lift_frndint.py`): a conditional jump after a
  compare of one width and a test of another, which the lifter turned into a never-taken fallback (the bosses ended
  early); and `frndint`, which the lifter rounded to nearest whatever the FPU control word said (the CRT's `floor()`
  then picked the wrong animation keys: models and pads glitched).
- **Threads** are host threads running guest code; the game suspends its own threads (`NtSuspendThread`), which the
  runtime makes cooperative: a suspended thread parks only at a kernel-call boundary and only when it holds no guest
  lock (deadlocks at level loads otherwise).
- **Rendering is a mirror**: the game's D3D code still builds its push buffer; the bridge reads the same state and
  draws it on the host. Frames between the game's 60 a second are the recorded frame drawn again with the camera and
  objects interpolated.
- **Audio** is the toolkit's emulated MCPX APU (xemu's), driven by the game's own DirectSound code; it is a DLL so the
  LGPL part can be rebuilt and replaced by anyone.
- **Saves**: the game writes `UDATA\56550003\4D5C926D6357\crash.sav` (every slot in one file). The packaged
  `ProjectWrath.exe` keeps it in `%LOCALAPPDATA%\ProjectWrath\saves`; the developer build (`crash_twoc.exe`) keeps it
  in `game_files\UDATA` unless `RECOMP_SAVES_OUTSIDE=1`; `portable.txt` beside the exe or `RECOMP_SAVES_IN_PACKAGE=1`
  keeps everything in the game's folder; `RECOMP_DATA_DIR` picks another data folder (tests).
- **Names follow the exe's file name**: `crash_twoc.exe` (the developer's) writes `run_twoc.*.log` and
  `crash_twoc.ini` with the old window title; any other name (the package's `ProjectWrath.exe`) uses Project Wrath's.
- **Logs**: the err log has one line per notable event with a `[TAG]` (`[SAVES]`, `[SAVE]`, `[SETUP]`, `[CFG]`,
  `[D3DB]`, `[APU-OUT]`, `[PATH]` for every file the game opens). `run_twoc.ps1` runs the developer build with the
  usual switches; `-FastTitle -AutoStart` reaches the game in about half a minute.

## Not in this repository

The private repository also holds the project's working notes (the history of every step, measurements and
playtests, with disassembly excerpts and addresses), the test harness (`twoc/tools/diag`: scripted play by level and
position, memory probes, frame and audio analysers, captures) and the owner's device packages. They are kept private
because they quote the game's program or the owner's own setup; nothing in them is needed to build or run the game.
