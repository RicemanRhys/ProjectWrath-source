# Diagnostics

These are debugging tools from steps 24 and 25 (notes 18.26 and 18.27). None of them are part of the build.

| File | What it does |
|---|---|
| `step24_temp_diagnostics.patch` | A temporary instrumentation patch against `e16c13b`. It has three parts. (1) `/MAP` in `CMakeLists.txt`. (2) A hang sampler thread in `main.c`. Every 3 s it suspends each host thread, unwinds its native stack, records its suspend count and CPU time, and writes `hang_sampler.log` using only `WriteFile`/`wvsprintfA`, with no CRT locks. It also dumps the stream thread's guest stack, DSOUND's lock `0x152BB0` and the voice flags. (3) Taps in `recomp_manual.c`: `KeQuerySystemTime` caller counts, and `TMP_SAFE_SUSPEND=1`, which runs `NtSuspendThread` under the stderr lock. The guest addresses in parts 2 and 3 are specific to that hang, so edit them for a new one. |
| `symbolize.py` | `python symbolize.py crash_twoc.map hang_sampler.log [first_snapshot]`. It turns the sampler's raw host frames into `sub_XXXXXXXX+0x..` names using the linker map, rebasing for ASLR from the `exe base` in each snapshot header. It prints only frames in our code. |
| `fnof.py` | `python fnof.py 2C032 1241B3 ...`. It finds the lifted function that contains each guest address, using the headers in `src/recomp/gen/recomp_*.c`. Set `RECOMP_GEN_DIR` to use a different directory. |
| `compare_runs.py` | `python compare_runs.py a.err.log b.err.log`. It compares two `run_twoc.err.log` files on behaviour and ignores timing: CdRom file-open order, `[BOOT]/[DSP]/[XPP]/[DEV]/[ICALL]/[GAME]/[APU]` event lines, and exceptions. Use it to check that an option or fix is inert when unset. |
| `step25_vm_tap.patch` | A temporary tap against `6f51e4b` (notes 18.27). It logs every guest `NtAllocateVirtualMemory` (`[0x15AB08]`) and `NtFreeVirtualMemory` (`[0x15AB0C]`) as `[TMPVM]` lines. With `TMP_FIX_FREE=1`, `MEM_RELEASE` really frees the block (`xbox_HeapFree`) and a decommit is a successful no-op. That was the leak-against-size experiment. |
| `vmacct.py` | `python vmacct.py run.err.log ...`. It sums the `[TMPVM]` lines: new regions, bytes released, the live peak, and the biggest callers. |

Using the patch:

```
git apply twoc/tools/diag/step24_temp_diagnostics.patch
cmake --build twoc/build --config Release --parallel
# run, then copy twoc/build/Release/crash_twoc.map and hang_sampler.log somewhere
git apply -R twoc/tools/diag/step24_temp_diagnostics.patch   # never commit it applied
```

Release builds have no PDB. `/MAP` only adds a map file, and code generation stays the same. See the notes on host symbolization.
