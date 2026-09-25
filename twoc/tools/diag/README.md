# Diagnostics

These are debugging tools from steps 24 to 30 (notes 18.26 to 18.32). None of them are part of the build.

| File | What it does |
|---|---|
| `step24_temp_diagnostics.patch` | A temporary instrumentation patch against `e16c13b`. It has three parts. (1) `/MAP` in `CMakeLists.txt`. (2) A hang sampler thread in `main.c`. Every 3 s it suspends each host thread, unwinds its native stack, records its suspend count and CPU time, and writes `hang_sampler.log` using only `WriteFile`/`wvsprintfA`, with no CRT locks. It also dumps the stream thread's guest stack, DSOUND's lock `0x152BB0` and the voice flags. (3) Taps in `recomp_manual.c`: `KeQuerySystemTime` caller counts, and `TMP_SAFE_SUSPEND=1`, which runs `NtSuspendThread` under the stderr lock. The guest addresses in parts 2 and 3 are specific to that hang, so edit them for a new one. |
| `symbolize.py` | `python symbolize.py crash_twoc.map hang_sampler.log [first_snapshot]`. It turns the sampler's raw host frames into `sub_XXXXXXXX+0x..` names using the linker map, rebasing for ASLR from the `exe base` in each snapshot header. It prints only frames in our code. |
| `fnof.py` | `python fnof.py 2C032 1241B3 ...`. It finds the lifted function that contains each guest address, using the headers in `src/recomp/gen/recomp_*.c`. Set `RECOMP_GEN_DIR` to use a different directory. |
| `compare_runs.py` | `python compare_runs.py a.err.log b.err.log`. It compares two `run_twoc.err.log` files on behaviour and ignores timing: CdRom file-open order, `[BOOT]/[DSP]/[XPP]/[DEV]/[ICALL]/[GAME]/[APU]` event lines, and exceptions. Use it to check that an option or fix is inert when unset. |
| `step25_vm_tap.patch` | A temporary tap against `6f51e4b` (notes 18.27). It logs every guest `NtAllocateVirtualMemory` (`[0x15AB08]`) and `NtFreeVirtualMemory` (`[0x15AB0C]`) as `[TMPVM]` lines. With `TMP_FIX_FREE=1`, `MEM_RELEASE` really frees the block (`xbox_HeapFree`) and a decommit is a successful no-op. That was the leak-against-size experiment. |
| `step26_profile_sampler.patch` | A temporary time profiler against `145f8ef` (notes 18.28). It adds `/MAP` and a sampler thread in `main.c` that every 100 ms suspends each host thread, unwinds 12 native frames, and writes `prof_sampler.log` with the raw frames, each thread's CPU time, and a header with USER `DMA_PUT`/`GET`, the D3D fence pair, the PFB flush register and the vblank count. Unlike the step 24 sampler, it's meant for "where does the time go", not "where is it stuck". |
| `prof.py` | `python prof.py crash_twoc.map prof_sampler.log [t_from_ms] [t_to_ms]`. It aggregates that log: CPU use per thread, the main and NV2A ack threads' leaf functions and marker functions on their stacks, and the GPU state once a second. System DLL frames show as `<sys>`; they share one base per boot, so resolve them from another process (ctypes `GetProcAddress`) if needed. |
| `vmacct.py` | `python vmacct.py run.err.log ...`. It sums the `[TMPVM]` lines: new regions, bytes released, the live peak, and the biggest callers. |
| `fpsrun.py` | Step 27 (notes 18.29). `python fpsrun.py TAG --stderr file\|nul\|pipe [--fast] [--auto] [--seconds N] [--stop-at-menu]`. It launches the game with `run_twoc.ps1`'s variables (`--fast`/`--auto` are `-FastTitle`/`-AutoStart`) and reads guest memory from outside the process every 0.5 s, so a frame rate needs no log and works with stderr to NUL. The frame counter is the game's `0x25A660` (one `inc` per main-loop pass, `sub_0009A670`); D3D's `0x10EBD4` moves with it. `--stop-at-menu` ends the run 3 s after the menu banner (title timing). |
| `fps.py` | `python fps.py TAG ...`. It summarises `fpsrun.py` samples: menu time, the intro-load gap, fps from the end of that gap (the intro cutscene) in 10 s buckets and on average, and the time to reach cutscene frame N. |
| `memsnap.py` | Step 28 (notes 18.30). A module: `attach()` finds `crash_twoc.exe`, `read`/`u32` read guest VAs from outside the process, `snap()` copies the image/BSS, the guest heap and the contiguous arena (`0x80000000..0x84000000`). |
| `probe.py` | `python probe.py INPUT_FILE OUT_PREFIX [hold_s] [none\|up\|down\|left\|right]`. Needs a game started with `RECOMP_DEV_INPUT_FILE=INPUT_FILE`. It snapshots idle (A, A2), stick held (B, C) and released (D, E), writes `OUT_PREFIX.npz` and lists dwords that move only while the stick is held. Compare several runs, including `none`: one probe alone is swamped by particles and clocks. |
| `crashpos.py` | `python crashpos.py INPUT_FILE`. It drives the stick 5 s in each direction and prints Crash's position (player object `0x8F5120`, `+0x6C`) and state byte (`+0x172`) once a second. |
| `step28_ktap.patch` | A temporary patch against `c262d7d`: a kernel-call tap in `coop_kernel_call` (`recomp_manual.c`), armed by `ktrace=1` in the input file. It logs each guest kernel call with four args and `eax`, at most 20 per thunk slot. Slot numbers are the XBE thunk table's (file offset `0x14B000`, slot 0 = ordinal 294). Never commit it applied. |
| `drive.py` | Step 30 (notes 18.32). `python drive.py TAG SECONDS [--bridge] [--no-pbexec] [--no-input] [--intro=S] [--outdir=DIR]`. One unattended run through splash, title, menu, intro and hub: `-FastTitle -AutoStart` variables plus `RECOMP_DEV_INPUT_FILE`, Start `S` s after `INTRO1.wav` opens (default 6), then A and the stick once `THEMEL.wav` opens in the hub. Clears every other `RECOMP_*` variable. Writes `TAG.err.log`, `TAG.out.log` and `TAG.events.txt`. |
| `census.py` | Step 30. `python census.py TAG.err.log [--keys]`. It summarises a `RECOMP_D3D_BRIDGE=1` log (the `[D3DB] c` blocks from `twoc/src/d3d_bridge.c`) by phase: boot, splash, title, menu, intro-load, intro, hub-load, hub. It prints calls per swap, draw paths and primitive types, texture formats, FVFs and shader objects, the RS/TSS indices that change between draws (with XDK names), render targets, locks, copies and threads. A block that straddles a phase marker is listed but left out of the tables. |
| `discover.py` | `python discover.py TAG.disc.bin [t_from] [t_to] [min_rate] [max_rate]`. It lists guest dwords that rose at a steady rate in an `fpsrun.py --discover` dump. That's how `0x25A660` was found. |

Using the patch:

```
git apply twoc/tools/diag/step24_temp_diagnostics.patch
cmake --build twoc/build --config Release --parallel
# run, then copy twoc/build/Release/crash_twoc.map and hang_sampler.log somewhere
git apply -R twoc/tools/diag/step24_temp_diagnostics.patch   # never commit it applied
```

Release builds have no PDB. `/MAP` only adds a map file, and code generation stays the same. See the notes on host symbolization.
