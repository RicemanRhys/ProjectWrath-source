# What the public source repository may contain

Step 110, Part 1 (notes 18.113). An inventory of everything tracked in this private repository's `twoc/` and in the
toolkit's local commits, sorted by how much of it comes from the game's own program or data. Not legal advice: the
groups describe what each file *is*; whether a group may be published is for the owner (and a lawyer:
`docs/LAWYER_QUESTIONS.md`). `tools/make_public_tree.ps1` exports exactly the "in" column below, and
`tools/audit_public_tree.ps1` refuses anything of group (a).

Sizes are in bytes at the private repository's step-110 state; counts of game references come from
`tools/diag/step110/gamescan.py` (functions named by address `sub_XXXXXXXX`, constants in the game image's range
0x10000-0x200000, x86 instruction lines, SHA-256 values).

## (a) Generated or disassembled from the game: OUT

| Path | Size | What it is |
|---|---:|---|
| `twoc/src/recomp/gen/` (untracked, never committed) | ~33 MB, 9 files | The lifter's C translation of `default.xbe`'s machine code (every game function as C, the dispatch table, the types). The game's program in another language. |
| `twoc/docs/option_b/d3d99_asm.txt` | 174,391 | A disassembly listing of the game's statically linked Direct3D library (99 entry points: addresses, instructions, callers). |
| `twoc/docs/option_b/table.json` | 24,449 | The same 99 entry points as a table: address, callers, call counts, the push-buffer methods each writes. |
| `twoc/docs/option_b/tables.md`, `step29_option_b_plan.md` | 11,453; 39,530 | The analysis written up from those listings (quotes addresses and instructions). |
| `twoc/docs/option_b/{d3dlib,dump,entries,table}.py` | 13,397 together | The scripts that produced the listings from the XBE. |
| `twoc/seed_functions.json` | 5,276 | 52 function start addresses in the game image, found by hand (functions reached only by pointer). A function listing of the game, though only numbers. **Needed to regenerate the code** (see below): out by default, `make_public_tree.ps1 -IncludeSeeds` puts it in if the owner decides so. |

## (b) Hand-written, game-specific: IN only where the build needs it

| Path | Size | What it contains | Why a public repository needs it |
|---|---:|---|---|
| `twoc/src/recomp_manual.c` | 214,272 | Hand-written replacements for 48 of the game's functions, wrappers and references to 117 in all (224 mentions by address, 91 image constants): the kernel-call taps (cooperative suspension, the heap, the pool, the save taps), the CRT fixes (memmove, frndint, floor), the D3D and input entry points, the HUD anchoring, the boss-script join. Original code written against the game's addresses; no game code copied. | The build: the generated code calls these by name; without them it doesn't link. |
| `twoc/src/d3d_bridge.c`, `d3d_bridge.h` | 283,857; 6,060 | The mirror bridge: wrappers around the game's D3D entry points (by address) that read the game's D3D state at known offsets and draw it with the host renderer. | The build. |
| `twoc/src/d3d_host.c`, `d3d_host.h` | 560,594; 25,201 | The host renderer (Direct3D 11, interpolation, anti-aliasing, the overlay's frame); a few game addresses (the camera at 0x242620, the HUD's panels; 13 `sub_` mentions, mostly comments). | The build. |
| `twoc/src/main.c` | 30,369 | The entry point: loads `default.xbe`, the memory layout, the fence mirror at the game's D3D device address, the game's entry point. | The build. |
| `twoc/src/gamefiles.c` | 12,880 | The start check: `default.xbe`'s size and SHA-256, the number of files in each of Crashdat's folders. | The build. |
| `twoc/data/disc_usa.tsv` | 240,479 | The supported disc's manifest: the 2,267 files' paths, sizes and SHA-256 (and the certificate's title id, region, version). Names and hashes only, no game data. | The build (compiled into the first-run setup, which checks the player's own disc against it). |
| `twoc/lift.ps1`, `twoc/analyze.ps1`, `twoc/tools/{lift_flag_join,lift_frndint,wrap_callers}.py` | 1,509; 1,114; 12,809; 1,938; 2,893 | The regeneration pipeline's steps and its two lifter fixes (a flag join across compare widths; frndint by the control word), with one function named. | Regenerating the generated code from the player's own disc (below). |
| `twoc/run_twoc.ps1` | 10,164 | The developer launcher: the game's switches (fast title, host-only, widescreen, dumps). | Developers (optional; no personal paths). |
| `twoc/tools/diag/` | 400 files, 1,135,083 | The developer harness: level, pad and position tables, the game's memory addresses (player object, wumpa counter, camera, warp rooms), patches against the hand-written code, log and frame analysers, capture tools. | Not needed to build: **private**. (A few files are generic: `wgcgrab.cpp`, `loopcap.cpp`, `vrrprobe.cpp`; they can be published on their own later.) |
| `twoc/tools/capturekit/` | 16 files, 16,108 | The owner's capture kit (launchers named after levels and glitches). | Not needed: **private**. |
| `twoc/tools/package/targets/{LegionGo,Nitro5}/` | 6 files, 73,291 | The owner's test packages' READMEs, test steps and settings for their own devices. | Not needed: **private** (the Share target is published). |
| `twoc/tools/package/{smoke_test.ps1,smoke104.py,windows.py,pgo_train.sh}` | | The test packages' smoke tests; the PGO training run, which drives the private harness. | Not needed for a plain build: private (the release script says PGO needs the harness). |

## (c) Clean: IN

Everything else of twoc: the host and platform code (`src/` but the files above: settings, overlay, controls, pads,
audio volume, anti-aliasing, ambient occlusion, the first-run setup and its disc reader, saves and their backups,
the data folder, names, restart, About, GPU and CPU information, power and memory logs, the Vulkan probe, the audio
DLL's loader, the manifest, the version resource), `apu_dll/` (the LGPL audio DLL's build files and wrapper),
`third_party/` (Dear ImGui, SMAA, XeGTAO, Vulkan headers, volk: each with its licence), `CMakeLists.txt`,
`tools/build_info.cmake`, the packaging scripts for the Share package (`make_package.ps1`, `package_audit.ps1`,
`make_apu_source_package.ps1`, `third_party_licenses.py`, `LICENSE.package.txt`, `files/`, `targets/Share/`),
`LICENSE`, `NOTICE`, `THIRD_PARTY_LICENSES.txt`, `ISSUES_URL.txt`, `.github/ISSUE_TEMPLATE/`, and the public files
in `twoc/public/` (README, CONTRIBUTING, SECURITY, the developer documents).

Kept private although clean: the notes (`crash claude md/`, 1.08 MB: the whole history of the work, with
disassembly excerpts, addresses, playtest notes, the owner's folders and devices), replaced in the public tree by
`docs/DEVELOPING.md`; `docs/SHARING.md`, `docs/RELEASE_CHECKLIST.md`, `docs/LAWYER_QUESTIONS.md` and this file (the
owner's own planning).

## The toolkit's local commits

xboxrecomp (MIT, sp00nznet; its APU files LGPL-2.1, from xemu) at upstream `766ecef` ("Release v0.11.0", 2026-09-19)
plus 41 local commits touching 23 files (+3,278 / -1,279 lines): the D3D8 translator (fog, combiners, the
dot-product texture modes, vertex-program multiply, per-draw costs, baked draws, the w = 0 vertex), the APU (the
voice processor's resampling, HRTF, 5.1, gains, pacing), the kernel (counters, accessors, stack slices) and the NV2A
survey. **Clean**: no game function, address or data in the added lines (only comments that name twoc and its
notes). They go public as a fork's branch or as the patch files `make_public_tree.ps1` puts in `toolkit/patches`
(docs/TOOLKIT.md).

## What the public repository can and can't do by itself

It **can**: show every hand-written line (the port's own code, the bridge, the renderer, the setup, the tools), build
the packaging and the audio DLL, and let a developer **with their own disc** regenerate the translated code and build
the game. It **can't** build the game as it stands: the generated code (`twoc/src/recomp/gen`) is not in it, by
design (the "repository has no generated code" model).

**Regenerating the code** (a developer with the disc, Windows, Visual Studio 2022, CMake, Python 3.11+):

1. Clone the public repository; make the toolkit beside `twoc/` (`toolkit/fetch_toolkit.ps1`: upstream at the
   pinned commit, the patches applied with `git am`).
2. Put the disc's `default.xbe` in `game_files\` (the first-run setup's extraction gives exactly that file; its
   SHA-256 is in `twoc/data/disc_usa.tsv`).
3. A Python virtual environment at `.venv\` with the toolkit's one requirement (`pip install capstone`; capstone
   5.0.7 here).
4. `powershell -File twoc\analyze.ps1` (the XBE parser, disassembly, function identification, ABI analysis, seeded with
   `twoc\seed_functions.json`), then `powershell -File twoc\lift.ps1` (the lifter through the two fixes, then the
   wrapper callers): `twoc\src\recomp\gen\`.
5. `cmake -S twoc -B twoc\build -G "Visual Studio 17 2022" -A x64`, `cmake --build twoc\build --config Release`.

**Tested from a clean copy (step 110):** `git archive` of twoc and of the toolkit at their HEADs into scratch,
`default.xbe` copied in, the repository's Python (capstone 5.0.7): `tools.xbe_parser` (the analysis JSON beside the
XBE, a step `analyze.ps1` didn't run: added in step 110), `tools.disasm` with the seeds (12 s), `tools.func_id`,
`tools.abi_analysis`, the lifter through the two fixes (18 s), `wrap_callers.py`: **all 9 generated files
byte-identical** to the private tree's `twoc/src/recomp/gen`. **Without `seed_functions.json`** the same pipeline
lifts 4,595 functions instead of 4,647: the 52 reached only by pointer are missing and the game would stop at the
first call to one. So a public tree without the seeds can't regenerate a working program (the decision in group (a)
above). Not run from that copy: the compile (Part 3's release build compiles from a clean clone with the generated
code copied in).

## The toolkit: how the public repository refers to it (recommendation)

Upstream (https://github.com/sp00nznet/xboxrecomp) is MIT ("Copyright (c) 2026 sp00nz", `LICENSE`); its `NOTICE`
says the APU files come from xemu under the LGPL 2.1 or later (`apu_core.c`, `apu_dsp.c`, `apu_vp.c`, `apu_regs.h`,
`apu_state.h`, `apu_debug.h`, `fpconv.h`, `nv2a_regs.h`; `apu_shim.h` by choice) and that distributing the source
satisfies the LGPL. The MIT parts need their notice kept; the LGPL parts need their source and licence with any
binary (the Share package already ships `xemu_apu.dll` with its complete source zip and the LGPL text).

| Option | For | Against |
|---|---|---|
| **A public fork** of sp00nznet/xboxrecomp with a `projectwrath` branch (the 41 commits), referenced as a **submodule** | Attribution and history in the open; upstream sees the changes and can take them; a normal `git clone --recursive` | The owner creates the fork on GitHub (a step outside this session); the fork must stay available |
| **Patch files** against a pinned upstream commit (`toolkit/`, already in the export) | Self-contained; works today; nothing to create | A developer runs `fetch_toolkit.ps1`; history only as patches |
| A submodule of upstream alone | Simple | Can't carry the local commits |
| Vendored (the toolkit copied into the repository) | One clone builds | Mixes two projects' histories and licences in one tree; harder to send changes upstream |

**Recommended:** keep the patch set in the public repository now (it is tested: applied with `git am` to upstream
`766ecef` it rebuilds the private toolkit's tree exactly, `da88e858`), and, once the owner has created the public
fork with the `projectwrath` branch made the same way, add it as a submodule at `xboxrecomp/` and keep the patches as
the record. Offer the generic fixes upstream as pull requests later, one topic at a time.
