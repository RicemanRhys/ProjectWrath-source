# Recomp / Decomp / PC-Port Reference Notes

Compiled Sep 21, 2026 from a research chat. Facts come from READMEs, wiki pages and search snippets read that day. Star counts and project status change quickly, so re-check before relying on them. Anything marked **(unverified)** is a link I never opened.

---

## 1. The Crash project at a glance

- **Goal:** a native Windows PC version of *Crash Bandicoot: The Wrath of Cortex* (the **Xbox** version) using static recompilation, worked on together with Claude Code.
- **Wish list:** stable 60fps (xemu currently gives a choppy 30-40fps), 16:9 widescreen, surround sound.
- **Idea to try first:** a 16:9 patch tested in xemu before the recomp runs, mainly to find out whether the game culls geometry at the sides.
- **Hardware (as stated):** RTX 4080 Super, Ryzen 7 7800X.
- **Have:** own ISO of the game: **NTSC-U (North America)**, confirmed from the XBE header. Details in section 13.
- **Deliberately not done yet:** the project `CLAUDE.md` (postponed by choice).
- **Toolkit chosen:** `xboxrecomp` by sp00nznet (original Xbox, x86).

---

## 2. Plan for the first Claude Code session

**Prerequisites (Windows):** Visual Studio 2022 ("Desktop development with C++"), CMake 3.20+, Python 3.10+ with `pip install capstone`, Git, Claude Code.

**Folder layout:**
```
crash/
  xboxrecomp/    (cloned; builds runtime libraries only)
  twoc/          (your game project, copied from xboxrecomp/templates/new-game)
  game_files/    (extracted disc contents, gitignored, never committed)
```

**Extract the ISO:** use `extract-xiso` or `xdvdfs` (the xboxrecomp README also lists an ISO tool of its own). `default.xbe` must sit at the top level of `game_files/`. Keep the ISO itself outside the repo. Confirm it's the Xbox release (PS2 and GameCube releases won't work).

**Milestones, one at a time:**
1. Check prerequisites, build the runtime libraries, run the tests (`pytest tools/`).
2. Parse the XBE and report: engine, XDK version, sections, function count, threading, so we can judge how hard the target is. Also note the game's frame rate/timing method.
3. Optional Ghidra naming pass, before lifting, so generated C has real names.
4. Lift to C, scaffold the game project from the template, get it to compile.
5. Boot, then loop: crash, trace, fix, repeat.
6. Then graphics debugging, then features (60fps, 16:9, surround).

**First prompt to paste into Claude Code** (from `crash/`):
```
I want to statically recompile the original Xbox version of Crash Bandicoot:
The Wrath of Cortex into a native Windows exe, using the xboxrecomp toolkit in
./xboxrecomp. Goals: playable, stable 60fps, widescreen (16:9), surround sound.
I own the disc; the extracted files are in ./game_files (never
commit them).

Before running anything, read xboxrecomp/README.md and the docs/ folder in full,
especially GETTING_STARTED.md, the decompilation guide, and the technical notes
on indirect calls, the register model, memory layout, D3D8->D3D11 translation
and NV2A shader translation.

Then: (1) check prerequisites (MSVC 2022, CMake, Python + capstone) and tell me
what's missing, (2) build the runtime libraries and run the tests, (3) parse
the XBE and report engine, XDK version, sections, function count, threading and
frame timing so we can judge how hard this target is. Stop there and write what
you learned into a CLAUDE.md before continuing.
```
(Earlier versions of this prompt named specific doc files that I had not verified exist. This version tells Claude Code to read everything in `docs/` instead.)

**Working habits that pay off:**
- Every pipeline step goes in a script, so regenerating the lifted C is one command.
- Commit after each working state. Keep the generated C out of git (millions of lines).
- Grep the generated code instead of reading it wholesale, unless you don't mind the usage.
- Have Claude Code add a "dump framebuffer to PNG and exit after N seconds" mode, so it can see its own output.
- Learn RenderDoc for graphics debugging.
- Record what's learned about the engine in `CLAUDE.md` as you go.

---

## 3. Facts about the game

- Developer: Traveller's Tales. Publisher: Universal Interactive. Released PS2 Oct 2001, **Xbox Apr 2002**, GameCube Sep 2002 (the GameCube port was by Eurocom).
- Xbox version: higher-quality textures, better lighting and fur, reduced load times vs PS2.
- **Video:** Xbox supports 480p, but **not 16:9, 720p or 1080i** (ConsoleMods wiki). TCRF also says the Xbox version is the only one natively capable of 480p.
- **Audio:** MobyGames lists Xbox sound as **Dolby Digital**. IGN's Xbox review called the surround mix "sloppy", and IGN's GameCube review lamented the lack of Dolby Pro Logic there. I haven't confirmed the format in-game.
- Reviewers noted fixed camera angles and "sluggish" controls.
- The PAL GameCube version reportedly has unused 60Hz/widescreen toggle code (Xbox fandom wiki).
- PCSX2's wiki lists a widescreen patch for the PS2 version.

---

## 4. Wish-list analysis

### 60fps
- xemu emulates the whole console, so it is expensive. A recomp translates code ahead of time, so the CPU cost mostly disappears. A 2003 Xbox game is light work for the hardware above, so smoothness should improve a lot (expectation, not a measurement).
- The user believes the game can reach 60fps on real hardware. If so, a working recomp should just run at native speed. Verify with frame-time logging: some games mix 60 and 30 in busy scenes, or use a limiter.
- If the game is really 30fps or has timing-locked logic, options are (a) patching the frame wait, as the Simpsons recomp did with an instruction-level patch, and (b) **visual interpolation with the logic left at the original rate**, as TriAevum and WiiCompiled do (experimental, may show artifacts).
- Other stutter sources: shader-compile hitches (can precompile shaders), frame pacing/vsync bugs, and translation-layer performance bugs.

### 16:9
- Native 4:3 only, so this is a new feature. Widening means changing projection (aspect/FOV), UI placement, and possibly culling.
- **Culled at runtime:** fixable. Patch or disable the culling check in a hook and the geometry draws.
- **Never built or stripped in level data:** not restorable automatically. Options are widening modestly, skyboxes/fog fillers, or hand-made geometry after reverse-engineering the level format.
- **Chunk streaming / baked visibility lists:** may need widened loading ranges or overridden lists.
- HUD/menus need repositioning. Pre-rendered videos would stay 4:3 (pillarboxed).
- **Quick test:** run with culling disabled and look at the level edges.
- **xemu pre-test idea:** patch a copy of `default.xbe` (aspect ratio, FOV, culling) and run it in xemu. The addresses found carry over to the recomp. I haven't verified whether xemu has a built-in patch system, so patching the XBE is the reliable route. A flag flip (like OGxHD) won't test culling. Keep patched XBEs private.
- Finding the right spots needs the same disassembly as the recomp, done in Claude Code on the real file, not from chat.

### Surround sound
- Likely Dolby Digital (per MobyGames), to be confirmed in the game's audio menu or from the audio libraries the XBE imports.
- Audio is a separate runtime piece. xboxrecomp's architecture listed a basic Windows audio backend plus an audio-chip emulation layer. True 5.1/7.1 output would need a multichannel-capable backend, so plan to add and test it.
- The original mix was criticized, and a recomp would reproduce it.

---

## 5. Claude Code and usage notes

- Claude Code (terminal/desktop) is the right tool for build/run/debug loops. Cowork is aimed at general knowledge work, not this.
- **Usage is shared** across Claude chat, Claude Code and Cowork: a rolling 5-hour window plus a weekly limit. Anthropic doesn't publish fixed prompt counts, and the third-party numbers I found conflicted, so use `/usage` in Claude Code to see where you stand.
- Opus uses more of the allowance than Sonnet (one source: about 2.5x per token). Default to Sonnet 5, and switch to Opus with `/model` only for hard bugs. Pro's default is Sonnet 5, and the 1M-context option draws extra usage credits on Pro.
- Ways to stretch it: fresh session per task (`/clear`), a good `CLAUDE.md`, lower effort for mechanical work.
- Setup that needs no usage: installing prerequisites, cloning, extracting the ISO.
- `CLAUDE.md` is a plain Markdown file in the project folder that Claude Code reads at the start of every session. `/init` drafts one. Contents planned: goal, layout, build commands (MSVC needs a Developer prompt), rules (never commit game files or generated code), a "what we've learned" section.

---

## 6. Existing work on Crash and Twinsanity (as of my search)

No **Xbox** recomp of either game was found. Only decomp/reverse-engineering work:

| Project | What it is |
|---|---|
| `denzi-gh/crashwoc-decomp` | WIP decomp of Wrath of Cortex for **GameCube** (USA Rev 0, GCBE7D). Uses decomp-toolkit, objdiff and a ProDG toolchain. README mentions AI-assisted workflows, including Claude Code. |
| `calmsacibis995/crash-ps2` | Decomp of the **PS2** version (NTSC-U v1.00). Can't be built yet; mostly stubs. |
| OpenCrashWOC (Open-Travelers) | Clean-room open-source reimplementation. Only its landing page was seen. |
| `NeoKesha/OpenSanityNeo` | Twinsanity **Xbox PAL**: a rebuildable x86 disassembly with resolved labels. Glitchy, only some chunks playable, Ghidra project and code-gen script not published yet. |
| `Smartkin/twinsanity-reversed` | Twinsanity **PS2** reverse-engineering notes and Ghidra data. |

Other Crash items seen in search results (not opened): `wurlyfox/c1` (Crash 1 port to C), `CTR-tools/ctr-native` (Crash Team Racing native port), `ughman/c2c` (Crash 2 in C, old), `Matteo842/CrashBandicoot-Launcher` (Crash 1 PS1 recomp launcher built on RecompOne).

The GameCube version was ported by a different studio, so its decomp is a structural/naming reference, not a drop-in.

---

## 7. Glossary

- **Emulator** (xemu, Xenia, Dolphin): a program that pretends to be the console while you play.
- **Static recompilation:** translating the game's machine code into C/C++ ahead of time, then compiling a native program.
- **Runtime:** the stand-ins the recompiled game still needs: OS/kernel, graphics, audio, input.
- **Decompilation:** hand-rewriting the game's code as readable source. A *matching* decomp compiles back to a byte-identical binary with the original compiler.
- **Port:** taking source (a decomp) and adapting it to run on a new platform.
- **XBE / XEX:** executable formats for the original Xbox / Xbox 360. **DOL / REL:** GameCube/Wii executables.
- **Hook:** a small function that replaces or wraps one game function. **Midasm hook:** an inline patch at one instruction address.
- **Culling:** skipping objects outside the camera's view.
- **HLE:** high-level emulation of system services.
- **objdiff / decomp-toolkit / m2c / decomp.me:** common decomp tools (comparison, splitting, auto-draft C, collaborative matching).

---

## 8. Reference projects by console

### Original Xbox (x86)
- **xboxrecomp** (sp00nznet): toolkit for recompiling original Xbox games from XBE to native Windows programs. Pipeline: XBE parser (`--json`, output named `<name>_analysis.json`), disassembler (`--text-only`, or `--extra-sections` for XDK library code), library function identification, ABI analysis (don't skip), optional Ghidra naming, then the lift to C (millions of lines). **The repo builds libraries only.** The exe comes from your own game project (template in `templates/new-game`). Runtime libraries cover kernel, D3D8->D3D11, audio, input. Docs cover the register model, indirect calls, memory layout, D3D8 translation and NV2A shaders. It also has a decomp mode that splits the game into per-function assembly. Prerequisites: Windows, VS 2022, CMake, Python + capstone. The README says a first boot will crash, indirect calls are the hardest ~10%, and Burnout 3 took about two weeks to reach rendered tracks (its earlier "playable" claim is being redone). Debug helpers include watchdog/trace environment variables and a framebuffer window. Forks have older READMEs, so use sp00nznet's repo. `https://github.com/sp00nznet/xboxrecomp`
- **OpenSanityNeo**: see section 6.

### Xbox 360 (PowerPC)
- **XenonRecomp** (hedge-dev, ~6.5k stars): converts Xbox 360 executables to C++. **Translator only, no runtime.** XenonAnalyse finds jump tables (may need per-game tweaks). Needs Clang 18+. Exceptions unsupported, MMIO unimplemented. Supports hooks and mid-asm hooks. Foundation of Unleashed Recompiled.
- **XenosRecomp** (hedge-dev): converts Xbox 360 shaders to HLSL.
- **ReXGlue SDK** (rexglue): recompilation runtime + toolkit, rooted in Xenia code, inspired by XenonRecomp. Early development. Runtime uses a Xenia-derived GPU/kernel emulation layer: the game's CPU code is native but GPU commands are still translated at runtime. Has a wiki with a quickstart. Fork **rexglue-ostentation** (SolarRecomps) adds texture/audio replacement.
- **The Simpsons Game Recomp** (YesterMester): ReXGlue-based, ~82,000 recompiled functions. Playable on Linux/Steam Deck, Windows first bring-up. Real 60fps needed an instruction-level patch of a hardcoded "wait 2 vblanks". A performance roadmap plans replacing GPU emulation with a native renderer in stages. Installs from the user's own ISO through a launcher. v0.0.4.1 as of Jul 2026.
- **Naughty Bear ReStuff** (MaxDeadBear): ReXGlue-based, 60fps, randomizer, texture/audio replacement, installs via the Goopie launcher.
- **reDAHM** (masterspike52): Destroy All Humans! Path of the Furon, ReXGlue-based. No README content on the page I read.
- **KameoRePowered** (birabittoh): ReXGlue-based, cleanest README. Scripts download the SDK, extract the ISO into `assets/`, build (Title Update and DLC supported), kb/mouse controls. **Hook system:** add a guest address to a config file, implement a C++ function; or a midasm hook for inline patches. Licence note: recompiled output is not redistributable.
- **Marathon Recompiled** (sonicnext-dev, Sonic '06): XenonRecomp + XenosRecomp, ahead-of-time shader conversion, own runtime. Windows/Linux/macOS. Under development, "not for public use", preserves original bugs. Build: clone with submodules, put your game files in `MarathonRecompLib/private/`.
- **EdgeOfTimeRecomp** (goliathret, Spider-Man: Edge of Time): ReXGlue + a XenosRecomp fork. Shows no screen yet. ReXGlue's emulated renderer had "significant restrictions", so a custom native renderer (hooking the game's embedded D3DX9) is being built in a private repo. Strict policy against LLM-driven PRs.
- **Lesson:** getting the CPU code native is the part that works well. The GPU translation is what takes the time.

### PS2 (MIPS)
- **PS2Recomp** (ran-j): **(unverified)** never opened.
- PS2 decomps of Crash: see section 6.

### GameCube / Wii (PowerPC)
- **WiiCompiled** (patchzyy): working static recomp + native port of Mario Kart Wii (PAL `RMCP01`). .NET translator, **aurora** graphics layer on Dawn/WebGPU, custom runtime. Any aspect ratio, high internal res, experimental frame interpolation. Physics verified via ghost replays. Setup translates and compiles on the user's PC (no translated binary shipped). The translator handles DOL/REL generically, but a playable port needs a game-specific runtime (audio, input, GX).
- **GCRecompiler** (ExpansionPak): archived May 23, 2026, unfinished. Points to another recompiler, GCRecomp (Rust).
- **Decomp -> port route:** see below.
- **Twilight Princess:** `zeldaret/tp` is a fully matching GameCube decomp (the repo says it will not produce a port). **Dusklight** (Twilit Realm, formerly Dusk) is the native PC/mobile port built on it, using aurora, released May 2026 after ~5 years.
- **doldecomp/melee:** WIP matching decomp (US 1.02, `GALE01`), 3,000+ commits. `configure.py` + ninja, objdiff, m2c-based helper, decomp-permuter. Its built DOL can be shifted (add or remove code). Not a port.
- **melee-pc** (999sian): one-person beta port of Melee built from the decomp on aurora, "same approach as Dusklight". Works for most modes. Missing: online rollback, widescreen camera/HUD, macOS. Its **porting notes are valuable**:
  - Hard-coded GameCube offsets break because pointers grow to 8 bytes.
  - Bitfield order, byte order and struct layouts need fixing one by one.
  - Aurora's `GXEnd` submits a draw, so every `GXBegin` needs one.
  - GCC-only big-endian struct support is used, and lint tools check struct sizes against the GameCube ABI.
  - Decompiled game code is **not licensed**. Only the port code is GPL, and the repo isn't redistributable as a whole.
- **doldecomp/sms** (Super Mario Sunshine): WIP decomp, updated Sep 19, 2026. Supports JPN Rev 0 (GMSJ01); PAL Rev 0 "slightly broken". The US version wasn't listed. No PC port found. (See section 9.)

### Switch (AArch64)
- **suyu** (suyu-emu; another copy under dougchansan, unopened): Switch emulator plus an AArch64-to-x86-64 recompiler mode, based on Eden (yuzu lineage). Archived Sep 5, 2026 as the final v0.04. 1 star, essentially no community. Legal risk: yuzu-lineage emulators have had trouble with Nintendo. Treat as reference only; I didn't verify that the recompiler mode works.

### 3DS (ARM)
- **TriAevum** (coccofresco): experimental alpha native port of Ocarina of Time 3D, built on Ship of Harkinian's platform code and Azahar 3DS research. Widescreen framing with adjustable FOV, **60/90Hz visual interpolation with the original 30Hz logic kept**, texture packs, graphics options. The README doesn't say how edge content or culling is handled. Needs your own decrypted EUR copy.

---

## 9. If you do a Super Mario Sunshine (GameCube) project later

- Check which region you own first: the decomp supports JPN Rev 0, and PAL is partly broken.
- Two routes: (1) finish or contribute to the decomp, then port it Dusklight/melee-pc style; (2) a WiiCompiled-style translator + aurora + a game-specific runtime you write yourself.
- Decomp ports hit pointer-size and layout bugs (see melee-pc). Recomps keep the original 32-bit memory layout, which avoids that class of bug but doesn't give readable source.
- Different games need different compilers. Melee and Sunshine use Metrowerks, while the Crash GameCube decomp uses ProDG.

---

## 10. Cross-cutting lessons

- **Frame rate is often hard-coded.** Look for waits and timing-locked logic. Interpolation is a fallback that leaves logic untouched.
- **Hooks are the modding mechanism** (function hooks and mid-asm hooks): 60fps patches, widescreen, controls.
- **The GPU is the hardest part.** Even ReXGlue projects hit renderer limits and are building custom renderers or converting shaders ahead of time.
- **Never redistribute game-derived code:** the generated C, the recompiled output, patched executables, or decompiled source. Ship tools and launchers that build from the user's own disc, the way these projects do. Keep game files and generated code out of git.
- **Setup tools that translate on the user's machine** (WiiCompiled, Simpsons launcher) are the common pattern.
- **Read each project's own contribution rules** before opening PRs (AI policies vary).
- **Errors should be loud:** projects prefer crashes with logs over silent failures.
- **A matching decomp doesn't need to be 100% complete** before someone starts a port (Melee's FAQ says so), though a complete decomp is a better reference.

---

## 11. Warnings

- **Fake "PC port" repos exist.** Repos like `Zelda-TP-PC-Port` with keyword-stuffed READMEs and "run the installer as Administrator" instructions look like malware bait. The real Twilight Princess projects are `zeldaret/tp` and Twilit Realm's Dusklight. Only take builds from a project's own Releases page. WiiCompiled's README warns the same.
- Several projects (Marathon, EdgeOfTime, reDAHM) say they're in development and not for public use. Don't trust anyone claiming a finished port.
- Third-party usage-limit figures are inconsistent. Use `/usage` and Anthropic's support pages.

---

## 12. Open questions

1. ~~What region is the ISO?~~ Answered: NTSC-U, title ID VU-003 (section 13).
2. Does the game run at 30fps or 60fps on real Xbox hardware? Is its logic timing-locked? *Partly answered (section 14.8): logic is a fixed 60 Hz tick with 1 to 6 catch-up ticks. The real-hardware render rate is still unknown.*
3. Which audio format does it actually use (Dolby Digital vs Pro Logic)? *Still open: a DOLBY section is linked (section 14.4), but I haven't confirmed it is enabled.*
4. Is 4:3 geometry culled at runtime, baked into the level data, or both? *Not investigated. What is known about resolution and projection is in section 14.9.*
5. Are cutscenes engine-rendered or pre-rendered video? *Probably engine-rendered (section 14.5, inferred from `.hgo` intro/outro assets and no video codec linked).*
6. Does xemu offer a patch/cheat mechanism, or is patching the XBE the route?
7. ~~How much of the camera is fixed vs free?~~ Answered: fixed camera angles that follow Crash automatically.
8. Is PS2Recomp relevant to any PS2 project? (never opened)

---

## 13. Our disc and workspace (Sep 21, 2026)

**ISO:** `crash rom/Crash Bandicoot - The Wrath of Cortex (USA).xiso.iso` (975,896,576 bytes, already in xiso form). Kept outside git.

**Extraction:** `xdvdfs unpack` with xdvdfs v0.8.3 (the Windows zip from antangelo/xdvdfs GitHub Releases) into `game_files/`. That gave 2,267 files, 932 MB, with `default.xbe` at the top level.

**XBE certificate (`default.xbe`, 1,777,664 bytes, base 0x10000):**

| Field | Value |
|---|---|
| Title | Crash Bandicoot: tWoC |
| Title ID | `0x56550003` = **VU-003** (VU = Universal Interactive) |
| Game region flags | `0x00000001` = **North America only (NTSC-U)** |
| Certificate timestamp | 2002-03-20 18:42:29 UTC (fits the Apr 2002 Xbox release) |
| Disc number / version | 0 / 1 (probably the original release, not a revision) |
| Allowed media | `0x2` (DVD-X2) |

**Folder layout of `game_files/`:**
```
default.xbe
Crashdat/                  2,266 files, 931 MB
  levels/                  481 files, 459 MB
  sfx/                     683 files, 353 MB (sfx/Music holds the music as .wav)
  chars/                   959 files, 46 MB
  stuff/                   37 files, 7.1 MB
  Gfx/                     48 files, 3.7 MB
  ats/                     51 files, 110 KB
  levels.dat, chars.dat, memoryfiles.dat, oldmemoryfiles.dat
  Universal.bmp, lega2l.bmp
  mkdat.exe                (a leftover TT build tool, not the game)
```
Most common file types: .wav (663), .ani (524), .hgo (252), .bsa (205), .nux (70), .dat, .bmp, .ter, .LGT, .ats. The `.nux`/`.hgo` models match Traveller's Tales' NU engine naming.

**Workspace:**
- The top folder is a git repo. `.gitignore` excludes `game_files/`, `*.iso`/`*.xiso`, `*.xbe`, `xboxrecomp/`, generated recomp output (`**/src/recomp/gen/`, `**/tools/*/output/`, `*_analysis.json`) and build output.
- `xboxrecomp/` is a separate clone of sp00nznet/xboxrecomp (branch `main`) at commit **`766ecefcd7fb2a9b344de8ec891f6fe9ea14261b`**, "Release v0.11.0 -- Nothing Said So", dated 2026-09-19. The outer repo ignores it, so record the commit here if it changes.
- The notes live in `crash claude md/`, not in the top folder.

---

## 14. Toolchain and XBE analysis (Sep 21, 2026)

Milestones 1 and 2 from section 2 are done up to function ID and ABI analysis. **No lifting, no Ghidra yet.** Addresses are Xbox VAs (base 0x10000). Items marked **(inferred)** come from reading disassembly and have not been run or tested.

### 14.1 Toolchain and build

- **Bitness:** the runtime libraries and the game exe are **x64** (xboxrecomp README: "native x86-64 exe"). Only the conformance test oracle is 32-bit, and `python -m tools.conformance` calls `vcvars32.bat` itself. The shell here is already a VS x64 environment (`VSCMD_ARG_TGT_ARCH=x64`). In a fresh shell use "x64 Native Tools Command Prompt for VS 2022" or run `vcvars64.bat`.
- **Versions:** MSVC 19.44 (VS 2022 Community), CMake 4.4.3, Python 3.13.15. Project venv at `.venv/` (gitignored) with capstone 5.0.9, pytest 9.1.1, plus `pefile` and `numpy`, which `tools/fusion` imports but the README doesn't list.
- **Runtime libraries:** `cmake -S xboxrecomp -B xboxrecomp/build`, then `cmake --build xboxrecomp/build --config Release`. 0 errors, 68 warnings, 8 libs (`xbox_kernel`, `xbox_d3d8`, `xbox_dsound`, `xbox_input`, `xbox_usb`, `xbox_apu`, `xbox_nv2a`, `xbox_video`). Libraries only, no exe.
- **Tests (run from `xboxrecomp/`, using `..\.venv\Scripts\python.exe`):** `-m pytest tools/` gives 364 passed, 24 skipped. `-m tools.conformance` gives 5261 vectors and 211 function vectors, 0 mismatches.
- **The 24 skips (resolved Sep 22, 2026, see section 16):** these tests only look for `clang` or `gcc`, never `cl`. LLVM is now installed and the same run gives 388 passed, 0 skipped.

### 14.2 Commands run (all from `xboxrecomp/`, outputs gitignored)

```
py -3 -m tools.xbe_parser ..\game_files\default.xbe --json ..\game_files\crash_analysis.json   (instant)
py -3 -m tools.disasm ..\game_files\default.xbe -v        (13 s; no --text-only, so the library sections are included)
py -3 -m tools.func_id ..\game_files\default.xbe -v       (1 s)
py -3 -m tools.abi_analysis ..\game_files\default.xbe -v  (1 s)
```
Outputs: `game_files/crash_analysis.json`, `xboxrecomp/tools/{disasm,func_id,abi_analysis}/output/`. Use the venv's python in place of `py -3`. `tools.disasm` rewrites `functions.json` from scratch, so re-run any name merge after it.

### 14.3 XBE facts

| Item | Value |
|---|---|
| Title | Crash Bandicoot: tWoC, retail, NA, DVD_X2 |
| Build date | 2002-03-19 00:26:22 UTC. Debug path `C:\Projects\Crash\Release\Crash.exe` |
| Image | base 0x10000, size 0x9400E0 (9.25 MB), stack commit 0x10000, heap reserve 0x100000 |
| Entry point | 0x000EF089. This is the standard XAPI startup stub: it calls CreateThread (0xECC6A) with thread proc 0xEF01A, which calls the game main loop `sub_00087BF0` |
| Kernel thunks | 0x0015AA60 |
| XDK | **1.0.4361** for all 8 libs: XAPILIB, D3D8, D3DX8, XGRAPHC, DSOUND, XBOXKRNL, LIBCMT, LIBC. The docs list signature DBs for 4627, 5233, 5455, 5558, 5849 and 5933, so 4361 is not covered |

**Sections (11):**

| Section | VA | VSize | Notes |
|---|---|---|---|
| .text | 0x00011000 | 0xEC680 | game code, then XAPI/CRT at about 0xEC800 to 0xFD680 |
| D3D | 0x000FD680 | 0x11F04 | D3D8 library |
| D3DX | 0x0010F5A0 | 0x243B8 | D3DX8 library |
| XGRPH | 0x00133960 | 0x213D | |
| DSOUND | 0x00135AA0 | 0x1D944 | |
| XPP | 0x00153400 | 0x7648 | USB peripheral (input) library |
| .rdata | 0x0015AA60 | 0x119E4 | |
| .data | 0x0016C460 | 0x7D92F8 | about 8 MB, mostly BSS |
| DOLBY | 0x00945760 | 0x7180 | 1 detected function, so mostly DSP image or data (inferred) |
| $$XTIMAGE | 0x0094C8E0 | 0x2800 | title image |
| $$XSIMAGE | 0x0094F0E0 | 0x1000 | save image |

- **No XMV, WMADEC, XONLINE or XNET sections.** No video-codec, WMA or network libraries are linked.
- **Kernel imports: 113 of 366.**

| Group | Count |
|---|---|
| Av | 4 |
| Ex | 6 |
| Hal | 8 |
| Io | 9 |
| Ke | 22 |
| Kf | 2 |
| Mm | 12 |
| Nt | 22 |
| Ob | 2 |
| Ps | 3 |
| Rtl | 12 |
| Xbox | 6 |
| Xc | 3 (SHA only) |
| DbgPrint, LaunchDataPage | 1 each |

- **Not imported:** no `KeQueryPerformanceCounter`, no network or crypto beyond SHA. `XeLoadSection` and `XeUnloadSection` are imported (XAPI section loading).

### 14.4 Libraries used

- **Graphics:** D3D8 (286 functions), D3DX8 (583, heavy SSE/MMX: about 1,780 instructions) and XGRAPHC (30). The game calls **91 distinct D3D entry points from 123 game functions**, concentrated in a wrapper cluster around 0x398A0 to 0x3A760. It also calls 9 D3DX and 2 XGRPH entry points. The D3D library is statically linked and touches hardware directly (PCI config read, GPU interrupt via `KeConnectInterrupt` at 0x1074CD). Which route the recomp takes (replace the D3D entry points, or run the recompiled library over the NV2A emulation) is still to be decided. Read `docs/technical/d3d-translation.md` and `d3d8ltcg-device-context.md` next.
- **Audio:** DSOUND (445 functions) with 28 entry points used from about 36 game functions, mostly the cluster at 0xB8E00 to 0xB9C00. The DOLBY section is present, consistent with Dolby Digital output but not confirmed as enabled. Music and SFX are plain `.wav` files (strings such as `MUSIC\CRTIMER` and `%s.wav`), and no WMA or XMV libraries are linked.
- **Input:** XPP (222 functions), called through 10 entry points from about 14 game functions (0x362F0, 0x36330, 0x369F0, 0xEB2A0 to 0xEB6C0). Button-name strings `xbox2_a/b/x/y/black/white`.
- **Interrupt-driven library code:** D3D (GPU), DSOUND (APU, 2 interrupts) and XPP (USB OHCI) all use `KeConnectInterrupt`, DPCs and timers (`KeSetTimer` at 17 sites, mostly XPP). The runtime's `apu`, `usb` and `nv2a` layers matter here.

### 14.5 Engine clues

- **Not RenderWare:** func_id found 0 RW strings and 0 RW functions. Consistent with Traveller's Tales' own engine.
- **Only 3,814 strings.** Most are asset paths, so there are almost no function or debug names.
- **Asset extensions:** `.hgo`, `.nus`/`.nux`, `.ani`, `.bsa`, `.ghg`, `.ai`, `.anm`, `.gra`, `.LGT`, `.mtl`, `.obj`, `.crt`, `.dds`, `.bmp`. Data root `d:\crashdat\levels\`, which matches `game_files/Crashdat`.
- **Leftover in-game editor and debug menus:**
  - Menu strings such as Switch Properties, Terrain One Cycle, Attached Particles/Sounds, Waypoints and a light editor.
  - A material editor with "Shader:" entries (Environmap, BRDF Gold, Projected Tex, Specular, Water Caustics, Glass, Water) and cull mode CCW/CW/None.
  - Cubemaps.
  - `fmvdump\frm` (debug FMV frame dump).
  - `DEBUG`.
- **PS2 leftovers:** memory-card test menus with "PS2 Card" and "Slot 0/1".
- **Localization:** text in 6 languages (EN, NL, ES, IT, DE, FR).
- **Cutscenes:** they appear to be engine-rendered, because intro and outro assets are `.hgo` objects (`levels\b\intro\cscrash.hgo` and similar) and there is no video codec section **(inferred)**.
- **C++ presence:** 182 validated vtables, 1,642 virtual methods, 98 constructors.

### 14.6 Functions

- **Disassembler total: 4,610 functions**, 432,515 instructions, 91,429 xrefs. About 21% of the 22,095 the docs list for Burnout 3.
- **Correction (found in section 15.3):** 2 of the 445 DSOUND "functions" are data, not code (`sub_0014726F`, 50 KB, and `sub_0014FF0C`, 14 KB). So DSOUND has 443 real functions, the true total is **4,608**, and the DSOUND x87 and other instruction counts above include garbage from that data. Other small data-as-code regions may remain inside function ranges (the lifter reported 90 unimplemented instructions; the ones sampled were data).

| Section | Functions |
|---|---|
| .text | 3,043 |
| D3D | 286 |
| D3DX | 583 |
| DSOUND | 445 |
| XPP | 222 |
| XGRPH | 30 |
| DOLBY | 1 |

- **Library code by location (my split, approximate):** 1,567 in the XDK sections, plus about 596 XAPI/CRT functions in `.text` from about 0xEC800. That is about 2,160 (47%) library, and about 2,447 game functions (about 954 KB, median 126 bytes, largest `sub_000B0EF0` at 14,240 bytes).
- **By name, func_id identified only 12 CRT functions (0.2%):** `_ftol`, `fmod`, `_aullshr`, `strcat`, `_allmul`, `memmove`, `_allshr`, `_alldiv`, `_allshl`, `_purecall`, `strcmp`, `strcpy`. The signature DB doesn't fit XDK 4361.
- **func_id's own totals:** it reports 5,966 functions. That includes 1,356 "vtable thunk" candidates (unverified; the disasm docs warn such addresses can sit inside existing functions), so **use 4,610**. Its "game_render, game_audio, game_input" classes (about 1,100 functions) just mean "calls into an XDK section", not library identification.
- **ABI analysis:** 4,569 cdecl and 41 thiscall. **3,460 (75%) are "low-confidence default"** (cdecl, 0 params), and only about 1,150 have real evidence, mostly from `ret N`. Signatures may be wrong for the rest.
- **Game-code instruction mix:**
  - About 43,750 x87 instructions.
  - Essentially no SSE (a `cvttss2si` in the float-to-int helper at 0x7AF40, which is called from many sites).
  - 151 jump tables.
  - About 146 indirect call sites (69 `call [abs]`, 32 `call [mem]`, 45 `call reg`) plus 1 `jmp reg`. That is small compared with the "hardest ~10%" warning in the docs.

### 14.7 Threading

- **Main thread:** created by the XAPI startup stub, running proc 0xEF01A, which calls the main loop.
- **Three more threads** created by game code through CreateThread (0xECC6A):
  - At 0x2C043, proc 0x2BF30 (handle at [0x1BAA8C]).
  - At 0x87969, proc 0x86BC0 (handle at [0x23C454]).
  - At 0xB9AE9, proc 0x5DA30 (in the sound-code cluster).
- **Roles:** not determined.
- **Kernel calls:** `NtResumeThread`, `NtSuspendThread`, `KeSetBasePriorityThread` (1 each, XAPI wrappers), `PsTerminateSystemThread` (2), `KeSetEvent`, `NtSetEvent` and `KeWaitForSingleObject` (6 and 3 and 6), `NtWaitForSingleObject` (2), critical sections (4 init sites in CRT/XAPI).

### 14.8 Frame rate and timing

- **The game timer is a hard-coded 60 Hz logic tick with catch-up (inferred).**
  - `sub_00037540` is called once per main-loop iteration (at 0x8840B).
  - It computes ticks = round((QPC delta / QPF) × **60.0** (float at 0x15D5C0)) minus the carried count, and returns it **clamped to 1..6**.
  - The result goes to [0x1A0A98], and an inner loop at 0x88450 runs the per-tick logic that many times per rendered frame. Any of [0x23C2C8], [0x23C2C4] or [0x23C2CC] being nonzero forces it to 1.
  - `sub_00037520` (called at 0x88295) reads the frequency and stores it at 0x58E2A8.
- **QPC and QPF:**
  - XAPI QueryPerformanceCounter at 0xED3AF is a bare `rdtsc`, with no kernel call.
  - XAPI QueryPerformanceFrequency at 0xED3C0 returns 0x2BB5C755, which is 733,333,333 Hz.
  - A separate 16-slot QPC start/stop profiler (`sub_000361B0` and `sub_00036200`, about 25 call sites, mostly in the main loop).
- **The tick count is never below 1 (inferred).** If the recomp presented faster than 60 Hz, the game would run **fast**. The frame rate has to be limited by vsync or a limiter.
- **Other timing calls:** `GetTickCount` (`KeTickCount`) from 12 sites in 5 game clusters (0x2BD80, 0x2D950, 0x86BB0, 0xB8B40, 0xB9E00), roles unknown. `Sleep` has one caller, in the XAPI layer. **No frame limiter or vsync wait found in game code yet.** Presentation and vblank waits live in the D3D library (`KeWaitForSingleObject`/`KeSetEvent` at 0xFECB0, 0x1034D0, 0x106720, 0x106940, and an `rdtsc` helper at 0x106570). The game's Present call and swap interval are not located.
- **Not yet known:** whether the game reaches 60 fps on real hardware (open question 2). The logic is fixed-step at 60 Hz with 1 to 6 catch-up ticks, and it does not by itself say whether rendering keeps up.
- **No PAL/50 Hz variant** of the constant found (disc is NTSC-U anyway).

### 14.9 Aspect ratio, resolution and projection

- **Display setup:** `sub_0007AF00` (called from 0x37330) calls XGetVideoFlags-style `0xEDC56` (EEPROM setting via `ExQueryNonVolatileSetting`, type 8, shifted right by 16 and masked with 0x5F) and stores it at [0x945284]. It then stores **640 at [0x4F2540] and 480 at [0x4F2544]** and calls 0x3B0C0.
  - [0x945284] is read only by 0x7AF00 and `sub_00039A80` (in the D3D wrapper cluster, likely building the presentation parameters).
  - [0x4F2540] (width) is read by 11 functions, and [0x4F2544] (height) by 26.
  - Whether the game honors the widescreen or 480p bits is **not yet checked**. Read `sub_00039A80` first.
- **Constants:**
  - No 16/9 float (1.7778) anywhere in data or code.
  - No literal 640.0f or 480.0f floats.
  - One 1.3333334f at 0x161E18 with 3 readers (0x619E0, 0x6AB40, 0xA7E30), all of which look non-projection: an audio 0 to 127 volume scale, a vector interpolation, and a colour/alpha fade.
  - Integer 640 and 480 immediates in game code are otherwise only struct strides.
- **Projection setup, FOV and culling are not located yet.** Suggested route: the D3D wrapper cluster 0x398A0 to 0x3A760 (SetTransform-like calls) and the users of [0x4F2540] and [0x4F2544].

### 14.10 What looks easy and hard

**Easier than the reference title:**
- Small: about 2,450 game functions, versus 22,095 for Burnout 3.
- Few indirect calls.
- One consistent library set, and no video codec, WMA, XOnline or XNet.
- 113 kernel imports and 4 threads.
- Fixed 60 Hz logic tick.
- Plain `.wav` audio.
- Data lives in `d:\crashdat`.

**Harder:**
- Almost no names to lean on. Strings are asset paths, func_id names 12 functions, and the engine is proprietary. Names will come from the recomp debug loop.
- 75% of ABI entries are low-confidence defaults.
- About 43,750 x87 instructions, so x87 precision behaviour matters.
- D3D, DSOUND and XPP are statically linked and interrupt-driven, so the NV2A, APU and USB emulation layers matter. The notes already say the GPU is what takes the time.
- Widescreen and 60+ fps need work: 640×480 is hard-coded, the timer runs faster than real time if unlimited, and the camera and projection are not yet found.

### 14.11 What the next two steps would need (not run)

- **Lifter:** `py -3 -m tools.recomp ..\game_files\default.xbe --all --split 1000 --gen-dir <project>\src\recomp\gen` from `xboxrecomp/`. It needs the existing `functions.json` and `abi_functions.json`, and a game project copied from `xboxrecomp/templates/new-game` (for example `twoc/`). The generated `src/recomp/gen/` is gitignored, as is anything under `**/tools/*/output/`. The docs say 5 to 15 minutes for a large game, so expect less here. (LLVM is now installed and the lifter tests pass, see section 16.)
- **Ghidra naming (optional):**
  - Needs a Ghidra install unpacked under `C:\tools\ghidra\ghidra_*_PUBLIC`, or the `GHIDRA_ROOT` or `GHIDRA_HOME` environment variable set. None is present.
  - Needs a compatible JDK. Java 25.0.1 is installed, but I did not check which JDK the chosen Ghidra requires.
  - Runs through Git Bash: `XBE=game_files/default.xbe tools/ghidra_naming/run_ghidra.sh`, then `py -3 tools/ghidra_naming/merge_names.py --apply`.
  - Expected yield is small (the docs measured 134 names on Burnout 3), and the FID `vsOlder_x86` database may or may not match XDK 4361 code. Do it before the lift, and re-apply after any re-run of `tools.disasm`.

---

## 15. Game project `twoc/`: lifted and compiling (Sep 21, 2026)

The lifted C compiles and links into `twoc/build/Release/crash_twoc.exe` (14.9 MB). **It has not been run.** Running is the next milestone.

### 15.1 Plan for D3D, DSOUND, XPP, D3DX, XGRPH

The docs offer two routes for a library that is statically linked into the XBE: **replace it at its API entry points** with the toolkit's layers (`xbox_d3d8`, `xbox_dsound`, `xbox_input`), or **run the recompiled library over the toolkit's hardware emulation** (`xbox_nv2a`, `xbox_apu`, USB OHCI). My plan per library, with reasons. Nothing here has been run.

| Library | Plan | Main reason |
|---|---|---|
| **D3D** (286 functions) | **Replace at the API entry points.** Write a bridge for each of the 91 entry points the game calls, reading the stack arguments and calling `xbox_d3d8`. | `docs/technical/gap-analysis.md` states this route ("D3D8 API intercept instead" of push-buffer parsing). `xbox_d3d8` covers fixed function, register combiners and NV2A vertex microcode. The alternative needs an xemu-captured device-context snapshot and push-buffer replay, which the docs only describe for Burnout 3's LTCG library. |
| **DSOUND** (443 functions) | **Run the recompiled library over the emulated APU** (`RECOMP_AC97_READY`, `mcpx_apu_init_standalone`, VEH trap on 0xFE800000 to 0xFE880000), as the template is already wired. **Fallback:** replace the 28 entry points with `xbox_dsound`. | It keeps the original mixing, streaming and Xbox ADPCM behaviour with no per-call bridge. `xbox_dsound` is only 573 lines. |
| **XPP** (222 functions) | **Replace at the API entry points.** About 10 entry points from about 14 game functions go to `xbox_input` (XInput). | The toolkit's OHCI model says in `src/usb/ohci.h` that it does not yet walk endpoint or transfer-descriptor lists, so a recompiled XPP would find a controller port and never receive a report. |
| **D3DX, XGRPH** | **Lift and run as is.** | CPU-side math and texture helpers. Their calls into D3D reach the replaced entry points. |

- **D3D bridging cost:** the 91 entry points are unnamed in this XDK 4361 build, so each has to be identified from disassembly before it can be bridged. Most of them are called from one wrapper cluster (about 0x398A0 to 0x3A760), which is where to start. Whether the game uses custom NV2A vertex or pixel shader programs (which `d3d8_vsh.c` and `d3d8_combiners.c` would need to handle) is not yet known.
- **DSOUND caveats:**
  - The APU's global and encode DSPs are stubbed (`src/apu/README.md`), so there are no effects and no AC-3 encode. Dolby Digital surround is therefore not available yet, and the surround-sound goal needs a custom multichannel backend in any case.
  - The route is documented against another title (Half-Life 2, per comments in the template's `main.c`), not against XDK 4361.
  - The library also relies on interrupts, DPCs and timers going through the kernel bridge.
- **Mechanism for replacement:** `tools.recomp --exclude-manual <file>` skips generating bodies for the functions defined in a hand-written C file (here `twoc/src/recomp_manual.c`), so the hand-written definition links instead. `recomp_lookup_manual` covers indirect calls.
- **Fallbacks:** if the APU route fails, use the `xbox_dsound` bridge. If the D3D API route hits something it can't express, the NV2A push-buffer route (a device-context snapshot from xemu, per `d3d8ltcg-device-context.md`) is the other option.

### 15.2 What was created

- **`twoc/`** (copied from `xboxrecomp/templates/new-game`):
  - `CMakeLists.txt`: project `crash_twoc`, sibling `../xboxrecomp` pulled in with `add_subdirectory`, and `/MP` added so the five large generated files compile in parallel.
  - `src/main.c`: entry point `0x000EF089`, XBE path `game_files\default.xbe` and data dir `game_files`, both relative to the working directory. **Run the exe from the repo root.**
  - `src/recomp_manual.c`: the template plus the two data stubs from section 15.3.
  - `lift.ps1`: regenerates `src/recomp/gen/` (runs the lifter with `--all --split 1000 --exclude-manual`). It needs the analysis outputs from section 14.2 to exist.
  - `twoc/.gitignore` (from the template) and the outer `.gitignore` both exclude `src/recomp/gen/` and `build/`.
- **Build commands:**
  - `cmake -S twoc -B twoc/build`
  - `cmake --build twoc/build --config Release --parallel`
  - The first build also rebuilds the runtime libraries inside `twoc/build/xboxrecomp/`. Rebuilding the game after a re-lift took 53 seconds.

### 15.3 Lifter and build results

- **Lifter (15 seconds):** 4,610 of 4,610 functions translated, 0 failed, 764,567 lines of C in 5 files (about 190K lines each for the first 3) plus `recomp_types.h`, `recomp_funcs.h`, `recomp_dispatch.c` and `recomp_stubs_unresolved.c`.
  - 65 call targets were not detected as functions and got stubs.
  - 90 unimplemented instructions were emitted as no-op comments (31 distinct mnemonics, including `hlt` 24, `rcr` 10, `in` 6, `pushal` 6, `out` 5). The ones I sampled (`rcr` at 0x9A040, `rcl` at 0x12D70F) have nonsense operands and look like data tables inside function ranges. I did not check all 90.
- **First build: 1 error.** `error C2065: 'dr2': undeclared identifier` at `recomp_0004.c(33766)`. The lifter had emitted `dr2 = eax` for a `mov dr2, eax` decoded from garbage.
  - Cause: `sub_0014FF0C` (and `sub_0014726F`, 50 KB, which contains it) are data in the tail of the DSOUND section, found by the disassembler's `tail_jump_alias` heuristic. Neither has any xref (no call, jump or data reference). Real DSOUND code ends before 0x14726F.
  - Fix: both are defined as empty stubs in `recomp_manual.c`, and `--exclude-manual` skips their bodies. No change to the vendored toolkit.
- **Second build: 0 errors.** The generated-code warnings count was 0 in this incremental build. The runtime libraries' own warnings (the 68 from the first library build) are unchanged.
- **Function count after the fix:** 4,608 generated.

### 15.4 Open items

- **Tests:** *(superseded on Sep 22, 2026: LLVM is installed and all tests pass, see section 16.)* When this section was written, LLVM was not installed and `pytest tools/` gave 364 passed, 24 skipped.
- **Git:** *(updated Sep 22, 2026)* when this section was written the outer repo had no commits and no git identity. Now the identity is set repo-locally (`user.name` and `user.email` in the repo's own config), and the outer repo has three commits, on `master`. The `twoc/` sources (`CMakeLists.txt`, `src/main.c`, `src/recomp_manual.c`, `lift.ps1`, `.gitignore`) are committed.
- **Not done, by design:** no run, no Ghidra naming pass, no CLAUDE.md.
- **Next milestone (run and debug):** first boot will crash (README: expected). The first things to check are `sub_00087BF0`, which is the main loop, and the XAPI startup, then the D3D and XPP bridges.

---

## 16. LLVM installed, lifter tests enabled (Sep 22, 2026)

- **Clang:** `clang --version` reports **23.1.1** (target `x86_64-pc-windows-msvc`), found at `C:\Program Files\LLVM\bin\clang.exe`. That directory is on PATH in the current shell. `gcc` is not installed, and the tests don't need it.
- **Tests (from `xboxrecomp/`, `..\.venv\Scripts\python.exe -m pytest tools/ -q -rs`):** **388 passed, 0 failed, 0 skipped**, 94 subtests passed, 24.7 s. 364 + 24 = 388, so the 24 former skips all ran and passed. No skip lines appeared with `-rs`.
- **Spot check that they really compile:** in `-v` output, clang-backed tests such as `test_fpu_branch.py::test_idiom_semantics_compiled`, `test_incdec_result.py::test_saved_incdec_result_compiled`, `test_lifter_sar_width`, `test_lifter_double_shift`, `test_lifter_result_clobber` and `test_dispatch_flat` show PASSED. These test the lifter's flag, shift, x87-branch and dispatch output against real x86 semantics.
- **What it means:** no lifter bug was found by the newly enabled tests, so the earlier caution about trusting lifted output is lifted for the cases these tests cover (SAR/SHLD/SHRD widths, carry and overflow flags, result clobbering, FNSTSW branches, dispatch table). It does not prove that Crash's own lifted output is correct. Only running the game does that.
- **Not re-run:** `python -m tools.conformance` (last result in section 14.1) and the `twoc/` lift and build. Neither the xboxrecomp commit (`766ecef…`) nor the lifter changed, so no reason to expect a different result.
- **State of `twoc/`:** unchanged since Sep 21. Sources are `CMakeLists.txt`, `lift.ps1`, `src/main.c`, `src/recomp_manual.c` and `.gitignore`. The gitignored `src/recomp/gen/` holds the lifted C and `build/` holds the last build. *(Updated Sep 22, 2026: the outer repo now has three commits and a repo-local git identity, see section 15.4.)*
- **Next:** first run of `crash_twoc.exe` from the repo root (section 15.4).

---

## 17. First run of `crash_twoc.exe` (Sep 22, 2026)

Full report: `report.md` in the session scratch folder (not in the repo). Nothing was fixed; no source, build or generated file changed.

- **Command:** from the repo root, `RECOMP_WATCHDOG_SECS=25 RECOMP_FB_WINDOW=1 RECOMP_FB_DUMP=<file>.bmp`, under `timeout 40`. A second diagnostic run added `RECOMP_KERNEL_LOG_BUDGET=2000000` (the default budget of 200 kernel calls hides everything after early init) and `RECOMP_PEEK`. The dump is a BMP, not a PNG, and the venv has no Pillow.
- **Result: a hang, not a crash.** The watchdog fired at 25 s (exit code 3). No access violation. The game got through XAPI/CRT startup, D3D and DSOUND init, save-data probes, and loaded `sfx\generic\genericsfx.dat` and `gfx\copyr1.raw`. It made 3,629 kernel calls, then none: the ICALL total stopped moving (a CPU spin, not slowness).
- **No framebuffer dump:** the game never called `AvSetDisplayMode` (ordinal 3), so the window never opened. Nothing has been drawn.
- **Hang cause (inferred from disassembly, not yet verified by a fix):** a D3D library fence wait, `sub_001034D0`, reached through `sub_00100C40` (swap/present-like) -> `sub_00100C00` -> `sub_001003A0` -> `sub_001037A0`. It spins until `*[dev+0x34]` reaches the fence, with `dev = [0x0010EBF0]`, `dev+0x30` = PUT and `dev+0x34` = pointer to GET. Nothing advances it. The runtime has `xbox_Nv2aMirrorFence(device_ptr_va, put_off, get_ptr_off)` for exactly this, and `twoc/src/main.c` doesn't register it. Candidate: `xbox_Nv2aMirrorFence(0x0010EBF0, 0x30, 0x34)`.
- **Bug found: CRT memcpy drops some copies.** `sub_000F5DD0` (0xF5DD0..0xF606A) has two `jmp [reg*4+table]` instructions the lifter didn't turn into switches (0xF5E1D and 0xF5E24; the second indexes with a negative `ecx` and reads the table at 0xF5F18). They were lifted as `RECOMP_ITAIL`, the targets are mid-function labels, the indirect call fails, and the copy is skipped. Seen as `[ICALL] Failed to resolve VA` at 0xF5F30, 0xF606C, 0xF60A7, 0xF60D8. Fix is in the lifter (`xboxrecomp/tools/`, needs agreement) or a hand-written memcpy in `twoc/src/recomp_manual.c`.
- **Correction to section 15.3:** the `sub_0014FF0C` stub (recorded as 0x14FF0C..0x153708) runs 0x308 bytes into the XPP section (starts 0x153400) and covers real code at 0x153600 (*corrected in section 18.6: it is a real function, 0x153600 to 0x153708, now seeded*). Failed calls to 0x153600 and 0x153EE5 (an undetected function start) follow from it. XPP init therefore doesn't run, so no input; the planned `xbox_input` replacement (section 15.1) sidesteps it.
- **Other observations:** `NtFreeVirtualMemory failed err=87` with garbage 64-bit-looking bases (`kernel_memory.c:306`, runtime side, inferred cause: 64-bit read of a 32-bit guest pointer). The runtime writes `xbox_kernel.log` into the working directory and it isn't gitignored yet. At the hang, `[0x4F2540]=640`, `[0x4F2544]=480` and `[0x945284]=0` (no widescreen/480p flag bits), which answers part of the open check in section 14.9.
- **Proposed order:** (1) register the fence mirror and re-run; (2) try `RECOMP_PB_EXEC=1` to see whether the toolkit's push-buffer executor produces a first image, which decides whether the API-entry-point D3D plan in section 15.1 stays; (3) work around or fix memcpy; (4) XPP later.

---

## 18. Bring-up log: steps 1 and 2 (Sep 22, 2026)

Reports live in the session scratch folder (`step1_report.md`, and so on), not in the repo. Rule for this section: fixes stay in `twoc/`. Nothing in `xboxrecomp/` is changed and nothing is raised upstream.

**Rule refinement (18.9, Sep 22, 2026):** "stays in `twoc/`" now allows small, additive, non-behavioral exports from `xboxrecomp/` — a new function that only exposes an existing internal lookup to outside callers, with no change to what any existing function does. Reason: some fixes need a piece of state or logic that's already sitting in the runtime (a handle table, a resolved pointer) but is `static` to one file. Duplicating it in `twoc/` is worse than a one-line accessor: it drifts from the real implementation and hides the connection between the two. Still off-limits: anything that changes existing behavior, anything upstream (no PRs, no pushes — local commits only in the `xboxrecomp/` clone, recorded here with their hash), and anything bigger than a thin accessor (that stays a `twoc/`-only hand-write, even if slower).

**Rule refinement (18.29, Sep 24, 2026): a perf-only exception.** A small local change in `xboxrecomp/` is also allowed when it only makes the runtime faster and the output stays identical, for example caching a `getenv` that's read on a hot path. Log volume counts as a perf change too, if the log line is kept available: rate-limiting a trace line that's implied by another switch is allowed, and the full line stays behind its own variable. Same conditions as the accessors: document it here with its hash, commit it locally only in the `xboxrecomp/` clone (like `f8da7ec`), never push it, never raise it upstream. Still off-limits: anything that changes what the game or the emulated hardware does (for example the order of GET updates in the NV2A ack thread), even if it's faster.

**Rule refinement (18.32, Sep 25, 2026): `xbox_d3d8` fixes as a last resort.** The Option B bridge renders through `xboxrecomp/`'s `xbox_d3d8` (D3D8 on D3D11). Gaps there (stream 0 only, declarations ignored, no CopyRects, no ZBias, and so on) are worked around in `twoc/` first. A local fix inside `xbox_d3d8` is allowed only when a translator bug (vertex-shader microcode, register combiners) leaves no work-around in `twoc/`. Same conditions as the accessors: committed locally only in the `xboxrecomp/` clone, never pushed, never raised upstream, and recorded here with its hash. Reason: no path the game runs today uses `xbox_d3d8` (its only other user, the runtime's video pump, never starts for Crash), so such a fix can't change current behaviour, and a `twoc/` copy of a translator would drift from the original. Everything else in `xboxrecomp/` stays under the 18.9 and 18.29 rules.

**Rule refinement (18.44, Sep 28, 2026): the APU's physical addressing.** The user approved option (a) of step 41's audio scoping: a local, never-pushed change to the emulated APU's physical-address accessors in `xboxrecomp/`, paired with a hand-written `MmGetPhysicalAddress` in `twoc/`. It is `853b2ee` (`src/apu/apu_shim.h`'s `apu_phys` and the stream ADPCM copy in `apu_vp.c`): an address with bit 26 set is low RAM, any other a contiguous-window offset. Reason: the runtime keeps the contiguous window apart from the RAM at VA 0 (`xbox_memory_layout.c`), so the APU read DSOUND's voices, SGE lists and buffers from the wrong memory, and no `twoc/` change can reach the APU's reads. Scope: the physical accessors only. The rest of the APU (the monitor, the mixdown, the DSP stubs) stays under the 18.9 and 18.29 rules; 18.44 found the monitor discards the VP's output, and changing that needs its own decision. Same conditions as the others: committed locally only, never pushed, never raised upstream.

**Rule refinement (18.45, Sep 28, 2026): the APU's output path.** The user approved changing the APU monitor in `xboxrecomp/` (local, never pushed). Two local commits came out of step 43:
- `31b7165`, the approved change: `mcpx_apu_monitor_frame` keeps the 256 samples the VP and the DSP stub produced, mixes the test tone and the HLE software mixer on top only when they're on, and submits exactly those samples. It also paces the frame loop (`throttle`, next to the monitor) by the device's queue, as xemu's monitor does: the wall-clock throttle made 40-45 kHz.
- `4b439c7`, **beyond the word "monitor", flagged for the user**: the VP's `voice_resample` resamples each voice to its pitch (it played every voice at 48 kHz: 8.8% fast), and returns -1 when a voice gives nothing (it returned 0, which spins the frame thread forever once streams are refilled from the APU interrupt). Without it the sources can't match and item 2 hangs the game. It is its own commit, so dropping it is one `git reset`, but then `twoc` `a8bd0c6` has to go too.

Scope: the monitor, its pacing and the VP's resampler. The rest of the APU (the DSP stubs, the front end's handling of PIO methods while trapped, raising interrupts) stays under the 18.9 and 18.29 rules; the interrupt is delivered from `twoc/`. Same conditions as the others: committed locally only, never pushed, never raised upstream.

### 18.1 Step 1: fence mirror. Worked.

- **Change:** `twoc/src/main.c` now calls `xbox_Nv2aMirrorFence(0x0010EBF0, 0x30, 0x34)` before the guest entry point.
- **Result:** the section 17 hang is gone. Kernel calls went from 3,629 (stalled) to 17,782 in 25 s, and the first swap completes. `AvSetDisplayMode` is now reached and the framebuffer window opens (640x480). The game loads `levels\b\pause`, `levels\b\hub` (`.nux`, `.ter`), `stuff\xboxmods01.nus`, `levels\b\titles\title.nux`, `stuff\font.nux` and `memoryfiles.dat`, and NV2A USER PUT advances to 0x14F60. The section 17 diagnosis is confirmed by this.
- **No pixels yet:** the dump (about 10 s in) is entirely black. Expected, since nothing executes the push buffer without `RECOMP_PB_EXEC`.
- **Correction:** the game was not still loading at 25 s. The kernel-call log stops at #17782 (about 10 s in) and the ICALL total at the watchdog (37,498) is unchanged in step 2. The fence hang was fixed, and the game then stalled in a second wait (18.2). The watchdog is a fixed timer, so "no exit after Ns" alone proves nothing.

### 18.2 Step 2: push-buffer executor (`RECOMP_PB_EXEC=1 RECOMP_PB_SCAN=1`). First pixels. No code change.

- **Result:** the framebuffer shows the **Universal Interactive Studios splash** (from `gfx\copyr1.raw`), clean at 640x480, in the timed dump and in 11 more dumps the executor writes. The recompiled D3D library drives the toolkit's NV2A push-buffer executor, so this route works so far.
- **Executor stats:** 16 clears, 30 draws, 90 triangles, about 13.8 M pixels written. **13,820 unhandled methods (314 distinct)**, which will limit later scenes. The push-buffer survey shows 363 distinct (subchannel, method) pairs and 0 unrecognised words.
- **The game then stops on the splash** (same ICALL total 37,498, same esp, last file `memoryfiles.dat`, with or without the executor). It is in `sub_000875A0` (splash routine, called from `sub_00087BF0` at 0x87CF0), busy-waiting at 0x87782 for `[0x1BABB0]` to reach 0x78 (120 ticks, about 2 s at 60 Hz).
- **What advances it (inferred from code, not yet tested):** `sub_00039910` is `inc [0x1BABB0]; ret`, registered at 0x3B0FA with `push 0x39910`. That is the D3D vertical-blank callback. The D3D library connected GPU interrupt vector 3 (ISR 0x106580), but nothing raises vblank. The runtime has an opt-in, `RECOMP_VBLANK` (`kernel_bridge.c`, `kernel_vblank_tick`), which sets the PCRTC vblank bit and calls the vector-3 ISR every 16 ms.
- **Next:** test `RECOMP_VBLANK=1 RECOMP_PB_EXEC=1`, then the memcpy workaround (18.3).

### 18.3 Lifter gap: jump tables with a negative index (recorded, not fixed)

The lifter turns some `jmp [reg*4 + table]` into a `switch` but not these two, in the CRT memcpy `sub_000F5DD0`. They are emitted as `RECOMP_ITAIL(MEM32(...))`, which needs a function entry, so the mid-function targets fail and the copy is skipped.

Minimal repro from `xboxrecomp/` after a lift:

```
000F5E13: 83 e9 04               sub ecx, 4
000F5E16: 72 0c                  jb 0xf5e24
...
000F5E1D: ff 24 85 30 5e 0f 00   jmp dword ptr [eax*4 + 0xf5e30]   ; not recognised
000F5E24: ff 24 8d 28 5f 0f 00   jmp dword ptr [ecx*4 + 0xf5f28]   ; not recognised
000F5E2C: ff 24 8d ac 5e 0f 00   jmp dword ptr [ecx*4 + 0xf5eac]   ; recognised (8 entries)
```

- Table data at 0xF5F18 (4 entries): `F5F28 F5F30 F5F3C F5F50`, used by other jumps as `[edx*4+0xF5F18]`, which the lifter does recognise.
- The 0xF5E24 jump uses base 0xF5F28 with `ecx` in -4..-1 after `sub ecx,4` / `jb`. The table ends at the base, so entries sit at negative offsets. This is probably why detection fails (inferred).
- The 0xF5E1D jump uses base 0xF5E30 with `eax = edi & 3`. Entry 0 (`[0xF5E30]` = `0x90000F5E`) is not a code address, and entries 1 to 3 (`F5E40 F5E6C F5E90`) are. Since the `and eax,3` result is presumably never 0 on that path, the lifter may be rejecting the table for its junk first entry (inferred).
- **Symptom:** `grep -n "RECOMP_ITAIL(MEM32(ecx \* 4 + 0xF5F28))" twoc/src/recomp/gen/recomp_0002.c` finds it. The run shows `[ICALL] Failed to resolve VA 0x000F5F30`, `0x000F606C`, `0x000F60A7` or `0x000F60D8`, depending on the copy size.
- **Workaround plan (step 3, in `twoc/` only):** a hand-written memcpy in `twoc/src/recomp_manual.c`, via `--exclude-manual`, then re-lift. *(Done, see section 18.8.)*

### 18.4 Runtime issues seen, recorded, not fixed

- **D: `NtFreeVirtualMemory` failures** (`xboxrecomp/src/kernel/kernel_memory.c:306`): 51 warnings in the step 1 run (`err=87`, and one `err=487`). Bases such as `0x00F7FAB001ED2000` look like two adjacent 32-bit guest words read as one 64-bit pointer (inferred). Effect: CRT heap frees fail (a leak).
- **Init noise in `xbox_kernel.log`:** `Unresolved kernel ordinal 0` x231 and a warning "232 kernel imports are unresolved" (yet the bridge reports 113/113 resolved). Not investigated. `AvSendTVEncoderOption: unknown option 0x0F`.
- `xbox_kernel.log` is written into the working directory. It is now in `.gitignore`.

### 18.5 Step 3: vblank test (`RECOMP_VBLANK=1`). No progress: the vblank ISR is not lifted.

Env-only run, no code change (`RECOMP_VBLANK=1 RECOMP_PB_EXEC=1 RECOMP_PB_SCAN=1`, watchdog 25 s). Report: `step3_vblank_report.md` in the session scratch folder.

- **Result:** still the splash. Exit 3 from the watchdog, and the kernel-call log stops at #17782, the same as step 2. The 10 dumps hold only two alternating images, both the same splash (one has a thin black bar at the bottom edge). No new screen.
- **Cause (checked in the logs and the disassembly):** the runtime's vblank tick fires, but prints `[NV2A] vblank -> ISR not callable`. `kernel_raise_interrupt` (`kernel_bridge.c:1926`) can't find code for the connected routine, vector 3 -> **0x00106580**. That address is **not in `functions.json`**. The disassembler finds function starts through call targets, and this routine is only ever passed to `KeConnectInterrupt`. So it was never lifted. `sub_00106570` ends at 0x106573, then nops, and the real code runs 0x106580 to 0x106697 (`ret 8`). It's the same class of gap as the undetected 0x153EE5 (section 17).
- **What the ISR does (from disassembly, not run):** the context is the D3D device. It returns FALSE unless `[dev+0xA0]` and `[[dev]+0x140]` are nonzero. On the main path it may read I/O port 0x80C0 (`in al, dx`, which the lifter emits as a no-op), may call `sub_00105AB0`, clears `[[dev]+0x140]`, then calls `KeInsertQueueDpc(dev+0x84, 0, 0)` (thunk 0x15ABDC) and returns TRUE. The queued DPC presumably ends up calling the game's vblank callback `sub_00039910`, which does `inc [0x1BABB0]`. Not traced yet.
- **Options (none tried):** (1) add 0x106580 as a function start and re-lift, if there's a way to do that without touching `xboxrecomp/`; (2) hand-write the ISR in `twoc/src/recomp_manual.c` (queue the DPC and return TRUE), which `kernel_raise_interrupt` already finds through `recomp_lookup_manual`; (3) hand-write a bypass that increments `[0x1BABB0]` directly, which is the least faithful.
- **Next:** find out which DPC or callback sits at `dev+0x84`, then decide between (1) and (2). The memcpy workaround (18.3) is still waiting.

### 18.6 Step 4: pointer-only function audit, seeded re-lift, vblank rerun

Report: `step4_seed_report.md` in the session scratch folder (with `seed_table.txt`). Changes are in `twoc/` only.

- **Chain confirmed (disassembly, then a run with `RECOMP_PEEK`):** the vblank interrupt -> ISR 0x106580 -> `KeInsertQueueDpc(ctx+0x84)`, whose DPC is `sub_00106E60` -> `sub_00106720` -> `KeSetEvent` plus `call [ctx+0x1C8]`. Slot `ctx+0x1C8` is `0x10E540`, and `sub_000FEC90` (the D3D SetVerticalBlankCallback) stores 0x39910 there. Then `sub_00039910` does `inc [0x1BABB0]`. Here `ctx` = the static interrupt object 0x10E378, at device+0x2268, and the device itself is static at 0x10C110 (`[0x10EBF0]`).
- **Audit of functions taken by pointer only, missing from `functions.json`:**
  - The CreateThread start routines **0x2BF30, 0x86BC0, 0x5DA30** (so three game threads had no code) and the vblank ISR **0x106580**.
  - Every KeInitializeDpc routine and every other KeInitializeInterrupt routine was already present.
  - 46 more starts were found: callbacks pushed as immediates in game code, function-pointer stores, and vtable slots in `.data`. Four of them (0x32740, 0x32890, 0x329E0, 0x49950) sat inside existing functions and now split them.
  - 0x153EE5 and 0x153600 were added by hand. **Correction to section 17:** 0x153600 is real code (0x153600 to 0x153708), not device-table init. The bogus `sub_0014FF0C` had swallowed it, and both bogus stubs now end at 0x153600.
- **Method that worked:** `tools.disasm --seed-functions` takes extra starts without any change to `xboxrecomp/`. New files: `twoc/seed_functions.json` (52 addresses) and `twoc/analyze.ps1` (disasm with seeds, func_id, abi_analysis). Run them with `powershell -NoProfile -ExecutionPolicy Bypass -File twoc\analyze.ps1`, then `lift.ps1`. Script execution is off on this machine, so the bypass is needed per process.
- **Seeds must not point into nop padding.** The first attempt included 0x416BC (a D3D hardware constant, not a pointer) and 0x52078, both inside padding. They swallowed 5 real starts (0x41760, 0x41784, 0x417AC, 0x417BC, 0x52080), so they were dropped. The final check compares `functions.json` before and after: +52 functions, 0 lost.
- **Result of the re-lift:** 4,662 functions, 4,660 lifted (the other 2 are the manual data stubs), 0 failed, 775,582 lines. Unresolved call stubs went from 65 to 64. The build has 0 errors.
- **Vblank rerun (`RECOMP_VBLANK=1 RECOMP_PB_EXEC=1 RECOMP_PB_SCAN=1`):** the ISR is now claimed (`[NV2A] vblank -> ISR claimed it`), and the ICALL failures at 0x153EE5 and 0x153600 are gone. Only the memcpy ones remain. **Still the splash**, the same two images. `[0x1BABB0]` reads 2 to 3 instead of reaching 0x78.
- **New blocker (cause found in the runtime source, not fixed):** the DPC only runs its vblank branch when PMC_INTR_0 bit 24 reads set. The runtime tick sets it once per 16 ms, but `nv2a_ack_thread` (`xbox_memory_layout.c`, table `NV2A_ACK`) clears PMC_INTR_0 and PCRTC_INTR_0 in a loop with only `Sleep(0)`, so the DPC nearly always sees 0. Over 25 s there were 601 ISR -> KeInsertQueueDpc calls but only 16 KeSetEvent calls.
- **Options (none tried):** (a) a hand-written replacement for `sub_00106E60` in `twoc/src/recomp_manual.c` that runs the vblank part and the `[ctx+0x1C8]` callback directly; (b) the same for `sub_00106720` only; (c) call the 0x39910 callback from a hook on the tick, which is the least faithful.
- **Audit caveat:** heuristic. It finds pointer-taken starts, not starts reached only by computed addresses. The disassembler leaves 121 gaps of 48 bytes or more in `.text`, not all of them code, and those were not audited further.

### 18.7 Step 5: hand-written D3D DPC. Past the splash: first new screen.

Report: `step5_dpc_report.md` in the session scratch folder. Change in `twoc/src/recomp_manual.c` only.

- **No documented switch exists for the ack thread.** I checked every `RECOMP_*` environment variable in the runtime and the docs. The thread starts unconditionally whenever the NV2A aperture exists (`xbox_Nv2aAckStart`, called from `xbox_memory_layout.c`). Its register table and stop flag are static, and nothing in the header exposes them. `RECOMP_VBLANK` only enables the tick that sets the pending bits.
- **The fix (option a):** a hand-written `sub_00106E60` (the D3D interrupt DPC) in `recomp_manual.c`, written from the disassembly. It keeps every branch. The one change: on the first pass of each DPC invocation the pending mask is OR-ed with 0x1000000 (vblank), because the ISR is raised only by the tick and only for a vblank. Later passes read PMC_INTR_0 as given. `recomp_manual.c` now defines `RECOMP_GENERATED_CODE` and includes `recomp/gen/recomp_funcs.h`. The old local `g_eax` and other declarations were removed (the local `g_eax` lacked `RECOMP_TLS`). Re-lifted with the existing `lift.ps1` (`--exclude-manual`): 4,659 of 4,662 lifted, 0 failed. The 3 skipped are the two data stubs and this function.
- **Result of `RECOMP_VBLANK=1 RECOMP_PB_EXEC=1 RECOMP_PB_SCAN=1` (25 s watchdog):** `[0x1BABB0]` now counts (0x244 at the watchdog). The game leaves the splash and loads the pause, hub and title levels, particles and cubemaps, `titles.dat`, then `levels\b\logos\logos.nux`, `logos1.cut` and `logos2.cut`, which is the engine's logo sequence. It is still running at 25 s (the kernel-call count is 74k, the vblank ISR and DPC keep firing, and the main thread is in `KeDelayExecutionThread`).
- **New screen:** the last executor dump (`d009`) is a full-screen blocky green gradient, bright at the top left, with no text. It looks like a low-resolution or badly filtered background quad, but I haven't identified the scene. The `RECOMP_FB_WINDOW` snapshot at about 10 s is black.
- **Executor:** 3,021 draws, 6,654 triangles, 286 batches skipped as not screen-space, and **689,147 unhandled methods (365 distinct)**, up from 13,820 (314) at the splash. This is the likely reason the picture isn't recognisable. Only the two CRT memcpy ICALL failures remain, and there is no crash.
- **Still not done:** the memcpy workaround (18.3). `sub_00106720` has an `in al, dx` (port 0x80C0) that the lifter emits as a no-op. Run longer than 25 s to see later screens.

### 18.8 Step 6: memmove replaced, 90 s run, unhandled methods, run script

Report: `step6_memcpy_run_report.md` in the session scratch folder. Changes in `twoc/` only.

- **memmove (18.3) fixed.** `sub_000F5DD0` is the CRT memmove that the game also uses as memcpy (`void *memmove(dst, src, n)`, cdecl, returns dst). `recomp_manual.c` now defines it with the host `memmove` on the guest addresses, so every size, alignment and overlap is correct. Re-lifted with `--exclude-manual`: 4,658 of 4,662 lifted, 0 failed, unresolved call stubs 65 -> 62. **No `[ICALL] Failed to resolve` line appears in any run any more.** The old failures at 0xF5F30, 0xF606C, 0xF60A7 and 0xF60D8 were jump-table targets inside this one function. It was the only function in the CRT range with the problem.
- **90 s run (`RECOMP_VBLANK=1 RECOMP_PB_EXEC=1`):** 16 dumps, 3 distinct images: the two splash frames, then the green frame (md5 de9ab167...). **The green frame does not change.** It is byte-identical to the previous run's and across all dumps from about 25 s to 90 s. The executor writes only the first 8 frames with geometry and then one picture per report, and reports are hard-coded to about every 10 s, so that is the finest cadence without touching `xboxrecomp/`.
- **The game stops drawing at about 30 s.** After `logos2.cut` (the last file opened) the executor totals freeze (clears 142, 785,988 unhandled methods, 3,738 draws, 6,933 triangles), while the vblank counter keeps rising.
- **New blocker: the game waits forever for a thread to finish.** The main thread loops on `ObReferenceObjectByHandle` / `ObfDereferenceObject` / `KeDelayExecutionThread`. These are two XAPI helpers: `sub_000ECB2B` (GetExitCodeThread) and `sub_000ECCE5` (Sleep). The stack shows the caller chain `sub_00087BF0` (main loop) -> 0x2DA42 -> `sub_0002C020` (creates the thread with start routine 0x2BF30 and keeps its handle in `[0x1BAA8C]`). Which thread it waits for is inferred from that return address. The runtime's `ObReferenceObjectByHandle` (`kernel_bridge.c:3518`) always writes `*Object = 0`, so the game reads guest address 4 and gets exit code 0x103 (STILL_ACTIVE) forever, even though a worker thread does terminate (`PsTerminateSystemThread`, call #74361).
- **Options for the thread wait (none tried):** (a) hand-write `sub_000ECB2B` so it returns the real exit code of the host thread behind the handle (needs checking that the handle is a host HANDLE); (b) report the thread as finished, which is least faithful and first needs knowing what the thread at 0x2BF30 does.
- **Top unhandled NV2A methods (674,891 in the 45 s run, 365 distinct):** none is a draw command. The top 20 are `NO_OPERATION` (25,349), `WAIT_FOR_IDLE` (12,675), then vertex-program state: `SET_TRANSFORM_CONSTANT_LOAD` and `SET_TRANSFORM_CONSTANT[0..3]` (8,499 each), `SET_VIEWPORT_OFFSET` and `SET_CLIP_MIN/MAX` (6,768 each), `SET_VIEWPORT_SCALE` (4,876 each), `SET_TRANSFORM_EXECUTION_MODE` (4,655, last value 6) and `SET_TRANSFORM_PROGRAM` dwords (4,592 each). The game runs vertex programs and the executor does not, so only batches that are already in screen space get drawn (910 batches skipped as not screen-space of 3,738 draws). That is the likely reason the picture is one green quad. The full table with meanings is in the report.
- **How to run:** `powershell -NoProfile -ExecutionPolicy Bypass -File twoc\run_twoc.ps1` from anywhere. It sets `RECOMP_VBLANK=1`, `RECOMP_PB_EXEC=1` and `RECOMP_WATCHDOG_SECS=90`, starts the exe from the repo root, and kills it after Seconds + 30 s at the latest. Options: `-Seconds N`, `-DumpDir dumps` (framebuffer BMPs `shot000.bmp` ...), `-Window` (the 640x480 window, which stays black for now). Logs go to `run_twoc.out.log` and `run_twoc.err.log` (gitignored, like `dumps/`). Exit code 3 means the watchdog ended the run, which is the normal end for now. `RECOMP_PB_SCAN` is not needed. Build first with `cmake --build twoc\build --config Release --parallel`.

### 18.9 Step 7: GetExitCodeThread fixed. Thread-wait blocker gone. New blocker: a crash in DSOUND.

Report: `step7_thread_wait_report.md` in the session scratch folder.

- **Step 1 confirmed the polled handle first**, with a temporary diagnostic-only edit to `xboxrecomp/src/kernel/kernel_bridge.c` (reverted with `git checkout` before anything else was changed): every `ObReferenceObjectByHandle` call in the run polls the same handle token (`0x48000001`, type `PsThreadObjectType`), and that token is exactly the one written back for the thread spawned with start-context `0x2BF30`. Correction to section 18.8's framing: the bridge's `start_routine` argument for that spawn is always the fixed XAPI trampoline `0xECBD2`; `0x2BF30` is the thread's start *context* (its real proc), not the routine the kernel sees. The wait is confirmed to be on the right thread.
- **Root cause, pinned down exactly** (from the disassembly of `sub_000ECB2B`, `xboxrecomp/tools/disasm/output/asm/text.asm:306621`): XAPI's `GetExitCodeThread` resolves the handle via `ObReferenceObjectByHandle(hThread, ObThreadObjectType, &Object)`, reusing the `hThread` stack slot itself as the `&Object` out-parameter, then reads a "terminated" byte at `Object+4` to choose between the real exit status at `Object+0x120` and `STILL_ACTIVE` (0x103). The bridge's `ObReferenceObjectByHandle` always writes `Object = NULL`, so `Object+4` is always guest address 4, always reads 0, and the function always took the `STILL_ACTIVE` branch.
- **The fix (option 3, approved):** a new additive accessor `xbox_bridge_resolve_handle(uint32_t token)` in `xboxrecomp/` (declared in `xbox_memory_layout.h`, defined in `kernel_bridge.c` as a one-line wrapper around the existing `bridge_resolve_handle`) exposes the handle table to outside callers. `sub_000ECB2B` is now hand-written in `twoc/src/recomp_manual.c`: it resolves the handle to the real Win32 `HANDLE` (every worker thread's handle is already a genuine `CreateThread` handle in that table) and calls the real `GetExitCodeThread` on it directly, skipping the bridge's object layer entirely. `PsTerminateSystemThread` already calls the real `ExitThread(ExitStatus)` for a worker, so this gives the exact right answer with no KTHREAD field reconstruction. Falls back to the old `STILL_ACTIVE`/success behavior for any handle that doesn't resolve (the main thread never gets a real spawned handle; there are 3 other call sites of this function besides the one confirmed here). Re-lifted (5 functions now excluded via `--exclude-manual`), rebuilt clean.
- **`xboxrecomp/` change committed locally only**, not pushed, not raised upstream: commit `52531ac` on top of the pinned `766ecef`. Two files, 23 lines, purely additive (see rule refinement above).
- **Result of a 90 s run (`RECOMP_VBLANK=1 RECOMP_PB_EXEC=1`):** the blocker is gone. The worker thread finishes (`PsTerminateSystemThread: status=0x00000000 (worker)`, real exit code 0), the main thread's poll loop exits immediately afterward instead of spinning forever, and the game gets about 100 kernel calls further than it ever had (~76,226 vs. the old ~74,361 stopping point) before hitting a **new access violation**: a write through a wild pointer (`eax=0xB3BBBD2E`, not inside any mapped guest region) reached through the main loop → a `.text` cluster already flagged as DSOUND-adjacent (section 14.4) → straight into the statically-linked **DSOUND library** (`sub_00136D09` → `sub_001367B9` → `sub_00139A36` → `sub_001388D6`, innermost). No new screen: the framebuffer dumps are still the same green frame (one dump, `shot009`, looks like a torn/partial frame from mid-crash, not a new scene).
- **Read on this:** real forward progress, not a bug in the fix — the fix did exactly what it was supposed to (deliver the real exit status instead of eternal `STILL_ACTIVE`), and that unblocked code that had never run before, which promptly hit the next pre-existing bug. Plausible causes, not yet checked: the `0x2BF30` worker's role (still "not determined" going into this, per section 14.7) may be audio init/setup, so this is the main thread consuming its results before they're ready; or, independent of that, DSOUND's ABI signatures are as unverified as everything else in the library set (section 14.6 — 75% low-confidence defaults), so this could be a plain parameter-passing mismatch surfacing on the first real call into this path.
- **Next:** trace `sub_000B9890` → `sub_00136D09` → `sub_001367B9` → `sub_00139A36` → `sub_001388D6` to find what feeds the bad pointer.

### 18.10 Step 8: the DSOUND crash, traced innermost-out. No fix yet.

Report: `step8_dsound_crash_trace.md` in the session scratch folder. No code changed this step (static trace only).

- **The write happens in `sub_0013A35E`**, through its first stack argument (`eax = MEM32(ebp+8)`, ten `fstp`s writing a 40-byte block — reads as a rotation/orientation matrix built from an angle (`fsincos`) and a direction vector). Confirmed the lifted C (`recomp_0004.c:16111`) matches this instruction-for-instruction — **not a lifter bug**; the same crash would happen on real hardware given the same uninitialized state.
- **Traced the pointer back three more calls:** `sub_0013A35E`'s arg 1 = `*(this+0)` inside `sub_001388D6`; that `this` = `(sub_00139A36's this) + 0x58` (an embedded sub-struct, passed by address, not a separate allocation); `sub_00139A36`'s `this` = `*(manager + 0xc)` inside `sub_001367B9`, where `manager` is `sub_001367B9`'s own stack argument. So the crash is `*(BigObject + 0x58)` used as a destination pointer, where `BigObject` is whatever `sub_001367B9`'s caller (`sub_00136D09`, in turn called from the DSOUND game-wrapper cluster, `sub_000B9890`) is currently treating as "the" 3D voice/listener. Not traced further back than that.
- **Also notable:** `sub_00139A36` calls into the crashing chain unconditionally, before its own "is 3D enabled" flag check — there's no active/created guard in front of the part that crashed.
- **Both proposed hypotheses checked, neither confirmed:**
  - **ABI/param mismatch:** checked. Every call in the chain is direct (fixed address, not indirect/vtable), and the calling convention (thiscall `this` in ecx, explicit stack args, explicit `ret N`) was hand-decoded from the raw bytes and matches the lifted C exactly. Not supported.
  - **Set up by the `0x2BF30` worker:** checked by reading the worker's own code (`sub_0002BF30`, `text.asm:35740`) directly. It's a critical-section-guarded loop that looks like a generic XAPI notification/message-pump thread — nothing DSOUND-related visible in it. Not well supported.
  - **More likely (unconfirmed):** this per-voice transform update is simply running for the first time ever, now that the main loop can get past the old wait — on a slot whose `+0x58` sub-object was never populated by whatever DSOUND setup call (a `Create3DBuffer`-shaped one, going by the field's use) is supposed to run first. If so, the `GetExitCodeThread` fix didn't cause this or feed it bad data — it just reached a pre-existing gap for the first time.
- **Proposed fix:** not written yet — needs one more trace pass (where `manager`/`BigObject` are allocated, and what should populate `BigObject+0x58`) starting from `sub_00136D09` and `sub_000B9890` upward, and a check of whether DSOUND has an unserviced interrupt/DPC (DSOUND is interrupt-driven, section 14.4), by analogy to the vblank-ISR fix (18.7).
- **Whether it can stay in `twoc/`:** likely yes on present evidence — everything traced so far is recompiled DSOUND library or game code, not a bridge gap. If the missing piece turns out to be an unserviced DSOUND interrupt, the fix would look like 18.7 (a hand-written DPC in `recomp_manual.c`, no `xboxrecomp/` change). Only a genuinely missing runtime capability (no way to signal something from `twoc/` at all) would need another small additive export like 18.9's — nothing found so far points that way.

### 18.11 Step 9: DSOUND trace continued — `manager`/`BigObject` origin, guard bit, interrupt gap confirmed. Still no fix.

Report: `step9_dsound_deeper_trace.md` in the session scratch folder. No code changed (static trace only, same as 18.10).

- **`manager`'s origin traced:** `sub_00136D09` adjusts a COM-style interface pointer by `-8` (null-safe) to get `manager`; that interface pointer is `*(0x427A58)` — a **fixed static address**, not a per-voice allocation. Confirmed by grepping every occurrence of `0x427a58` in `text.asm` (34 hits, all the identical `mov ecx, 0x427a58` this-pointer load, clustered across 0xEC391–0xEC960 — a large family of one-line DirectSound3DListener/Buffer property-setter trampolines). None of `sub_000B96B0`, `sub_000B96D0`, or `sub_000B9890` write to `[0x427a58]`; all three only read and forward it. So `0x427A58` is a single static "3D listener" object, and whatever sets its field-0 interface pointer lives outside this per-frame cluster, not yet located.
- **Proved the pointer chain is genuinely valid, not null:** the XBE's `.data` section has `raw_size` (0x47F88) far smaller than its `virtual_size` (0x7D92F8); `0x427A58` sits well past that boundary, in pure zero-fill BSS — so at load time `*(0x427A58)` is guaranteed zero. Since the crash pointer (`0xB3BBBD2E`) isn't near-zero garbage, `*(0x427A58)`, `manager`, and `BigObject` must all be getting populated with real pointers by runtime code before the crash. The gap is narrower than "the whole chain is dead" — specifically `BigObject+0x58`'s 40 bytes are bad, not the pointers leading to it.
- **New finding: `sub_001388D6` has its own internal guard, and it passed.** Missed this in 18.10 — its prologue tests bit 1 of byte 1 of its own stack argument (a status dword read from `[BigObject+0xc]+0xa4`) and skips all work if clear. The crash happening at all means this guard bit was *set* — the game's own state said "this voice is 3D-active, proceed" — and only then did the write through the still-uninitialized sub-structure fail. Points at an ordering/pairing bug: whatever sets that status bit and whatever populates `+0x58` are two separate steps, and this run did the first without the second. Tried to find the status-bit setter by grepping the DSOUND wrapper cluster for `+0xa4` writes — inconclusive, that offset is too generic to search blindly without a concrete object address; not reporting a guess.
- **Interrupt/DPC check, as requested — confirmed real, not confirmed causal.** DSOUND connects two interrupt vectors (`KeConnectInterrupt` at `0x0013A198` and `0x0013FDB1`, matching section 14.4). The runtime's *only* interrupt-delivery call site anywhere (`kernel_raise_interrupt`, in `kernel_bridge.c`) is invoked from exactly one place, `kernel_vblank_tick`, and only for `NV2A_VECTOR` — nothing ever services DSOUND's two vectors. Structurally identical to the pre-18.7 vblank gap. But the traced crash chain is a plain synchronous call chain with no wait/event/DPC in it, so no direct evidence ties this specific crash to those two unserviced interrupts — plausible (an init step could be silently stuck waiting on one of them, the same shape as the vblank bug, just not hanging this time) but unconfirmed.
- **Fix not written — genuinely conditional on one more check:** a read-only diagnostic run (no code change, same shape as step 7's temporary logging) to capture `BigObject`'s real runtime address and watch `+0x58`/`+0xc`/`+0xa4` across a few frames before the crash, which would both confirm "never populated" vs. "populated too late" and give a concrete address to search the disassembly from. If that points at the interrupt gap, the fix is a hand-written DPC override (18.7's pattern, `twoc/`-only). If it's an ordering gap unrelated to interrupts, the fix needs the actual missing setter identified first — patching the guard bit or fabricating `+0x58`'s values instead would be least-faithful and was explicitly avoided. Either way, everything found so far stays inside recompiled DSOUND library/game code — no bridge gap, so **still expected to stay entirely in `twoc/`**.

### 18.12 Step 10: the diagnostic ran. `BigObject` is null — corrects 18.11's conclusion.

Report: `step10_dsound_diagnostic_result.md` in the session scratch folder. Temporary code only, fully reverted (confirmed via `git diff --stat` before and `git checkout --` after); re-lifted and rebuilt afterward to restore the committed state (back to 5 excluded functions, matching `fe91c02`).

- **The documented `recomp_lookup_manual` trace-wrapper pattern doesn't work here.** Registered a trace wrapper for `sub_00139A36` first — it never fired (0 `[DIAG]` lines, crash still happened) because `sub_001367B9` calls it by a statically-resolved fixed address, and the lifter translates that as a plain direct C call, bypassing `recomp_lookup`/`recomp_lookup_manual` entirely. That lookup path only fires for genuinely indirect (`RECOMP_ICALL`) calls. Worth remembering for any future diagnostic in this codebase.
- **Had to exclude and hand-transcribe `sub_00139A36` instead**, with one log line added at entry and the rest a faithful line-for-line translation from the disassembly. Caught two real bugs in the first draft before running anything: (1) wrongly added `esp += N` after each call to another lifted function — wrong, the callee's own generated body already pops its own return address + stack args via its translation of its own `ret N`; (2) forgot to pop this function's own return address (`esp += 4`) at its own return points, since it's a bare `ret`. Both fixed by re-deriving the convention from the file's three existing hand-written functions before running.
- **Confidence in the transcription:** the crash reproduced with the *exact same* fault address as every prior run after the fix — strong evidence the transcription is faithful (a mistranscription would much more likely have changed the crash's shape than reproduced it exactly).
- **Result: `BigObject = 0x00000000`.** Only one call to `sub_00139A36` happened before the crash. `[+0xc]=0`, consistent with reading near address 0. `[+0x58]=0xB3BBBD2E` — the crashing value — is simply `MEM32(0x58)`, whatever bit pattern happens to sit at that small, low, but still mapped guest address; it has nothing to do with any real 3D-audio object.
- **This corrects 18.11.** 18.11 argued the crash value not being "near-zero" proved the pointer chain up to `BigObject` was genuinely populated. That reasoning was wrong — it assumed a near-null chain must produce a near-zero *value* on final dereference, but `BigObject+0x58` (= guest address `0x58`) is itself a small-but-valid mapped address that can (and does) hold an unrelated, arbitrary 32-bit value. The diagnostic disproves "the chain is valid, only `+0x58` is uninitialized" directly. The real picture is simpler: `BigObject` is null because `manager` (`sub_001367B9`'s arg) is null, because `*(0x427A58)` — the cached DSOUND 3D-listener interface pointer, confirmed in 18.11 to start as zero (deep in `.data`'s BSS region) — has never been populated by the time this per-frame update runs. 18.10's original hypothesis (never confirmed until now) was right; 18.11's second-guessing of it was the wrong turn.
- **Still not located:** the actual DSOUND call that should populate `*(0x427A58)`. No literal-address write was found (18.11), and this step confirmed the disassembler's xref data has no distinct "write" category to search by (only `data_read`/`data_imm`/`call`/`jump`/`cond_jump`/`kernel_call`) — so a computed-address write, if the responsible code path has even run, can't be found by grep alone.
- **Proposed fix, in order of preference, not written:**
  1. **Preferred:** find the real "create the 3D listener" call and what's stopping it from completing — possibly the confirmed-but-unproven DSOUND interrupt-servicing gap (18.11), in which case the fix is a hand-written DPC pump following the D3D vblank pattern (18.7), `twoc/`-only.
  2. **Fallback if 1 can't be pinned down in reasonable time:** a hand-written guard in `recomp_manual.c` that skips the 3D-listener maintenance chain while `*(0x427A58) == 0` — safe, small, unblocks forward progress, but less faithful (3D audio positioning silently won't run until this is revisited).
  - Both stay entirely in `twoc/`; nothing found needs a new runtime export.

### 18.13 Step 11: null-listener guard applied. Crash gone — 90 s clean, past logos into particle/cubemap loading.

Report: `step11_dsound_guard_fix_report.md` in the session scratch folder.

- **Time-boxed search for the real setup call, as planned:** confirmed (checking all 7 disassembled sections this time, not just `text.asm`) that no code anywhere writes `0x427A58` via a literal address. Traced `sub_0013A107` — the function that connects DSOUND's two interrupt vectors (18.11) — directly: it's synchronous hardware bring-up (timer init, `KeConnectInterrupt`, an MCPX APU MMIO write, a 32-iteration loop, return) with no wait/block anywhere in it, which weighs against the interrupt gap being what's stuck here. No further lead found in the time-boxed pass, so per the plan: **fix 2, the guard.**
- **The fix:** `sub_000B9890` (0xB9890–0xB9899, `text.asm`) — a 9-byte function with exactly one caller (`sub_000EC470`) — is now hand-written in `twoc/src/recomp_manual.c`. If the cached interface pointer at `0x427A58` is null, the whole downstream chain (`sub_00136D09` → `sub_001367B9` → `sub_00139A36` → `sub_001388D6` → `sub_0013A35E`, the crash site) is skipped as a no-op instead of dereferencing near-null memory. Every other function in the chain is untouched. Re-lifted (6 excluded), rebuilt clean.
- **Known, accepted limitation:** 3D audio positioning silently does not update while `0x427A58` stays null. Other audio (music, non-positional SFX) is unaffected — none of DSOUND's other ~35 game-side entry points (section 14.4) go through this guard.
- **Result: 90 s run, no crash, no freeze.** Exit code 3 (normal watchdog timeout) instead of a segfault. **305,175 kernel calls** reached (vs. ~76,000 at the old crash point), with the GPU pushbuffer still actively advancing and `[GPU] draw #16000` logged right before the watchdog fired — the game was still live and rendering at the 90 s mark, not stalled.
- **New content, never reached before:** files loaded past `logos2.cut` for the first time — `levels\b\intro\intro.ptl`, `stuff\particle.raw`, `stuff\general.ptl`, `levels\b\hub\hubcubemap.dds`, `stuff\Droplet1.dds`, `gfx\WaterCubeMap.dds` (particle systems and cubemaps). Draws grew continuously throughout the run (3,186 → 16,000, never freezing), 924,000 triangles rasterised by the end (up from the old 6,933 ceiling), and the render surface address changed over time (`0x00530000` → `0x00404000` → `0x0065C000`) — real scene changes, not a static frame. Framebuffer dumps show new, changing imagery, including a distinct curved purple/blue/white shape (`shot010.bmp`) — clearly not the old frozen green quad.
- **No new blocker hit within 90 s** — the run simply ended on the watchdog timeout with the game still running. Not yet known what happens next at longer runtime.
- **Next:** run longer than 90 s to see what comes after; revisit the real DSOUND 3D-listener setup call properly when there's time for an unbounded trace (the guard is a known, intentional gap, not a real fix); the NV2A executor's missing vertex-program support (14.10/15.4) is the next likely ceiling on picture quality regardless.

### 18.14 Step 12: 240 s run. No new blocker — the game idles at the content ceiling, GPU throughput never stalls.

Report: `step12_long_run_report.md` in the session scratch folder. Env-only run, no code change.

- **No crash, exit code 3 (normal watchdog end)** after 255 s wall clock (240 s guest watchdog + ~15 s shutdown overhead). No ICALL failures, no access-violation/exception text anywhere in the 8,914-line error log. `xbox_kernel.log` shows only the already-known, already-open init noise (`NtFreeVirtualMemory` err=87, one expected `NtResumeThread`).
- **Content ceiling unchanged from 18.13.** No `[PATH]` file loads beyond the set already logged there (`logos1.cut`/`logos2.cut`, `intro.ptl`, `particle.raw`, `general.ptl`, `hubcubemap.dds`, `Droplet1.dds`, `WaterCubeMap.dds`) — all loaded within the first ~17 s, then nothing new for the remaining ~223 s.
- **GPU throughput never decelerates.** Draw count grows linearly the whole run, ~1,460-1,520 draws per ~10 s report, 30 → 34,489 by the last periodic report, **draw #35,800** at the 240 s watchdog snapshot (~2.2x past the 90 s run's ~16,000 ceiling, same unchanged rate throughout — the engine is as active at 240 s as at 20 s). Final report: 2,240,133 triangles rasterised, 91% (2,042,216) fully off-surface — same ratio as the 90 s run, consistent with the known missing-vertex-program-support gap (14.10/15.4).
- **Framebuffer: real activity, then a second freeze.** 10 distinct frames across 31 dumps (vs. 3 across 16 at 90 s): the splash (t=0-6.6 s), then solid black from t≈17 s, then **~90 s of real changing content (t≈37-127 s)** — a purple/blue/white curved gradient shape and a sparse white wireframe/spiral pattern, both new, recurring rather than progressing (reads as an animating cubemap/spinner, not a level transition) — then **solid black again from t≈147 s through the last dump (t≈228 s) and the 240 s kill**, byte-identical across all 9 of those dumps despite draw/triangle counts still climbing unchanged. Same shape as the 90 s run's frozen-frame ending (18.8), different content, and this time preceded by real visible motion instead of starting immediately.
- **Read on this, no new blocker to chase:** the game is most plausibly idling in the hub/menu (no new files, no level change) waiting on controller input, which isn't implemented yet (XPP input, still open). An animating background element drives the brief visible changes; the NV2A executor's missing vertex-program support keeps the rest invisible. Confirms rather than changes the existing next-step list.
- **Next:** XPP input is now the most likely thing actually gating forward progress (the game is idling, not hung); the DSOUND 3D-listener setup call and the NV2A vertex-program gap are unchanged open items.

### 18.15 Step 13: XPP poll/vibration wired to xbox_input. Input still doesn't reach the game -- root cause traced to a dead controller-insertion notification, not the two hand-written functions.

Report: `step13_xpp_input_report.md` in the session scratch folder. Changes in `twoc/` only.

- **Exhaustively enumerated the 10 XPP entry points** (a script over `text.asm` finding every direct `call` from game code into the XPP section, not a manual sample): `0x1542AB` (poll), `0x15431A` (vibration), `0x154AA8`/`0x153C66` (init, a 5-byte tail-jump trampoline), `0x154AAD`, `0x154249`, `0x15429F`, `0x154805`, `0x154937` (all enumeration/capability), plus `0x154A45`/`0x154A64` which turned out to be pure leaf math (byte range-check + divide, no device-table/IRQL access) and were left lifted as-is.
- **Hand-wrote the two data-plane entries** in `recomp_manual.c` (`--exclude-manual`, same mechanism as the memmove/D3D-DPC/GetExitCodeThread/DSOUND-guard fixes): `sub_001542AB` -> `xbox_InputGetState(0, &state)` (its 22-byte output buffer is byte-for-byte `XBOX_INPUT_STATE`, confirmed from the traced copy length and field shapes -- a straight `memcpy`), `sub_0015431A` -> `xbox_InputSetState(0, &vib)` (motor words read from the caller's own struct at fixed offsets). Both traced end-to-end from caller and callee bytes, not guessed. Added one `xbox_InputInit()` call in `main.c`. Known simplification: both hardcode port 0 (single-player game, nothing in the traced ABI names the port cheaply).
- **Result: zero hits, no behavior change.** 90 s run, same content ceiling as 18.14 (65 file loads, same last file `logos2.cut`), no crash. Both hand-written functions traced zero calls the entire run -- the wiring is correct (verified by full ABI trace, not just "it compiles"), but the game never reaches the code that would call it.
- **Traced upward instead of guessing at the remaining 6 candidates**, using a temporary diagnostic (a 1-line pass-through wrapper on the trivial `0x154AA8` trampoline, reverted afterward) plus `RECOMP_PEEK` (existing, read-only, no code change) to confirm each link:
  - `sub_00086D30` (the main loop's per-frame input service) calls the poll wrapper `sub_00036330` **unconditionally every frame** -- there's no gate in front of the call itself.
  - `sub_00036330` internally checks `*[0x23c2d8]`; if null (and a second global `[0x23b750]` is also 0), it bails without ever reaching `sub_001542AB`.
  - `[0x23c2d8]` only gets populated by `sub_000EB270()`, which scans a static 4-slot table at `0x427598` (step 0xF0, 4 slots = `XBOX_MAX_CONTROLLERS`) for a slot whose first dword is nonzero ("active") -- but it only *reads* that table, never writes it.
  - Slot activation would have to come from `sub_000EB6C0` (the notification pump, confirmed to run first every frame via the diagnostic and `RECOMP_PEEK`), which pumps two hardware-notification dword pairs through `sub_00154AAD` and dispatches per-port handlers (`0x15429F` teardown, `0xEB5A0`/`0xEB340`/`0xEB2A0` -- the very functions that call the remaining enumeration XPP entries) only on set bits.
  - **`RECOMP_PEEK` confirmed at the watchdog moment**: `sub_00154AAD`'s own gate field is zero for both notification pairs (`[0x1534AC+4]` = `[0x1534B0]` = 0, `[0x153430+4]` = `[0x153434]` = 0), so it always takes the "nothing changed" branch before ever touching real hardware state, and consequently all 4 controller slots (`0x427598/427688/427778/427868`) read 0 the entire run. `sub_000EB5A0`/`0xEB340`/`0xEB2A0` -- and the capability entries behind them -- are simply never reached; nothing was miscoded there, so there was nothing useful to gain from tracing those 6 functions' own bodies further.
  - Also confirmed the earlier init link (`sub_000EB7C0`, reached via a tail-jump from `sub_00037330` off the main loop's display setup -- undiscoverable from "Called by" alone, found by grepping every section for the literal address) **does** fire once and its `ExAllocatePool(0xB4)` succeeds (`[HEAP] #4: size=180` in the log), so this isn't an unreached-init or allocation problem -- it's specifically the notification-arming flags two levels further in.
- **Read on this:** the same shape as the D3D vblank ISR (18.7) and the DSOUND 3D-listener gap (18.10-18.13) -- an interrupt/DPC-serviced notification nothing in this recomp delivers. Matches what sections 14.4 and 15.1 already flagged: XPP is interrupt-driven (`KeConnectInterrupt`, DPCs, 17 `KeSetTimer` sites), and "the toolkit's OHCI model does not yet walk endpoint or transfer-descriptor lists, so a recompiled XPP would find a controller port and never receive a report."
- **Proposed fix, not written (needs a decision, not just tracing):** (1) preferred/most faithful -- find and service whatever's supposed to set `[0x1534B0]`/`[0x153434]` (an XPP interrupt/DPC pump, `twoc/`-only, same pattern as `sub_00106E60`; not yet located), or (2) simpler/least faithful -- a third hand-written override that just marks port 0 "active" directly (e.g. at `sub_000EB270`'s read, or writing `[0x427598]` once at startup), skipping the real insertion-notification chain entirely and reusing the poll/vibration ABI work already done. Both stay in `twoc/`.
- **Files changed:** `twoc/src/recomp_manual.c` (`sub_001542AB`, `sub_0015431A`, a small rate-limited `trace_hit()` helper), `twoc/src/main.c` (one `xbox_InputInit()` call). No `xboxrecomp/` changes.

### 18.16 Step 14: XPP port arming -- faithful search dead-ended on real OHCI hardware, so a gated insertion trigger replaces the blind override.

Report: `step14_xpp_arming_report.md` in the session scratch folder. Changes in `twoc/` only.

- **Time-boxed search for option (1) (18.15): found the ISR, and it's a real hardware gate, not a missing pump.** `sub_00153600` (XPP's `DriverEntry`-shaped device-creation loop) calls `xbox_KeInitializeInterrupt`/`xbox_KeConnectInterrupt` at `0x153E96`/`0x153E9D` with `ServiceRoutine = sub_001596F4`. That ISR reads a register pair at `[device+0xc]`/`[device+0x10]` shaped exactly like OHCI's `HcInterruptStatus`/`HcInterruptEnable`, tests bit `0x80000000` (master interrupt enable) and bit `0x20` (root-hub status change -- the actual "something got plugged in" bit), and only then queues the DPC that reaches `sub_00154AAD`'s gate fields. Unlike the D3D vblank gap (18.7), the DPC here is correctly wired to a real interrupt; that interrupt is a genuine OHCI root-hub-status-change IRQ this recomp's USB/host-controller emulation (14.4/15.1: doesn't walk far enough to raise it) never generates. Servicing this for real means building OHCI root-hub-status emulation -- out of scope for a time-boxed pass, so this stopped here per the "if not found quickly" instruction.
- **The originally-sketched fallback (blind write of `[0x427598]`) turns out to be unsafe, traced this pass.** `sub_000EB270`'s scan returns the slot *pointer*; `sub_00036330` (downstream of the gate) dereferences the slot's first dword as a real device-object pointer (`dword ptr [ebp]`, `[ebp+0xa3]`, `[ebp+0xd4]`, `[ebp+0xcc]`, `[ebp+0xa]`, `[ebp+0xb]` ...). A bare `1` there would make it dereference address `0x1` and crash the first time input is checked -- so the slot's content has to be a real device object, not a sentinel.
- **Fix actually written:** `sub_000EB270` (0xEB270-0xEB29A, the per-frame gate scan called from `sub_00086D30`, 42 bytes) is now hand-written in `recomp_manual.c`. On its first call, if `xbox_InputIsConnected(0)` is true, it hand-calls the real, already-correctly-lifted `sub_000EB5A0(0)` (the genuine per-port insertion handler the notification pump would have called, which builds the device object via `sub_00154249`/`sub_001547DA`) via `RECOMP_ABI_CALL`, simulating the cdecl call a real connect notification would make. Then it falls through to the original scan logic, unchanged. If no controller is connected, or the handler's own device-context lookup fails, the scan finds nothing -- identical to today, never a new crash either way. Reuses the already-verified poll/vibration ABI work (18.15) rather than fabricating a device struct.
- **Result: verified safe, not verified end-to-end.** Rebuilt via the full pipeline; lift confirms `sub_000EB270` is now hand-served ("Excluding 9 functions" vs. 8). Three runs (90 s, 30 s with a diagnostic print added, 90 s repeat): all clean exits at the normal watchdog end, same 63-file-load content ceiling as 18.14/18.15, no crash. The diagnostic (`[XPP] sub_000EB270 primed: xbox_InputIsConnected(0)=%d`) confirms the override runs every frame as intended, but prints `=0` every time: **this test machine has no physical XInput-compatible controller attached**, so `xbox_InputIsConnected(0)` (backed by one `XInputGetState` poll in `xbox_InputInit()` at boot) correctly reports not-connected and the gate correctly stays shut. Poll/vibration still did not fire and nothing changed on screen this pass -- but that's the test environment, not a defect: the fix is reachable and safe, just unable to prove it arms a slot without a controller plugged in when the process starts.
- **Next step:** plug in a real controller before a run. Expect `xbox_InputIsConnected(0)=1` followed by `[XPP] sub_000EB5A0(0) returned, slot[0]=0x...`. If `slot[0]` is nonzero, the 18.15 poll/vibration `trace_hit` lines should start firing every frame (that ABI was already verified correct once reached). If `slot[0]` comes back 0, `sub_00154249`'s own lookup (`sub_001547DA` against the 3-entry host-controller-context table at `0x153404`-`0x15340C`) failed -- i.e. driver-init never registered a host-controller context either, a separate and likely deeper gap.
- **Files changed:** `twoc/src/recomp_manual.c` (`sub_000EB270` hand-written: gate scan + one-shot primed insertion trigger + diagnostic trace). No `xboxrecomp/` changes.

### 18.17 Step 15: real controller connected -- confirms detection works, but the arming chain fails one level deeper than 18.16 could reach.

Report: `step15_xpp_real_controller_report.md` in the session scratch folder. Test-and-trace only, no code changes.

- **A physical Xbox One controller (wireless dongle, Windows-recognized) was connected before this run**, to test 18.16 for real. 90 s run, clean exit at 96 s (code 3), 17 framebuffer BMPs dumped.
- **`xbox_InputIsConnected(0)` now reads `1`** (`[XPP] sub_000EB270 primed: xbox_InputIsConnected(0)=1`) -- confirms the host-detection layer (`xbox_InputInit()`'s one `XInputGetState` poll at boot) is correct; the `=0` in 18.16 really was the test machine having no controller, not a bug.
- **`sub_000EB5A0(0)` still returns `slot[0]=0x00000000`.** The hand-called real insertion handler ran (as designed) but did not arm the port -- so **zero** poll/vibration hits again, and **no change on screen or in behavior**: same 63 file loads, same last file `logos2.cut`, framebuffer content changes only in the same idle particle/cubemap-load way every prior run without input already showed (checked by MD5 across dumps). The game was not "just waiting on a controller" -- the chain fails a step earlier than a live controller can fix on its own.
- **Traced the failure one level deeper.** `sub_000EB5A0` -> `sub_00154249` -> `sub_001547DA`, which scans a **3-entry host-controller-context table at `0x153404`/`0x153408`/`0x15340C`** (the original Xbox has multiple physical OHCI controllers backing its 4 ports) for one matching the port's key. A full-binary xref check (same method as 18.15's `0x1534AC`/`0x153430` check) found **no code anywhere writes to any of those three addresses** -- the only references are the loop-start immediates in the three functions that read the table (`sub_00153708`, `sub_001547A8`, `sub_001547DA`). This table is the "register this OHCI host controller" step, almost certainly populated by the separate OHCI host-controller driver via a bus/device-relations callback during real hardware enumeration -- the same "toolkit's OHCI model doesn't walk the controller far enough" gap already flagged in 14.4/15.1, now confirmed to block controller arming at *two* levels (this registration step, and the root-hub-status interrupt from 18.16), not just the one 18.16 could route around.
- **Read on this:** 18.16's fix is confirmed correct and reachable as far as it goes; the remaining blocker is real OHCI host-controller emulation (populating this 3-entry table with something `sub_001547DA` will match), which is materially bigger than a hand-called insertion trigger -- not something to route around with another one-line override. This is where XPP input work should pick up next, or be set aside in favor of the NV2A vertex-program gap (next-step 3) if OHCI emulation isn't worth the investment right now.
- **Files changed:** none.

### 18.18 Step 16: scoped a fabricated host-controller-context object (18.17's option 2) -- turns out not to be a data fix at all.

Report: `step16_xpp_fabricated_context_scoping.md` in the session scratch folder. Pure trace/scoping, no implementation, no re-lift, no rebuild, no run (per instructions).

- **Traced `sub_001547DA`'s match and everything read after a match.** The match key for the gamepad-insertion path is the literal `0x1534AC` (hardcoded in `sub_000EB5A0`, port-independent) against the entry's `+4` dword -- easy to satisfy. But a match falls straight into `sub_00154249` -> `sub_001554A1`, which reads far more off the matched pointer before it's done: `+0x1` (byte, a nonzero retry counter, decremented every call), `+0x8` (pointer to a `{count; ...; dataptr}` sub-object -- `count=0` makes its copy a safe no-op, the one genuinely cheap field), `+0x10` (pointer to a second "descriptor" sub-object with its own required fields `+0x0/+0x4/+0x5/+0xC`, one of which (`+0x0`) is itself passed as `this` into a third, untraced function `sub_00155C28`), plus whatever an early call to a fourth, untraced function `sub_00154B69(context, real_per_port_config)` requires. `context+4` (the field the now-bypassed original `sub_001542AB` cared about) turned out not to matter to this path any more, and pipe->[0xa3] (the back-link to context) isn't independently re-checked by `sub_00036330` either -- two fields that looked necessary at first glance turned out not to be, which is exactly the kind of thing this scoping pass was for.
- **Even a perfect fabrication likely still fails, but safely (no hang).** `sub_001554A1` builds a real two-phase USB SETUP-packet transfer and blocks on `xbox_KeWaitForSingleObject` against a hand-built on-stack `KEVENT` -- checked the host implementation (`kernel_sync.c`): it casts the guest pointer straight to a Win32 `HANDLE` and calls `WaitForSingleObjectEx`. A guest stack address is not a valid handle in this process, so the wait should fail fast (`WAIT_FAILED`/invalid handle) rather than hang -- good for risk, bad for the fix: the real transfer can never complete, so `sub_001554A1` takes its own failure branch regardless of how well the data is faked, unless `sub_001554A1`/`sub_00154249` is *also* overridden to skip the transfer and build the pipe object by hand -- at which point this is a functional rewrite, not a data table.
- **Other consumers of the same table, found via a full-binary xref check (one caller of `sub_001547DA`, confirmed no others):** a fourth, previously-uncatalogued reader, `sub_001547A3` (called by `sub_00154F76`/`sub_001553E1`, neither traced), matches table entries on a *different* field -- the entry's own `+0x0` byte, not `+0x4`. A fabricated entry aimed only at the insertion path's `+0x4` key could still be picked up by this unrelated path if `+0x0` happens to collide with whatever key it uses -- an open, unquantified risk.
- **Size/risk: not small.** Contrasted against every fix so far (fence mirror, memmove, `GetExitCodeThread`, DSOUND guard, poll/vibration, 18.16's gate -- each either a narrow local change or a call into already-correct lifted code), this one requires fabricating 2-3 structs, tracing two more functions (`sub_00154B69`, `sub_00155C28`) whose requirements are still unknown, and hand-writing a replacement for roughly the first (construction) half of a 564-byte function while discarding its second (real-transfer) half. Realistically closer in size/category to option 1 (real OHCI emulation, already scoped out in 18.16) than to any prior `twoc/`-only fix -- "fabricate a struct" does not describe what this actually is once traced to the bottom.
- **Can it stay in `twoc/`?** Mechanically yes (same `recomp_manual.c`/`main.c` override mechanism as every fix so far, no `xboxrecomp/` changes required) -- but that answers *where*, not *how big*.
- **Recommendation:** treat this as the same category of decision as 18.16 already raised -- invest in either real OHCI emulation (benefits every USB path) or a hand-written pipe-object builder for this one call path (a narrower but still nontrivial version of "option 2"), or set XPP input aside in favor of the NV2A vertex-program gap (next-step 3) for now.
- **Files changed:** none.

### 18.19 Step 17: scoped the NV2A vertex-program gap -- two renderers already exist in this toolkit, and the better one is unused.

Report: `step17_nv2a_vertex_program_scoping.md` in the session scratch folder. Pure trace/scoping, no implementation, no re-lift, no rebuild, no run.

- **Two independent rendering backends exist.** `xbox_nv2a`'s `nv2a_pgraph_d3d11.c` (651 lines, what `RECOMP_PB_EXEC=1` currently drives, per 18.2's pivot away from 15.1's original plan) intercepts raw NV2A push-buffer commands. `xbox_d3d8` (12,384 lines, including a **complete, already-wired-up 1,490-line NV2A vertex-shader-microcode-to-HLSL translator**, `d3d8_vsh.c`) only runs if the game's D3D8 API calls are intercepted at the API boundary -- 15.1's original, not-yet-taken plan.
- **The in-use renderer is a hard-coded 2D-only path, not a general 3D pipeline.** Confirmed by reading it: it only processes `NV097_INLINE_ARRAY` at a hard-coded 5-dword (X,Y,U,V,Color) layout, `submit_draw()` unconditionally treats every vertex as pre-transformed screen-space (`D3DFVF_XYZRHW`), `NV097_SET_TRANSFORM_PROGRAM`/`_CONSTANT` uploads are explicitly in a "safe to ignore" bucket (never stored, never executed), and **vertex-array-format/offset methods (0x1680-0x1780, how a game describes vertex-buffer-backed 3D geometry) are in that same ignored bucket too** -- a second, independently-discovered gap beyond just vertex programs. Its texture-binding logic is hard-coded per asset name for Burnout 3's menus (`"B3Logo"`, `menu_pb_road_rage.h`, etc.) -- this file was purpose-built as a 2D menu renderer for a different title; Crash's splash/logo screens render only because those happen to also be pre-transformed 2D draws, the one case it supports. This lines up exactly with 18.14's 91%-off-surface finding: real 3D vertices reinterpreted as already-in-screen-space pixel coordinates land far outside 0-640/0-480.
- **The unused renderer's vertex translator is real, not a stub.** Traced `dev_CreateVertexShader` (`d3d8_device.c:1287`): it counts real NV2A microcode instructions and calls `d3d8_vsh_create_shader()`, a full interpreter/compiler for the actual MAC/ILU dual-issue instruction set (documented in `d3d8_vsh.h`'s architecture comment), validated by 1,074 lines of smoke tests (`tests/d3d8_smoke`). It's simply never invoked because Crash's game code writes raw push-buffer commands directly rather than going through the D3D8 API surface.
- **Option A (extend `nv2a_pgraph_d3d11.c` with vertex-program execution): not small, and not `twoc/`-only.** Would mean building, inside the executor, substantially the same things `d3d8_vsh.c` already does elsewhere (capture microcode/constants currently thrown away, write a second independent microcode-to-HLSL translator or invasively refactor a 12k-line library to share one), *plus* adding vertex-buffer/array-format draw support from scratch (the newly-found second gap), *plus* generalizing the hard-coded FVF and texture-binding assumptions. All of this is new `xboxrecomp/` code, not a `twoc/`-only fix like every prior step.
- **Option B (replace the ~91 D3D entry points, 15.1's original plan): re-verified independently, and turns out cheaper than 15.1 feared.** A fresh full-binary xref scan (not just quoting 14.4) found **99 distinct D3D-section addresses called from outside it**, 246 call sites, heavily skewed (top four: 34/24/12/11 sites) -- consistent with 14.4's "91 from 123 game functions." Sampled the top entry (`sub_00103AD0`, 34 sites): a COM-style `AddRef`/`Release` pattern, matching `xbox_d3d8`'s existing object model exactly, no surprises. Crucially, **this is not the harder case `d3d8ltcg-device-context.md` warns about** -- that doc's invasive xemu-snapshot approach is only needed when a game's D3D8 library is LTCG-inlined into one ~80KB function; Crash's D3D library is a normal, cleanly-bounded, separately-sectioned 286-function library, so the ordinary per-function bridge in `d3d-translation.md` applies directly. Bridging these ~91-99 entries to `xbox_d3d8` gets real vertex-program execution, real WVP transforms, and real vertex-buffer draws **for free**, with no new toolkit code obviously required -- the entire vertex-program gap is solved as a side effect of finishing 15.1's original plan. The real costs are (1) identifying ~91-99 unnamed entries against a well-documented public API (faster per-entry than XPP's 10, but ~9-10x as many), (2) writing that many individual bridges (mechanically the same pattern as every fix so far, just at much greater count), and (3) doing the swap incrementally (highest-traffic entries first, dump the framebuffer after each batch) since a full pipeline swap risks going from "some pixels render" to "none" if a bridge is wrong -- unlike Option A, which is purely additive to a path that already produces some output.
- **Read on this -- the inverse of 18.18's conclusion.** In 18.18, the cheap-looking option (fabricate a struct) turned out to be a hidden rewrite, and the faithful-looking option (real OHCI emulation) was the only real path. Here, the option that looks like reopening an already-settled decision (Option B) is the one that reuses the most mature, already-tested code and stays `twoc/`-only, while the option that looks like a local, additive improvement (Option A) requires rebuilding infrastructure that already exists elsewhere in the same toolkit.
- **Recommendation:** Option B, if the vertex-program gap is worth pursuing now -- it reuses `xbox_d3d8` and `d3d8_vsh.c` as-is, matches every prior fix's `twoc/`-only shape, and the cost is bridging labor and incremental verification, not missing capability. Start with the four highest-traffic entries (`0x103AD0`, `0x100DD0`, `0x102440`, `0x103A90`, covering the bulk of call volume) and verify no regression via framebuffer dumps before bridging the long tail.
- **Files changed:** none.

### 18.20 Step 18: tried Option B's first bridge -- CreateDevice succeeds, but confirmed it cannot coexist with the rest of the game staying on the old push-buffer path.

Report: `step18_d3d_createdevice_bridge_report.md` in the session scratch folder. **Code left uncommitted** -- the bridge makes the game crash within the first couple of seconds every run; see "Result" below.

- **Correction to 18.19's picked targets, found before writing anything (per the task's own step 1).** Identified all 4 call-frequency-ranked addresses: `0x103AD0`/`0x103A90` (34/11 sites) turned out to be the game's own `AddRef`/`Release`-equivalents for its D3D resource-wrapper objects (refcount field + `MmFreeContiguousMemory` on release), `0x100DD0` (24 sites) a shared "write one push-buffer method + advance the cursor" primitive reading/writing `[0x10EBF0]` (the same device pointer 18.1's fence mirror tracks), and `0x102440` (12 sites) a real but narrow resource-creation function called from only 2 sites deep in unrelated game code, not the wrapper cluster. None of these are isolated, bridgeable API-boundary calls -- the first three are shared internal plumbing used by dozens of *other* still-unbridged D3D functions; bridging any of them alone would mean some code paths treat the shared device object as `xbox_d3d8`-shaped while the rest of the (unbridged) library keeps reading/writing it as the native push-buffer-shaped struct at the same address. Flagged this mid-task; the user's direction was to bridge `CreateDevice` itself instead, since it's the one thing everything else is downstream of, and report plainly if it doesn't coexist.
- **Identified and bridged the real CreateDevice pair.** `sub_00039A80` (14.9's presentation-parameters builder) calls `sub_000FD6C0` (stores two persistent D3D-library config flags, unrelated to device shape, left untouched) then `sub_000FD6E0`, the real `IDirect3D8::CreateDevice(Adapter=0, DeviceType=1, hFocusWindow=0, BehaviorFlags=0x40, pPP, ppDevice=0x1E3C90)`. Verified the guest `D3DPRESENT_PARAMETERS` the call site builds matches `xbox_d3d8`'s own struct (`d3d8_xbox.h`) **field for field, byte for byte** (640x480, `D3DFMT_A8R8G8B8` back buffer, `BackBufferCount=2`, `MultiSampleType=NONE`, `SwapEffect=DISCARD`, `Windowed=FALSE`, `EnableAutoDepthStencil=TRUE`, `D3DFMT_D24S8` depth, `PresentationInterval=1`) -- confirmed the guest struct could be cast and used directly with no re-marshaling needed.
- **The bridge** (`sub_000FD6E0` in `recomp_manual.c`): creates a small hidden window (D3D11 needs a real `HWND`; the game's own `hFocusWindow`/`hDeviceWindow` are both 0 and `RECOMP_FB_WINDOW` is off by default), calls `xbox_Direct3DCreate8(0)` + real `CreateDevice()`, writes the resulting **host** `IDirect3DDevice8*` into the same guest slots the original wrote a native pointer into (`[0x1E3C90]`, `[0x10EBF0]`/`[0x10EBF4]`). Documented the expected failure mode in the comment before running it: a host C pointer is not a valid guest VA under `XBOX_PTR`'s translation, so unbridged code dereferencing it computes an unrelated address.
- **Result: exactly the predicted failure, observed directly.** `xbox_d3d8` genuinely created a real device -- log shows fixed-function shaders compiled, NV2A combiners initialized, **`D3D8 VSH: Vertex shader translator initialized`**, "Device created (640x480)" -- confirming 18.19's identified vertex-program pipeline really does stand up correctly on its own terms. But the *very next* unbridged call the game makes (`sub_00100EA0`, called immediately after `CreateDevice` returns success, three times back to back for what looks like transform/viewport setup) reads the stored device pointer as the native push-buffer-shaped object, computes an offset into it, and dereferences the result: **clean read access violation** (`[CRASH] Access violation ... fault addr=0xBFD81704`, Xbox VA `0xBFD71704`, close to but not identical to the stored token `0xBFD6F690` -- consistent with a native struct-offset read off the mismatched object). Only 10 `[FILE]` loads happened before the crash (a normal run reaches 63); 0 framebuffer dumps -- the crash preempts everything the old path used to render, so there's no "still renders the same splash" to compare against, just "now crashes almost immediately where it used to run clean for 240 s+."
- **Conclusion: confirmed by direct observation, not just architectural reasoning -- bridging `CreateDevice` alone does not coexist with the rest of the game staying on the old push-buffer path.** Real progress on Option B needs either (1) bridging every function between `CreateDevice` and the first draw together (the full, non-incremental pipeline swap 18.19 already flagged as higher-risk, now confirmed necessary rather than optional), since unbridged code dereferences raw offsets directly rather than through an accessor a handle/token layer could intercept, or (2) treating this as a much bigger, batched effort from the start. **The narrow "bridge 4 small functions, verify incrementally" strategy from 18.19 does not hold up in practice** -- there is no safe, additive subset of D3D entry points once `CreateDevice` is the one thing everything else depends on.
- **Code state:** the crashing bridge was never committed and has since been **reverted** from the working tree (`twoc/src/recomp_manual.c`'s `sub_000FD6E0` override, plus the `xbox_bridge_get_hidden_window` helper, removed; re-lifted and rebuilt clean). The repo is back to the known-good, 240 s-clean state -- this section exists purely as a record of what was tried and why it doesn't work, not a pending cleanup task.
- **Files changed (uncommitted):** `twoc/src/recomp_manual.c` (`sub_000FD6E0` hand-written: CreateDevice bridge + a hidden-window helper). No `xboxrecomp/` changes.

### 18.21 Step 19: scoped Option B's full wrapper cluster -- the 99 entries split into 4 cost tiers, dominated by 10 large functions and one object-model decision.

Report: `step19_d3d_wrapper_cluster_scoping.md` in the session scratch folder. Pure scoping, no implementation, no re-lift, no rebuild, no run. Confirmed repo state clean and matching 18.20's revert before starting.

- **Used the disassembler's own full xref database instead of re-deriving one by hand.** `xboxrecomp/tools/disasm/output/xrefs.json` (91,429 edges: calls/jumps/cond_jumps/data refs/kernel calls) and `functions.json` (per-function size/instruction-count/calls_to/called_by) already exist on disk, built by `xboxrecomp/tools/disasm/xrefs.py`. Filtering for `call` edges landing in the D3D section (`0x000FD680-0x0010F584`, confirmed from `D3D.asm`'s header) from outside it reproduced **99 distinct entries, 246 call sites, top four 34/24/12/11** -- byte-for-byte the same four addresses 18.20 found by hand (`0x103AD0`/`0x100DD0`/`0x102440`/`0x103A90`), cross-confirming the xref database against 18.19/18.20's manual tracing. 256 of the D3D section's 287 functions (89%, 51KB of 59KB) are transitively reachable from those 99 entries -- essentially the whole section is live.
- **The 99 split cleanly into four cost tiers, not one uniform bucket:** **Tier A** -- CreateDevice (`0x000FD6E0`) itself, already scoped and written once in 18.20 (its own closure reaches 111 of the 256 live functions, ~26KB, the single largest dependency in the cluster). **Tier B** -- 4 shared-plumbing entries (the same four top-traffic addresses 18.20 already flagged as not real 1:1 API calls: two AddRef/Release-equivalents, a push-buffer-cursor-write primitive, a narrow resource-creator) covering **81 of 246 call sites (33%)** despite being 4 of 99 entries -- these gate a design decision (how a wrapped D3D8 object is represented in guest memory) that a large fraction of the remaining 95 entries likely depends on. **Tier C** -- 36 entries confirmed, by direct disassembly of 4 samples plus a script isolating every D3D-section function whose only internal call is the shared flush primitive `sub_00103740`, to be identical in shape: check push-buffer space, write one fixed NV2A push-buffer method header (extracted literal constants: `0x403a4`, `0x400a60`, `0x40308`, `0x41d84` shared by 4 different wrappers, etc.) plus the caller's one DWORD argument, advance the cursor, cache the value. The header format and numeric range match the 21 `NV097_SET_*` constants `xbox_nv2a/nv2a_pgraph_d3d11.c` already defines and partially decodes -- none of the extracted values matched one of those 21 exactly, but they're the same address space, so this is "extend an existing table," not "invent one." **Tier D-small** -- 48 entries with small closures (<=20 internal functions, mostly <2KB, several under 30 bytes) that look like the same family through a different code path, not individually confirmed this pass. **Tier D-big** -- 10 entries with 30-85-function closures (6-20.5KB each), comparable in shape to what CreateDevice itself required -- the real cost driver, at only 17 of 246 call sites (7%) combined. Table with kernel-call evidence and unconfirmed identity guesses (Reset, two Draw calls, shader-related, a resource-creator, a teardown path, a paired Create/Lock, a shared resource-release helper) is in the scratch report; none of these 10 were traced call-by-call the way CreateDevice was in 18.20, so the guesses are flagged speculative, not established.
- **Real size estimate, anchored to actual prior fix sizes** (DSOUND guard: 1 function/9 bytes; XPP poll+vibration: 2 functions; XPP arming gate: 1 function/42 bytes; CreateDevice: 1 function/180 bytes, already the largest hand-written fix in the project): Option B is at minimum 1 CreateDevice-class function (already done once), 1 object-model design decision gating 33% of call sites, one bounded NV097 lookup table that then makes ~84 of 99 entries (60% of call sites) close to 1 line of C each once built, plus **10 more CreateDevice-class tracing efforts** for Tier D-big -- the dominant real cost and highest risk, since 18.20 showed this exact class of function is where an incomplete trace produces an immediate crash rather than a silent gap. Rough order of magnitude: **10-15x the effort CreateDevice alone took in 18.20**, concentrated in Tier D-big and the Tier B decision rather than spread evenly, and (per 18.20, not re-litigated) all of it has to land together before any of it is testable.
- **Recommended order if this is greenlit:** (1) re-land Tier A as part of the batch, not tested alone; (2) resolve Tier B's object/refcount model first, since it gates the largest single block of call sites and probably several Tier D-big entries too; (3) build the NV097 method table and batch-translate Tier C + Tier D-small together (verify D-small's shape first); (4) trace and bridge the 10 Tier D-big entries individually, largest/most-central closure first -- where a wrong guess is most likely to reproduce 18.20's immediate-crash failure mode; (5) only then full rebuild/re-lift/first end-to-end run.
- **Explicitly not done this pass (scoping only, per instruction):** no identification of which specific `xbox_d3d8` function each of the 99 should call; no decoding of NV097 method constants against Xbox's public method table (flagged as the concrete next lookup task); no call-by-call tracing of the 10 Tier D-big entries; no code, re-lift, rebuild, or run.
- **Files changed:** none.

### 18.22 Step 20: traced 18.18's two unexamined functions for the hand-written pipe-object route -- both are deeper than 18.18 thought, but the game never needs anything they produce.

Decision going in: take 18.18's option 2 (narrow hand-written builder for the one gamepad path), not real OHCI emulation. Pure trace/plan, no code, no re-lift, no rebuild, no run.

- **Correction to 18.18's framing, from `sub_00154249`'s own body:** it is `XInputOpen(type, portKey, slotFlag, cfg)` (stdcall, `ret 0x10`). The table entry `sub_001547DA` returns (18.18's "context", `*D`) is the static **class-driver descriptor** for the device type (`D+4` == the type key `0x1534AC`, `D+1` = remaining open count, `D+8` = `{len; ptr}` default-state template, `D+0x10` = default cfg, `D+0x14` = report-parse callback). `sub_001554A1` is the open itself: `ecx=D`, `edx=portKey`, out-ptr, cfg.
- **`sub_00154B69` (106 bytes) -- a lookup into a second, independent table: the enumerated-USB-device-node table.** It scans `count=[0x435098]` (set to 4 by init) entries of `0x16` bytes at `[0x43509c]` for one with flags `node+4` present (bit 0) + descriptor-read (bit 3) + not-being-removed (bit 1 clear), `node+0xE == D`, and `sub_001559F6(node+0) == portKey` (`sub_001559F6` is a 1-line getter: USB-device-object `+0x14`, the port). Returns the node or 0 (-> `0x48F`, `ERROR_DEVICE_NOT_CONNECTED`). **It needs nothing from `*D` beyond D's identity.** The only writer of these nodes is the unlabeled routine at `0x1556D5` (XID "add device"): called with a real USB device object from the hub/enumeration layer, it fills a node, then submits a real XID-descriptor control transfer (`bmRequestType 0xC1, bRequest 6, wValue 0x4200`) whose completion `0x1553E1` sets bit 3 and `+0xE`. So satisfying this check means fabricating a USB device object *and* a completed enumeration -- a third fabricated layer 18.18 didn't know about.
- **`sub_00155C28` (237 bytes, 33 callers) -- the USB core's URB-submit entry, i.e. the gateway into the OHCI host-controller driver.** It isn't "a function hanging off the descriptor's `+0x0`": `ecx` = USB device object (`node+0`), arg = URB. It switches on the URB function byte (`+1`, low 6 bits; `0x40` flag = "synchronous, no completion given") to fill device-specific fields (device address `dev+5`, default pipe `dev+8`, endpoint pointers `dev+0x18`), then calls `sub_001583AE(([dev+0xC])+0x18, urb)` -- the HCD's queue-transfer routine -- and, for sync URBs with a pending status (`0x40000000`), blocks on its own stack `KEVENT`. Everything beneath it is OHCI. `sub_001554A1` calls it three ways: via `sub_00154BD3` (open endpoints), an initial HID `GET_REPORT` (`0xA1/1/0x0100`) that `sub_001554A1` then waits on with `KeWaitForSingleObject`, and an optional interrupt-IN autopoll URB (function `0x41`, completion `0x154D2F`). **Faking it faithfully = HCD emulation.** Plainly: both functions pull in more scope than expected, not less.
- **But the game never consumes any of it.** Full-binary check of every consumer of the value `XInputOpen` returns (stored at controller slot `+0`, slots at `0x427598`, stride `0xF0`):
  - All ~25 readers of the current-slot pointer `[0x23c2d8]` in `text.asm` touch only game-owned slot fields (`+0xC8`, `+0xD0`, `+0xD4`, `+0x78`, `+0xA..0x1A` state copy), never slot `+0`'s pointee. 18.16's reading that `sub_00036330` dereferences the handle was off by one level: its `ebp` is the slot, `[ebp]` is the handle, and the handle is only null-tested and passed to XPP.
  - The handle reaches exactly three XPP entries: `sub_001542AB` (poll, 2 call sites) and `sub_0015431A` (vibration) -- **both already hand-written and ignoring it** (18.15) -- and `sub_0015429F` (`XInputClose` -> `sub_00155158`, 2 call sites: `sub_000EB5A0` re-insert and the `sub_000EB6C0` pump's removal branch). The capability entries `0x154805`/`0x154937` are only reached from the memory-unit handlers `sub_000EB2A0`/`sub_000EB340` (separate `0x4272D0` table), never with a gamepad handle.
  - So the handle is an opaque token to game code. `sub_001554A1`, `sub_00154B69`, `sub_00155C28`, the node table, the class descriptor table and the completion wait can all be bypassed together by replacing the open/close pair, not rebuilt.
- **Plan (not implemented):** two new hand-written functions in `recomp_manual.c`, same `--exclude-manual` mechanism, `twoc/` only:
  1. `sub_00154249` (`XInputOpen`, stdcall, 4 args, `esp += 20`): if `type == 0x1534AC` and `xbox_InputIsConnected(port)` (port derived from `portKey` by inverting the game's `[0x1B0AC8 + 4*i]` table, with a log line to confirm), return a nonzero per-port token; otherwise return 0, the same value the real failure path returns (last-error write skipped; its one caller discards `GetLastError`). Token = a fixed sentinel `0xC0DE0000 | port` in unmapped guest space, deliberately: any unexpected dereference faults loudly and names itself, instead of silently reading fabricated data.
  2. `sub_0015429F` (`XInputClose`, stdcall, 1 arg, `esp += 8`): no-op for our tokens (the real body would dereference `+0`/`+0xA3` and push the token onto the `[0x4350A0]` free list -- memory corruption).
  3. Optional, small: `sub_001542AB`/`sub_0015431A` decode the port from the token instead of hard-coding port 0.
  4. Wait-on-completion: not bypassed so much as never reached -- no URB is built, so no `KeWaitForSingleObject` runs.
  - Unchanged: 18.16's `sub_000EB270` gate (it now reaches a working open). Not fabricated: no descriptor, node, USB device object, pipe object or `KEVENT` fields.
  - Expected verification (controller plugged in before launch): `slot[0]=0xC0DE0000`, then 18.15's poll/vibration `trace_hit` lines firing every frame, then (the actual test) whether the hub/menu responds to input.
- **Known limits of the plan:** hot-plug still does nothing (the `sub_000EB6C0` pump's notification gates remain unserviced -- insertion is only primed once at startup, as in 18.16); the per-open `D+0x14` report callback and the `[0x1535A0]` hook for type `0x153500` are skipped -- the former only matters for data the hand-written poll already supplies, the latter isn't the gamepad type.
- **Files changed:** this notes entry only.

### 18.23 Step 21: hand-written XInputOpen/Close -- a controller slot is armed and real button presses reach the game every frame. The game does not visibly react yet.

Report: `crash claude md/reports/step21_xpp_open_close_report.md` (in the repo; earlier step reports lived in a session scratch folder that no longer exists). Changes in `twoc/` only.

- **Port table confirmed first:** `[0x1B0AC8]` in the XBE's `.data` is `{0,1,2,3}`, read only at `0xEB2E0`/`0xEB5C0`, never written, so `portKey == port`.
- **Implemented 18.22's plan:** `sub_00154249` (open -> token `0xC0DE0000|port` when the type is the gamepad and the host reports that port connected, else 0) and `sub_0015429F` (close -> no-op). Poll/vibration now take the port from the token instead of hard-coding 0, and poll logs every button change (thumbsticks excluded). Re-lift excluded 11 functions (9+2), 0 failed. Clean build.
- **120 s run, controller on port 0, dumps in `dumps\step21\`:** clean watchdog exit (code 3).
  - **Stage 1 pass:** `open ... -> handle=0xC0DE0000`, `slot[0]=0xC0DE0000`. First armed controller slot in the project.
  - **Stage 2 pass:** poll hit #1 right after `logos2.cut`, then #1000 by ~t=75 s (~17-18/s, one per main-loop iteration per 18.15; loop rate not independently measured). Vibration: 0 hits, expected (nothing requested rumble).
  - **Stage 3, no visible response:** A and Start (twice) were pressed ~t=30-40 s and all were captured into the game's own state buffer (slot `+4`). But there were no new file loads (still 65, last `logos2.cut`), and no dumped frame that 18.17's no-input dumps didn't already contain (the only new image is a dark-grey frame ~10 s *before* the first press). GPU rates unchanged.
- **Read on this:** input delivery is done and proven. 18.14's inference that the game was "idling at a menu waiting on input" is now weaker, because correct A/Start arrived and changed nothing. Untested candidates: (1) still in the logo/attract/load sequence, not at an input screen; (2) an in-place menu change hidden by the NV2A 3D gap (file loads rule out level/menu *transitions*); (3) the game reacts to edge flags in other slot fields (`+0xC8/+0xD0/+0xD4`) this path doesn't set.
- **Files changed:** `twoc/src/recomp_manual.c` (two new hand-written functions, port-aware poll/vibration, button-change log, `sub_000EB270` comment), this notes entry, the report.

### 18.24 Step 22: traced where input goes -- the title screen waits forever on a DSOUND stream that doesn't exist. Forcing it past that, the game's menus respond to input and it starts loading the next level.

Report: `crash claude md/reports/step22_input_trace_report.md`. Diagnostic only; all temporary code reverted (source = `b9323b8`, re-lifted, clean rebuild).

- **Slot fields update on a press** (per-frame diff inside the hand-written poll; `RECOMP_PEEK` alone is too coarse at ~10 s): `+0xC8` = buttons held (A `0x40`, Start `0x800`), `+0xCC` = previous frame, `+0xD0` = just pressed. `[0x23c2d8]`/`[0x23b750]` constant. Presses were **synthetic** (injected into the poll on a timer). There were no human presses this step, and the wireless controller later went offline. Main loop measured at ~17.6 frames/s.
- **Where the game is:** main function `sub_00087BF0`, level `[0x19c068]=0x25`, level loop `sub_0002D950(0)` with `[0x17be64]=0` (title run). Its A|Start skip (`0x2DB67`) and the Start->pause path in `sub_00086D30` are both gated off in this mode. The menu handler `sub_0006E130(0x5607C0, slot)` reads the fields but returns at once when no menu is open (`+0x6E == 0xFF`).
- **Menu timeline:** title setup opens menu 2 (`0x2C55B`), which closes *itself* at frame 68, just after the fade-in, with no input (`0x6F366`), then starts sound `0xAC`. No menu after that.
- **Why no main menu:** `sub_0002C020` registers `sub_0002BCC0` (= `set_menu(0)`) as the title animation's end callback (`obj+0x90`, fired at `0x7AA35` when time `+0x70` >= end `[[obj+0x58]+8]` = 961). But `sub_0002C330` sets the animation speed `+0x74` to 0 every frame until sound channel 4 reports playing (`sub_000EC770` -> `sub_000B9370` -> `sub_0005D4B0`, stream voice 1 of the sound manager at `0x427A58`, i.e. `0x431F1C`). **Every stream voice's DSOUND interface (`+4`) is null**, so it never reports playing. Time sits at 300.0.
- **Proven by temporary patch** (`sub_0005D4B0` returns "playing" only for the title query, return `0xB939B`, while `[0x1baa2c]==1`; the broad version hung audio init's wait-for-stop polls). Controller faked as connected, presses injected, 480 s run:
  - animation runs (~2.2 units/s, ~300 s to finish here vs an estimated ~22 s on hardware);
  - **`set_menu(0)` from the real callback**;
  - injected Start -> `set_menu(4)` (accept path `0x6F10F`) on the next log line;
  - injected A -> "start game" `set_menu(-1)` (`0x6F423`);
  - the level loop exits and the next level loads, including **`levels\b\intro\introchars.dat`, never loaded before** (77 loads vs the 65 ceiling);
  - then an **access violation** reading guest `0xFFFFDFC8` under `sub_0002BD80` <- `sub_0002C020` <- level load (`0x2DA42`).
  - Nothing new visible on screen beyond the moving title animation (NV2A gap).
  - **First time the game's state machine has responded to input** -- with synthetic input and a temporary audio workaround, not yet with a human and the real code.
- **Dead end:** the input record/replay buffer (`[0x1baba8]`) is idle, mode 0.
- **Read on this:** the blocker past `logos2.cut` is **audio (DSOUND streams never created)**, not input. It's in the same library as 18.10-18.13's 3D-listener gap (whose null pointer is the first dword of this same sound manager); the link isn't proven.
- **Files changed:** none in code (all reverted). This notes entry and the report.

### 18.25 Step 23: the stream blocker was DirectSoundCreate failing (no AC'97 codec) -- fixed for real. The title opens the main menu itself, a real controller drives it, and the next level loads until a new DSOUND stream-stop hang.

Report: `crash claude md/reports/step23_dsound_streams_report.md`. Code committed as `dc99be3` (`twoc/` only); all diagnostics reverted, re-lifted (4650 translated, 0 failed, 12 exclusions), clean build.

- **Root cause (one for both audio gaps):** `DirectSoundCreate` (`sub_00137A06` -> `sub_00137999`, first call in the manager init `sub_000B99D0`) returned `0x88780078` DSERR_NODRIVER: `sub_0013F5F8` polls the AC'97 codec-ready bit `0xFEC00130` bit 8, and nothing set it. The init bails, so there are no voices and no stream thread (`sub_0005DA30`, created at `0xB9AE9`), and `*(0x427A58)` stays null. **`*(0x427A58)` is the manager's `IDirectSound*`, written through `this` by DirectSoundCreate. It is the same null as 18.10-18.13, which is why no literal-address write existed.** The 18.13 guard is now inert (kept as a safety net).
- **Fix:** the runtime already had it behind `RECOMP_AC97_READY` (15.1's original DSOUND plan), never set by `run_twoc.ps1`. Setting it exposed two gaps, both fixed in `twoc/`:
  - the GP DSP command handshake. `sub_001391DE` spins until the DSP clears command 3 at `scratch+0x810` (`0x80BC8810`), and there's no DSP. It is now hand-written: the lifted body, with the spin replaced by a one-time clear. It is the only such wait in DSOUND (wrong, see 18.30: `sub_0013937A` waits on the same word). The effects image doesn't run, so no reverb. The runtime's `RECOMP_APU_DSP_ACK=<addr>` is the address-based alternative;
  - an AV on `rep movsd`/`memcpy` from APU GP memory (`0xFE830200`): the trap decoder can't step AVX. `main.c` now leaves APU `+0x30000..+0x80000` as plain RAM. `apu_core.c` ignores GP/EP anyway.
- **Result:** the stream thread starts; `LOGOS.wav` opens; `DirectSoundCreateStream` S_OK; the title query reports **PLAYING** from a real stream; the animation finishes; the real callback `sub_0002BCC0` opens **menu 0** (new banner `[GAME] MAIN MENU OPEN`). No stopgap needed (18.24's plan step 2 skipped).
- **Real wired controller, twice:** press -> `set_menu(4)` (`0x6F10F`), press -> "start game" `set_menu(-1)` (`0x6F423`). Run 2: the next level loads `particle.raw`, `intro.ptl`, `general.ptl`, `Droplet1.dds`, `WaterCubeMap.dds`. The menu takes ~12.5 min to appear (slow title, still open).
- **New blocker, before the 18.24 crash:** level setup `sub_0002C020` -> `sub_000EC870` = `sub_000B9180` (wait until all voices/streams stop). The stream thread, stopping stream 1 (`stop=01`), spins on `KeQuerySystemTime` (~400M calls) **while holding DSOUND's lock `0x152BB0`**. The main thread blocks on the lock. `sub_0013B682` (voice-off wait) was ruled out by a breaker that never fired; `sub_00139B74`/`sub_00139F89` are the other candidates. Probably DSOUND waiting on an APU voice-off/interrupt the emulated APU never produces (18.11's suspected interrupt gap), not proven. **Next:** dump the stream thread's guest stack (`0x01B4EF34..0x01B4EFF0`, the same every run) to name the spin.
- **Crash `0xFFFFDFC8`: not reached.** Static read: `sub_0002C020` (intro case) -> `sub_0002BD80(eax=0x17BBF8, 0x17BAA8)` -> `sub_000982C0("levels\intro\introchars.dat")` (return `0x2BDB0`, the innermost live frame; 18.24's `0x958E9` was likely stale). The `-0x2038` is a computed index (no literal displacement in `.text`); the null base is not yet identified. The VEH dump will name it once the hang is past.
- Also seen: the runtime watchdog printed nothing in 900-1500 s runs (hard kill ended them). Not investigated.
- **Files changed:** `twoc/run_twoc.ps1`, `twoc/src/main.c`, `twoc/src/recomp_manual.c`, this entry, the report.

### 18.26 Step 24: unattended fast test options (committed). The "stream-stop hang" is a host stderr deadlock: the game suspends its stream thread while that thread is inside the runtime's `fprintf`.

Report: `step24_fast_test_and_stream_stop_report.md` in the session scratch folder. The options are committed as `e16c13b` (`twoc/` only). All part-2 diagnostics were temporary and are reverted (source = `e16c13b`, rebuilt); the patch is in scratch.

- **Test options** (`run_twoc.ps1 -FastTitle -AutoStart`, off by default, the script clears the variables otherwise):
  - `RECOMP_DEV_FAST_TITLE` seeds the float `0x1BAA98` with 64. The title speed setter `sub_0002C2C0` uses it instead of 0.5 when it's nonzero. Nothing in the binary writes it (BSS; that setter is the only reader), so it's a leftover tuning knob. It's cleared when the menu callback fires.
  - `RECOMP_DEV_AUTOSTART` injects A on port 0 at +5 s and +20 s after the menu, and reports a neutral virtual pad on port 0 when none is connected (open/priming treat port 0 as connected).
  - **Unset: identical to the baseline** (same 22 file opens in the same order, same event lines, identical stdout).
  - **Both set, no pad: menu at 20.4-22.3 s** (was ~745 s), "start game" at +20 s (7 runs). The level-load hang follows about a minute after launch in all 6 runs without the experiment below.
  - A `sub_0002C330` wrapper wasn't usable: the lifter's `sub_X_gen` wrap mode renames the direct call sites too, so a wrapper only sees indirect calls.
- **Phase A, ~10 s, DSOUND's lock `0x152BB0` held (not a hang).** The spinning function is **`sub_0013B682`**, the voice-off wait: `while (voice+0x12 & 0x8000) sub_0013ADB8(voice)`. `sub_0013ADB8` reads `KeQuerySystemTime` (return `0x13ADD8`, esp `0x01B4EF34`) and forces the voice off (`sub_0013ACDD`) 5 s after the stop stamp at `voice+0x80`. Chain: `sub_0005DA30 -> sub_00136389 -> sub_0013D2DB -> sub_0013CF79 -> sub_0013B682`.
  - It waits for the hardware-active bit `0x8000` to clear. The APU voice-idle interrupt (DSOUND's `KeConnectInterrupt` at `0x13A198`/`0x13FDB1`, 18.11) is never raised here, so each voice ends at the 5 s timeout. Two voices, second stop stamp 5.06 s after the first.
  - Step 23's 6 s breaker was in the right loop: the loop just ends at 5 s.
- **Phase B, from ~64 s, forever: host deadlock.**
  - **Level setup `sub_0002C020` suspends the stream thread** (`0x2C032`, `sub_000EC880` = `NtSuspendThread([0x432070])`, the stream thread's handle at manager+`0xA618`), just after the sound-stop wait at `0x2C02D`. It would resume at `0x2C245`.
  - The suspension lands while the stream thread is in `xbox_RtlEnterCriticalSection`'s contention report (`[CS] ... waiting on guest lock`; the first 16 contentions are logged, with backtraces), inside `fprintf -> WriteFile`, holding the CRT stderr lock. Proof: its suspend count was already 1 when sampled and its CPU time was frozen; stderr is a disk file, not a pipe.
  - Then every logger blocks on that lock: the main thread in `xbox_translate_path`'s `[PATH]` log for the level's next file open, the NV2A ack thread, and the watchdog. The watchdog is why nothing printed in step 23's long runs.
  - The DPC thread loops in `sub_00106720` polling NV2A status. That's a consequence.
  - No kernel call happens after ~64 s. The "400M `KeQuerySystemTime` calls" were all Phase A.
- **Confirmed by a temporary experiment:** an `NtSuspendThread` thunk tap taking `_lock_file(stderr)` around the real suspend gets past it. The level loads `font.nux` and **`levels\b\intro\introchars.dat`**, then `xbox_HeapAlloc: out of memory (requested 4336960, used 49480464/50855936)`, null-pointer ICALL skips, and an AV at guest `0xFFFFFFFD` (innermost `0x1241B3`). That is very likely the 18.24 crash's null base: a failed allocation.
- **Proposal (not implemented):**
  1. **Cooperative `NtSuspendThread`/`NtResumeThread` in `twoc/`.** Wrap the thunk targets `[0x15AA7C]`/`[0x15AA80]` via `recomp_lookup_manual`. Suspend only marks the target (`xbox_bridge_resolve_handle` -> `GetThreadId`); the target parks at its next kernel call, which already passes through that lookup. So a thread can never stop holding a host lock (stdio, heap, runtime). The stderr-lock-only variant (the experiment) works but doesn't cover the heap lock.
  2. Later: service DSOUND's APU interrupts so voice-off completes (drops the 2 x 5 s waits).
  3. Then the heap exhaustion (48.5 MiB guest heap; check leaks and 18.4's `NtFreeVirtualMemory` failures first).
- **Files changed:** `twoc/run_twoc.ps1`, `twoc/src/main.c`, `twoc/src/recomp_manual.c` (`e16c13b`); this entry and "Start here tomorrow".

### 18.27 Step 25: diagnostics kept in the repo, cooperative suspend (committed) fixes the 18.26 deadlock, and the intro-load heap exhaustion is a leak: `NtFreeVirtualMemory` never frees.

Report: `step25_coop_suspend_and_heap_report.md` in the session scratch folder. Commits: `bfd0fd1` (diagnostics) and `6f51e4b` (cooperative suspend), `twoc/` only. The part-3 tap is reverted (source = `6f51e4b`, rebuilt) and kept as `twoc/tools/diag/step25_vm_tap.patch`.

- **Diagnostics (`bfd0fd1`):** `twoc/tools/diag/` holds step 24's `step24_temp_diagnostics.patch` (host-thread hang sampler, `/MAP`, `KeQuerySystemTime`/`NtSuspendThread` taps), `symbolize.py`, `fnof.py` (now finds `src/recomp/gen` itself) and `compare_runs.py`, plus a README. Step 25 adds `step25_vm_tap.patch` and `vmacct.py`.
- **Cooperative suspend (`6f51e4b`, 18.26's proposal 1):**
  - `recomp_lookup_manual` routes every guest kernel call (thunk VAs `0xFE000000..`) through `coop_kernel_call`, which tracks per-thread state: guest, in a kernel call, or parked.
  - A thread with a pending suspend parks on entry to, and on return from, its outermost kernel call. Nested calls never park.
  - `[0x15AA7C]`/`[0x15AA80]` (ordinals 231/224, checked against the XBE thunk table) go to hand-written `coop_NtSuspendThread`/`coop_NtResumeThread`. Suspend bumps a count and waits up to 100 ms for the target to be parked or inside a kernel call. Resume signals a per-thread event.
  - Non-spawned handles, and resumes with no cooperative count, go to the bridge.
  - Log tag `[SUSPEND]` (first 64 operations).
- **Verified with `-FastTitle -AutoStart`:**
  - Both suspends from `0x000ECA87` (title and intro level setup) find the stream thread in a kernel call.
  - The main thread keeps logging after the intro one. The file-open order is identical to 18.26's stderr-lock experiment.
  - `[WATCHDOG]` printed with deadlines of 64 s and 65 s while the stream thread was suspended. It never printed in steps 23/24.
  - A default 40 s run still ends at the watchdog (exit 3).
  - The level now stops at the heap exhaustion instead. After it: 18.24's AV at guest `0xFFFFFFFD` (innermost `0x1241B3`) in 5 runs, and a stack overflow after a null-ICALL loop in 1.
- **Heap exhaustion = leak, not heap size.**
  - `bridge_NtFreeVirtualMemory` (`kernel_bridge.c:970`) passes the guest `PVOID*` to `xbox_NtFreeVirtualMemory` as a 64-bit `PVOID*`. That function reads two guest dwords as one pointer (18.4's symptom, now explained) and calls host `VirtualFree`, which fails.
  - Even a correct read would be wrong: `NtAllocateVirtualMemory` hands out `xbox_HeapAlloc` blocks, which only `xbox_HeapFree` can release.
  - So every `MEM_RELEASE` leaks and every `MEM_DECOMMIT` fails. The fast run had 1,429 failures: 13 releases, 1,416 decommits.
  - Measured with the tap (`[0x15AB08]` ordinal 184, `[0x15AB0C]` ordinal 199): peak live guest VM is 24.3 MiB. The guest released 24.9 MiB and got none of it back, so the heap was at 47.2 of 48.5 MiB when the 4,336,960-byte request failed.
  - 41 MiB of the regions come from the guest heap's large-block path `sub_000F05E3` (`MEM_COMMIT|MEM_NOZERO`), freed by `sub_000F0D65`. Six are 1,228,848 bytes (640x480x4 + 48). The OOM heap dump is entirely these regions.
- **Experiment `TMP_FIX_FREE=1`** (`MEM_RELEASE` → `xbox_HeapFree`, decommit = successful no-op):
  - No OOM. The intro loads `introchars.dat`, then `intro2\house.cut`, `intro\control.nux` and `intro2\control.cut`, and level setup reaches its resume at `0x2C245`.
  - The run ends normally at 120 s. The main thread is then in D3D's push-buffer/fence wait (`0x1034C6`/`0x103729`, `DMA_PUT` ahead of `DMA_GET`), not yet examined.
- **Also seen:**
  - `xbox_HeapAlloc` reuses a freed block first-fit without splitting it, so a small request can take a whole freed multi-MB block. It's latent until frees work; it's in `xboxrecomp/`, so the rule applies.
  - `MmFreeContiguousMemory` frees (`0x80xxxxxx`) reach `xbox_HeapFree` and never match. The separate 64 MB contiguous arena is never returned, but it isn't close to full.
- **Proposal (not implemented):**
  1. A hand-written `NtFreeVirtualMemory` in `recomp_manual.c` for `MEM32(0x15AB0C)`, using the already-public `xbox_HeapFree`/`xbox_HeapBlockSize`: release frees the heap block and reports its size, decommit is a successful no-op, anything else goes to the bridge. It's the experiment made permanent.
  2. Then look at the D3D push-buffer wait.
  3. Later, decide what to do about no-split reuse: watch it, allow an `xboxrecomp/` exception, or add a `twoc/` allocator.
- **Files changed:** `twoc/tools/diag/*` (`bfd0fd1`, plus step 25 files in the notes commit), `twoc/src/recomp_manual.c` (`6f51e4b`); this entry and "Start here tomorrow".

### 18.28 Step 26: hand-written `NtFreeVirtualMemory` (committed) fixes the leak and the intro cutscene plays; the "D3D wait" is not a hang but a slow emulated GPU, ~83% of it overhead.

Report: `step26_ntfree_heap_d3dwait_report.md` in the session scratch folder. Commits: `145f8ef` (`twoc/`) and **`f8da7ec` in the `xboxrecomp/` clone (local only, on `52531ac`)**. The profiling sampler was temporary; it's reverted and kept as `twoc/tools/diag/step26_profile_sampler.patch` with `prof.py`.

- **`NtFreeVirtualMemory` (`145f8ef`, 18.27's proposal 1):** `[0x15AB0C]` now goes to `vm_NtFreeVirtualMemory` in `recomp_manual.c`.
  - A `MEM_RELEASE` at a heap block start calls `xbox_HeapFree` and reports the block size. The start test is `xbox_HeapBlockSize(base-1) != blk+1`.
  - A `MEM_DECOMMIT` with a non-null base is a successful no-op. Anything else goes to the bridge.
  - It runs inside the cooperative boundary: `coop_kernel_call` is now `coop_enter`/`coop_leave` around the call, reusable by hand-written kernel functions.
- **Verified** (`-FastTitle -AutoStart`, 120 s against a same-day baseline):
  - `NtFreeVirtualMemory failed` in `xbox_kernel.log` went from 1,429 to **0**. Nothing fell through to the bridge.
  - No OOM, no AV at `0xFFFFFFFD`, no stack overflow. The run ends at the watchdog (exit 3).
  - The intro loads `introchars.dat` → `house.cut` → `control.nux` → `control.cut`, and level setup resumes the stream thread.
  - At 180 s it goes on to open `SFX\VCE_ENG\INTRO1.wav` and renders the intro cutscene until the watchdog.
- **Heap watch (same commit, decision 3 = accept and watch):**
  - A `[HEAPSTAT]` line every 2 s: bump used/total, live/free from the block table, largest free block, VM release/decommit/bridged counts, and the "reuse slack" of `NtAllocateVirtualMemory` requests that landed in a block ≥ 64 KiB bigger than asked.
  - Needs `xbox_HeapStats()`, a read-only accessor over the allocator's state, committed in the clone as `f8da7ec` (18.9 rule: thin, additive, no behaviour change; not pushed).
  - **180 s:** bump used reaches **46,455/49,664 KiB (93.5%) at ~64 s and stays flat to 180 s.** Live peak is 42.9 MiB. 37.4 MiB was released and reused.
  - Reuse slack peaked at 3,039 KiB from one no-split reuse.
  - Headroom is thin: 3.1 MiB never used plus 6 free blocks, largest 4.1 MiB. A heavier level could fail to fit a large block while total free is enough. Watch `[HEAPSTAT]`.
- **Found:** the game's main thread runs *inline* inside the first `PsCreateSystemThreadEx` kernel call (`kernel_bridge.c:516`), so its cooperative depth is always ≥ 1 and it never parks. That's harmless now (a suspend of the main thread already goes to the bridge), but worth knowing.
- **The 18.27 "D3D push-buffer/fence wait" is throughput, not a stall.**
  - Over 90-180 s, executor draws rise steadily (~1,100/s). The PUT ring wraps every ~5 s, GET trails by one segment at most, and the fence pair moves together (~42 fences/s).
  - The frame rate is about **6.5 fps** (~65 clears per 10 s over three 640×480 surfaces, ~130 draws per frame).
  - "DMA_PUT ahead of DMA_GET (GPU behind)" is mostly a logging artifact: the ack thread logs just after PUT moves, before it copies PUT to GET.
- **Where the main thread actually waits: KickOff `sub_00103330`, not the fence wait `sub_001034D0`.**
  - Chain: game loop → `sub_0003B010` → draw `sub_00101BC0` → `sub_00103420(1)` → `sub_00103330`.
  - It sets PFB `0xFD100410` bit 16 (write-combine flush) and spins until it clears.
  - Only the runtime's `nv2a_ack_thread` clears it, once per loop. With `RECOMP_PB_EXEC` that same loop runs the software executor over the previous segment first, so CPU and GPU run in lockstep.
  - Profile (100 ms sampler, 90-180 s): the main thread is **98.6% in `sub_00103330`** at 98% CPU. The game's own work is ~1.4% of wall time.
- **Where the emulated GPU's time goes** (ack thread, 86% CPU):
  - **51.9% in `getenv("RECOMP_PB_EXEC_VERBOSE")`**, called unconditionally on every NV2A method (`nv2a_pb_exec.c:1551`; also per draw at `:1275`).
  - **31.3% in `NtWriteFile`:** the per-kick `[NV2A] DMA_PUT` line plus `fflush`, ~47/s. `RECOMP_PB_EXEC` implies `s_nv2a_trace` (`xbox_memory_layout.c:1886`). The ~6 ms per write is suspicious and not isolated.
  - Only ~15% is real work (raster, texture sampling, scan).
- **Also:** `kernel_timer_thread` burns ~43-47% of a core in the DPC's NV2A status poll (`sub_00106720`). The vblank count `[0x1BABB0]` advances ~30/s, not 60. Neither is investigated.
- **Proposal (not implemented):**
  1. Cache that `getenv` in a static (~4 lines).
  2. Take the per-kick trace line and `fflush` off the hot path. First, time a run with stderr to a pipe or NUL.

  Both are in `xboxrecomp/` and aren't accessors, so they **need a decision on a perf-only rule exception**. There's no twoc-only route into `nv2a_pb_exec_method`. Together they could make the executor up to ~5x faster (estimate). Next, optional and twoc-only, measure after 1-2:

  3. A hand-written KickOff whose write-combine flush completes at once, so the game can run ahead until the push buffer really fills (the GET waits in `sub_001035C0`/`sub_001034D0` are the real back-pressure).

  Separately: the DPC poll and the 30 Hz vblank.
- **Files changed:** `twoc/src/recomp_manual.c` (`145f8ef`), `xboxrecomp/src/kernel/xbox_memory_layout.{c,h}` (`f8da7ec`, local), `twoc/tools/diag/{step26_profile_sampler.patch,prof.py,README.md}` (notes commit); this entry and "Start here tomorrow".

### 18.29 Step 27: the executor perf fixes (18.28 proposals 1 and 2, `xboxrecomp/` local only) make the intro cutscene 3.5x faster (6.8 → 24.0 fps); the title reaches the menu in 129 s instead of 226 s. Proposal 3 isn't worth it.

Report: `step27_executor_perf_report.md` in the session scratch folder. Commits in the `xboxrecomp/` clone, local only, on `f8da7ec`: **`694cb98`** (P1) and **`5012b2d`** (P2), under the new perf-only rule exception (top of section 18). The profiling patch was applied for one run and reverted.

- **Measuring frame rate without the log.**
  - The guest frame counter is **`0x25A660`**: `sub_0009A670` does `inc [0x25A660]` once per main-loop pass (called from `sub_00039E00`). D3D's `0x10EBD4` moves with it.
  - I found them by scanning guest `0x100000..0x946000` for steady counters. The log has no frame count: the executor never sees `FLIP_STALL` and `PCRTC_START` never changes.
  - New `twoc/tools/diag/fpsrun.py` launches the game with `run_twoc.ps1`'s variables and reads guest memory from outside the process (`ReadProcessMemory`, VA + `0x10000`) every 0.5 s. So stderr can go to a file, NUL or a pipe. `fps.py` summarises the samples.
  - Cutscene fps is counted from the end of the intro load to the 180 s watchdog, with `-FastTitle -AutoStart -Seconds 180`.
- **P1 (`694cb98`):** `pb_exec_verbose()` in `nv2a_pb_exec.c` reads `RECOMP_PB_EXEC_VERBOSE` once. It's used by all three checks (per method, per draw, per flip). Nothing sets the variable at runtime, so the output is identical.
- **P2 measured first (after P1):** the cutscene ran 21.4 fps with stderr to a file, 23.4 to NUL and 22.9 to a pipe, a 9% cost (~11% steady). The `[NV2A] DMA_PUT` line was 92% of stderr: 109,825 of 119,844 lines in 180 s, ~600/s.
- **P2 (`5012b2d`):** in `xbox_memory_layout.c`, the `DMA_PUT` line implied by `RECOMP_PB_EXEC`/`RECOMP_PB_SCAN` is written at most once a second. `RECOMP_NV2A_TRACE` itself (new flag `s_nv2a_trace_every_kick`) still logs every kick. A PUT that stops moving is still reported every 2 s. After P2 the file-vs-NUL gap is gone (23.96 vs 24.04 fps), with 173 `DMA_PUT` lines per run.
- **Results:**

  | | Cutscene avg fps | Run time 120-180 s | Cutscene frames 500-850 | Title without `-FastTitle`, launch to banner |
  |---|---|---|---|---|
  | Before (`f8da7ec`) | 6.8 | 3.6 | 5.0 | 226 s (6.1 fps) |
  | P1 | 21.4 | 18.9 | 30.4 | not measured |
  | P1+P2 | **24.0** | 21.8 | 33.3 | **129 s** (10.8 fps) |

  - With `-FastTitle` the menu opens at 10.5 s instead of 13.5 s, and the cutscene starts at 45 s instead of 53 s.
  - Before the fixes, the fps fell from ~10 to ~3 as the cutscene went on: the later shots carry about twice the methods, and `getenv` was paid per method. That drop is gone now.
  - **Correction:** the "~12.5 min" title time in 18.25/18.26 was already stale. Today's pre-change baseline is 226 s. I didn't look into which earlier fix changed it.
- **Profile after P1+P2** (`step26_profile_sampler.patch`, run 90-180 s):
  - The main thread is still 95.2% in KickOff `sub_00103330`; its own work is ~5%.
  - The NV2A ack thread is saturated (99.3% CPU) with real work: **48.5% leaf in `nv2a_pb_scan`**, 22.2% `raster_triangle`, 20.6% `sample_texture`, 3.7% `note_unhandled`.
  - The 48.5% is the survey's `note()` (`nv2a_pb_scan.c:48`): a linear search over ~370 seen methods on every method word. `note_unhandled` is the same pattern.
  - `kernel_timer_thread` is 12.9% (was 43-47%). Not investigated.
- **Proposal 3 (hand-written KickOff): not worth it now, and not safe as described.**
  - The gain is capped at the game's ~5% share, since the GPU thread is saturated.
  - The ack loop clears the flush bit, then sets GET = PUT, and only then executes `[last_put, put)`. It skips a segment that wrapped.
  - KickOff's lockstep is what keeps D3D from reusing pushbuffer space the executor hasn't read yet. Running ahead would need GET updated after execution. That's a behaviour change in `xboxrecomp/`, outside the exception.
- **Proposal (not done):**
  1. Make `note()` O(1): a 16 K direct index keyed by `(subch << 11) | (method >> 2)` into the existing insertion-ordered `s_seen` table. The `[PB]` report stays identical. Likewise `note_unhandled`. It's perf-only with identical output, so it fits the exception. Up to ~1.8x executor throughput (estimate).
  2. Then look at KickOff and GET ordering again together, if the main thread's share has grown enough to matter.
- **Files changed:** `xboxrecomp/src/kernel/nv2a_pb_exec.c` (`694cb98`, local), `xboxrecomp/src/kernel/xbox_memory_layout.c` (`5012b2d`, local), `twoc/tools/diag/{fpsrun.py,fps.py,discover.py,README.md}` (notes commit); the rule refinement at the top of section 18, this entry and "Start here tomorrow".

### 18.30 Step 28: O(1) survey lookups make the intro 2x faster (23.1 → 46.9 fps); the intro ends and loads the hub, which hung on a second GP DSP wait (fixed); after that Crash walks in the hub, and his position changes in memory.

Report: `step28_note_index_and_hub_report.md` in the session scratch folder. Commits: **`fe406e4`** in the `xboxrecomp/` clone (local only, on `5012b2d`, perf-only exception) and **`c262d7d`** (`twoc/`). The temporary shadow check, profile sampler and kernel tap were reverted; the tap is kept as `twoc/tools/diag/step28_ktap.patch`.

- **Part 1, `note()`/`note_unhandled()` O(1) (`fe406e4`, 18.29 proposal 1):**
  - `note()` uses a direct index keyed by `(subch << 12) | (method >> 2)`. It's 12 bits, not the 11 proposed, because an increasing run steps past `0x1FFC` (up to `0x3FF4`), so 11 bits would collide.
  - `note_unhandled()` uses an index keyed by `method >> 2`. It's rebuilt after the report, because the report selection-sorts `s_unhandled` in place. A method that isn't 4-aligned or is ≥ `0x4000` falls back to the linear search (none seen).
  - **Identical output, checked:** a temporary shadow build ran both lookups on every call. In 180 s: 263,182,988 `note()` and 85,535,832 `note_unhandled()` calls, **0 disagreements**, 18 reports.
  - **Cutscene fps** (`fpsrun.py`/`fps.py`, 180 s fast run): **23.1 → 46.9** (50.0 on a second run with `c262d7d`). Frame 3000 at 130 s → 66 s. The title phase is unchanged (~7.4 fps). Step 27's 24.0 measured 23.1 today.
- **The intro ends by itself.** `INTRO1.wav` at ~45 s, `INTRO2.wav` at ~5 min, then the hub loads at ~10 min.
  - The hub is `levels\b\hub\*`, `hubchars.dat`, Crash's full animation set, and Coco/Aku Aku/Uka Uka/Cortex/Crunch `hubin`/`hubout`.
  - `hub.cpt`, `hub.obj`, `hub.vis` and `hubcubemap.dds` fail with FILE_NOT_FOUND. None is on the disc, so that's normal.
  - **Start skips the intro:** one press after `INTRO1.wav` opens, and the hub loads 8-17 s later.
- **Then the hub hung.** Level setup resumes the stream thread, then the main thread makes no more kernel calls and the pushbuffer stops (`PUT == GET`).
  - Profile: 77% leaf in **`sub_0013937A+0x121`**, called from the sound manager `sub_000B9700` → `sub_00137516` → `sub_00136EFC` → `sub_001367B9` → `sub_00139A36`.
  - `sub_0013937A` posts GP DSP command 2. First it spins on `[scratch+0x800+0x10]` (`0x1393A8: cmp [eax+0x10],0 / jne`). That's **the same command word as 18.25's `sub_001391DE`** (`scratch+0x810` = `0x80BC8810`).
  - With no DSP, the intro's command 2 stays posted and the next post spins forever. 18.25's "the only such wait in DSOUND" was wrong: the loop polls `[eax+0x10]`, not the word's own address.
- **Fix (`c262d7d`):** `sub_0013937A` is rewritten in C, with the same register and memory effects. A pending command is taken as consumed and cleared, the same answer as `sub_001391DE`. Log tag `[DSP] GP command 2 ... taken as done` (one answer at the hub load).
  - A scan of DSOUND (`0x130000..0x148000`) for tight memory polls finds two more, at `0x13BE31` and `0x13C479`. Both poll APU voice bit `[[0x153310]+voice*0x80+0x54] & 0x100000`, not the DSP. Neither is hit yet.
  - Regression: same 145 file opens in a 180 s fast run, exit 3.
- **Also in `c262d7d`: `RECOMP_DEV_INPUT_FILE=<path>`** (test-only, inert when unset). Port 0 also reads a one-line file every ≥ 50 ms (`buttons=0x10 a=255 lx=-32767 ly=32767`, and so on), with a virtual pad like autostart. Each change is logged as `[DEV] input file (+t s)`. A test can drive the pad while the game runs.
- **After the fix, the hub runs** at ~10 fps (the intro runs at 47-50):
  - The entry dialogue plays (`USCOR45` → `USCRN01` → `USCOR46`), then the hub music (`THEMEL.wav`).
  - Input arrives: XAPI slot table at `0x427598`; processed stick Y float at `0x427634`; the game's PS2-style bits at `0x23C470`/`0x23C478` (`0x1000` up, `0x4000` down); analog bytes at `0x427678` (`0x80` centre).
- **Crash's position changes with the stick.** The player object is the static **`0x8F5120`**, passed to `sub_00027C70` (call at `0x2B44F`) with the pad `[0x23C2D8] = 0x427598`. Position (x, y, z) is at `+0x6C` (DSOUND gets `0x8F518C` as a 3D position), state at `+0x172` (3 standing, 2 moving), idle timer at `+0x138`.
  - With `crashpos.py`, 5 s per direction: ~0.25-0.40 units/frame, following the floor (y 7.85-8.06), stopping at walls and still on release. Example: (−4.14, 8.05, −3.85) → (14.36, 7.95, 5.40).
- **Caveat: Crash can start in a zone that captures the stick.** In one run he stayed at (−0.01, 8.37, 4.41) the whole time: 0.3 above the floor, and inside the quad `sub_000681B0` tests (x/z corners (±2, 5), (±0.5, 2) at `0x19E158`, near (0, 8, 3.5) at `0x19E188`).
  - There, up/down only stepped a small vector at `0x1A4794`, and A enumerated `UDATA\56550003\` on 4 of 5 presses: menu-like, probably a save/load point, not verified.
  - In the other run, one A press 15 s after the hub music played a sound, and he was then outside the zone and free.
  - Related, not traced: slot `0x5607C0` (`+0x6E = 0xFF`) makes `sub_0006E130` skip its input block. `0x2B1D2..0x2B25B` binds character `0x15` to it (`sub_0006AF20(0x5607C0, 0x15, 4, -1)`, sound `0x36`) once `0x8F5120` is in that zone for 60 frames, or on pad bit `0x40`.
- **Screen:** all three surfaces (`0x80404000`/`0x80530000`/`0x8065C000`) are full-screen garbage in the hub, with no readable overlay. It isn't usable for identifying UI.
- **Proposal (not done):**
  1. Profile the hub's 10 fps (`step26_profile_sampler.patch`).
  2. Identify the start zone's UI only if it blocks something.
  3. Watch the two APU voice-bit waits.
- **Files changed:** `xboxrecomp/src/kernel/nv2a_pb_scan.c`, `nv2a_pb_exec.c` (`fe406e4`, local); `twoc/src/recomp_manual.c` (`c262d7d`); `twoc/tools/diag/{memsnap.py,probe.py,crashpos.py,step28_ktap.patch,README.md}` (notes commit); the 18.25 pointer, this entry and "Start here tomorrow".

### 18.31 Step 29: Option B planning pass. All D3D entries identified; a mirror bridge makes the swap incremental, and Tier B needs no new object model.

Report: `step29_option_b_plan.md` in the session scratch folder (with `table.json`, `tables.md`, `d3d99_asm.txt` and the scripts). Planning only: no code, re-lift, rebuild or run. Executor perf work is dropped (Option B replaces the executor).

- **All 99 entries identified by XDK name, plus a 100th.** `D3DDevice_End` (`0x101F00`) is reached by a tail `jmp` from `0x3AE30`, so the call-edge count in 18.21 missed it. The full table (tier, sites, name, NV097 methods emitted, bridge action) is in the report.
- **Step 19 labels corrected.** `0x100DD0` is `D3DVertexBuffer_Lock`, not a cursor primitive. `0x102440` is `CreateVertexShader`. `0xFEF20` is `SetRenderTarget`, not a release helper. The D-big 10 are:
  - Swap `0x100C40` (not Reset);
  - DrawIndexedVertices `0x101BC0`, DrawVerticesUP `0x1019C0`, DrawVertices `0x101B20`, Begin `0x101EC0`;
  - ApplyStateBlock `0x101705`;
  - the device's Release `0xFEC50`;
  - MultiSampleMode / MultiSampleRenderTargetMode `0xFD7A0`/`0xFD7D0`;
  - SetRenderTarget `0xFEF20`.
- **The D3D library.**
  - Static device at `0x10C110`, with `[0x10EBF0]` pointing to it.
  - `D3D__RenderState` at `0x10EE18` (146 states in XDK order: 0-56 pixel shader, 57-81 simple, 82-116 deferred, 117-145 complex). `D3D__TextureState` at `0x10EC18` (4 x 32). Dirty flags at `0x10EC10`.
  - Resources use the XDK header: `Common` holds the refcount (`& 0xFFFF`) and type; `Data` is physical.
  - Device field offsets are in the report.
- **The game side.**
  - Its render-state dispatcher `0x3A830` (21 callers) stores every value into `D3D__RenderState`, and the TSS dispatcher `0x3AC60` does the same for `D3D__TextureState`.
  - The SetTexture wrapper `0x3AAE0` has a fast path that pushes `TEXTURE_OFFSET`/`FORMAT` through `0xFEC00` without calling SetTexture.
  - No game code touches the push-buffer cursor.
- **The key result: a mirror bridge.** Native D3D keeps running as lifted code; each wrapper calls the original first, then renders the same call through `xbox_d3d8` from host-side state.
  - The bridge writes no guest memory, so 18.20's crash can't happen. The all-or-nothing constraint only applied to replacing the device.
  - The lifter already supports this: in `recomp_manual.c`, declare `extern void sub_X_gen(void);` and define `sub_X`. The lifted body is then emitted as `sub_X_gen` (`manual_scan.py`, `_WRAP_RE`). No toolkit change. *(Corrected by 18.32: the lifter renames every reference too, so callers skipped the wrapper. `twoc/tools/wrap_callers.py`, run by `lift.ps1`, fixes that.)*
- **Tier B resolved: no new object model.**
  - The native XDK structures stay the only representation. Refcounts are native only.
  - Host textures, render targets and shaders are a cache keyed by guest header address with a signature check. They're invalidated by LockRect wrappers and by intercepting `MmFreeContiguousMemory`.
  - No host vertex or index buffers: each draw copies its vertex range into `xbox_d3d8`'s UP ring buffer. That also gets around its stream-0-only limit.
- **Only 15 entries need wrappers** (29 sites): CreateDevice, Clear, Swap, 4 draw paths + SetVertexData2f/4f + End, CopyRects, SetVertexShaderConstant, 2 LockRects and `0xFEC00`.
  - 57 stay native: their effect is read at draw time from D3D's shadows.
  - 28 are queries or bookkeeping.
  - The 36-ish "Tier C" setters need no per-method bridge. `nv2a_regs.h` already has the NV097 method table (214 methods), and it's only needed to decode `0xFEC00`.
- **The real work.**
  - A draw-time state translator. `xbox_d3d8` uses PC D3D8 numbering and values; the XDK uses its own indices and GL-style values (ShadowFunc subtracts `0x200`, CullMode uses `0x900`/`0x901`).
  - Texture and vertex fetch from guest memory.
  - Known `xbox_d3d8` gaps: stream 0 only; vertex-shader declarations ignored; no CreatePixelShader, ZBias, CopyRects or immediate mode. Each has a `twoc/` work-around listed in the report.
- **Plan: about 8 sessions, each testable, default run unchanged** (`RECOMP_D3D_BRIDGE=1` gate):
  - S1: wrapper plumbing and a log-only census.
  - S2: host device, Clear, Swap.
  - S3: the splash, compared with executor dumps (at least 99% of pixels within +/-8).
  - S4: title and menu.
  - S5-S6: 3D (intro, hub).
  - S7: executor off, fps.
  - S8: buffer.
  - This replaces 18.21's "10-15x CreateDevice, all at once".
- **Decisions needed:**
  1. Adopt the mirror design.
  2. `xbox_d3d8` gaps: work around in `twoc/` first; allow local `xbox_d3d8` commits only if a translator bug has no work-around.
  3. A 3D reference (xemu captures), since the splash is the only known-good output.
- **Files changed:** this entry and "Start here tomorrow".

### 18.32 Step 30: Option B session S1. The wrappers are in (log-only), the default run is unchanged, and the census covers splash to hub. The splash draws with DrawVerticesUP.

Report: `step30_s1_census_report.md` in the session scratch folder, with every run's logs, `censusfinal.summary.txt` and `censusfinal.keys.txt`.

- **Decisions (user, Sep 25):**
  1. Mirror design (M) adopted.
  2. `xbox_d3d8` gaps are worked around in `twoc/` first; a local `xbox_d3d8` fix only for a translator bug with no work-around. That's the rule refinement 18.32 at the top of section 18.
  3. The user captures xemu screenshots (the intro's first shots, the hub spawn) into `twoc/reference/`, now gitignored. S5/S6 use them.
- **Built:**
  - `twoc/src/d3d_bridge.{c,h}`: `RECOMP_D3D_BRIDGE` (off by default) and a log-only census. Every 10 s, and at the splash and menu marks, it writes a block of `[D3DB] c <t> <interval> <total> <key>` lines. It never writes guest memory.
  - `recomp_manual.c` has 24 wrappers `void sub_X(void) { D3DB_CALL(id, sub_X_gen); }`: the plan's 15, plus 9 census probes (`FD680`, `FEC90`, `39910`, `1012CC`, `10191E`, `101705`, `10188D`, `FDC90`, splash `875A0`). With the bridge off, a wrapper is one branch and the lifted call.
  - `main.c` calls `d3db_init()`. `twoc/tools/diag/drive.py` is the unattended splash-to-hub driver; `census.py` splits a census log into phases.
- **18.31's "no toolkit change" was half wrong.** `tools.recomp` implements `_gen` by renaming the function in its database. Direct calls, tail jumps (`0x3AE30` → End) and the dispatch table were all renamed to `sub_X_gen`, so no caller reached the wrapper.
  - Fix, in `twoc/` only: `twoc/tools/wrap_callers.py`, run by `lift.ps1` after the lifter, renames every reference back to `sub_X` except the body's own definition line. It fails if a wrapped function doesn't have exactly one body.
  - Checked: a diff of the whole generated tree before and after shows only the 24 body names (and their comment headers) changed.
- **Default run identical (bridge off).** Same scripted 150 s run (`drive.py`, Start 6 s into the intro, then A and the stick in the hub), old build against final build:
  - the same 231 CdRom opens in order, ending `THEMEL.wav` in the hub;
  - 39 event lines each, differing only in timestamps and the pad path;
  - 74 `[HEAPSTAT]` each, exit 3, no `[D3DB]` line.
- **The splash is `DrawVerticesUP` only.** About 2 draws a frame:
  - XDK TRIANGLELIST, 6 vertices, stride 28, FVF `0x144` (XYZRHW|DIFFUSE|TEX1);
  - no pixel shader, one DXT5 texture;
  - to the back buffer, with 16 RS and 8 TSS values set.
- **Probable identities, all settled:**
  - `0xFEC90` is **SetVerticalBlankCallback**. `device+0x2430` = `ctx+0x1C8`, which the vblank handler `sub_00106720` calls. `0x39910` runs on the DPC thread at 31-38/s, independent of Swap.
  - `0x1012CC`/`0x10191E` are **Begin/EndStateBlock**: they set and clear `device+8` bit 0x20, and every Apply/Delete gets a live End token.
  - `0xFDC90` is **Dxt1NoiseEnable**: dispatcher state 139, stores RS[139].
  - `0xFD680` returns 1 once at boot, gating GetDeviceCaps. It's GetAdapterCount-like; the exact name doesn't matter (Q).
- **Census, per frame** (executor on; the census costs ~10% fps):

  | | DrawIndexed | DrawUP | Immediate quads | VS constants | Apply state block |
  |---|---|---|---|---|---|
  | title | 21 | 59 | 18 | 287 | 43 |
  | menu | 126 | 13 | ~125 | 1517 | 34 |
  | intro | 115 | 2 | ~1270 | 936 | 137 |
  | hub | 308 | 16 | ~515 | 6755 | 184 |

  - **Never called:** DrawVertices and the `0xFEC00` fast path.
  - **Primitives:** TRIANGLELIST 5, TRIANGLESTRIP 6, QUADLIST 8 (immediate mode only).
  - **Shaders:** 3 FVFs (`0x142`, `0x144`, `0x152`), up to 14 programmable vertex shaders, pixel shaders on 4-38% of draws.
  - **Streams:** streams 0+1 (stride 36 or 56) from the intro on.
  - **Textures:** DXT5, P8, A8R8G8B8, LIN_A8R8G8B8, and DXT5 cube maps.
- **What changes in the plan (report section 4):**
  1. `xbox_d3d8` numbers primitives PC-style (TRIANGLELIST 4, STRIP 5). The XDK passes NV2A ops (5, 6), so map them.
  2. XDK Clear flags are TARGET `0xF0`, ZBUFFER 1, STENCIL 2. Flags `3` (Z+stencil) is the common per-pass clear.
  3. **Draws come from more than one thread.** Each loading screen draws from its own new thread, under the game's D3D lock `0x4EA440`. *(Refined by 18.33: the main thread holds that lock for only ~1% of its D3D calls, but no main-thread call ever comes while a loading screen holds it.)*
  4. There are six 128x128 A8R8G8B8 render-to-texture targets every frame from the title on. CopyRects copies the whole back buffer into a 640x480 LIN texture about 3 times a frame in the hub.
  5. No lock of a rendered surface anywhere, so no readback is needed.
  6. The hot wrappers (VS constants, immediate data) must buffer, not call the host per call.
- **`RECOMP_PB_EXEC` unset: the fence mirror alone reaches the menu in 3.7 s.** But the frame loop runs unpaced (title ~720 swaps/s, menu ~180/s), so the menu's idle timeout fires before autostart's press, and the game plays the attract demo (`levels\a\snow_m`, ~32/s, exit 3). **Native Swap doesn't pace without the executor.** The plan's "present without vsync" is reversed for S7: the host present must pace. *(Corrected by 18.33: the demo didn't play. Both no-executor runs were stuck in the hub-load deadlock below, at the demo's level load, and the ~32/s was its loading screen.)*
- **Found, not fixed: an intermittent hub-load deadlock in the cooperative suspend (18.27).** 1 of 8 hub loads today, with the bridge on (Start 30 s into the intro).
  - The stream thread took DSOUND's lock `0x152BB0` and was suspended by level setup ("target in a kernel call") while still holding it.
  - The main thread then waited on `0x152BB0` inside level setup (watchdog stack: `0x13610E`, `0x1366B9`, sound manager `0xB8F5E`...), so the resume at 0x2C245 never ran. The loading screen kept drawing.
  - The bridge takes no guest lock and never touches that thread.
  - Proposal: don't park a thread that owns a guest critical section. Wait for a kernel-call boundary where it holds none; if the owner tracking is internal to `xboxrecomp/`, add a thin accessor (18.9). *(Done in 18.33, `800f27b`, without an accessor.)*
- **Files changed:** `twoc/src/d3d_bridge.{c,h}` (new), `twoc/src/recomp_manual.c`, `twoc/src/main.c`, `twoc/CMakeLists.txt`, `twoc/lift.ps1`, `twoc/tools/wrap_callers.py` (new), `.gitignore` (code commit); `twoc/tools/diag/{drive.py,census.py,README.md}`, the rule refinement at the top of section 18, the 18.31 correction, this entry and "Start here tomorrow" (notes commit).

### 18.33 Step 31: the hub-load deadlock is fixed (a parked thread owns no guest lock), and Option B S2 shows the game's cleared frames in step with it. Blt-model presents never reach the screen on this machine, so the bridge presents through its own flip-model swap chain.

Report: `step31_report.md` in the session scratch folder. Everything is in `runs/` there: logs, dumps, screen grabs, the standalone D3D11 test `wintest/`, and the helpers `screengrab.py`, `tintcheck.py` and `instep.py`. Commits: `800f27b` (the fix) and `c6f1588` (S2), `twoc/` only. No `xboxrecomp/` change.

- **The fix (`800f27b`, `recomp_manual.c`):**
  - The game takes critical sections only through `RtlEnterCriticalSection`/`RtlLeaveCriticalSection` (thunks `[0x15AA64]`/`[0x15AA60]`). It imports no TryEnter and no AndRegion.
  - So `coop_kernel_call` keeps an exact per-thread list of the sections held. The runtime's owner table is internal, but the boundary sees the same thing, so no accessor was needed.
  - A thread with a pending suspend parks only at a boundary where it owns none. The suspender's 100 ms settle wait counts a target that is taking or holding one as not stopped yet.
  - Log line: `[SUSPEND] park thread T: count 1, after N boundaries holding a guest lock`.
  - `coop_holds_cs()` exposes the list. `coop_watch_cs()`/`coop_cs_holders()` (`c6f1588`) count one section's owners across threads.
- **Verified deterministically:** `twoc/tools/diag/step31_suspend_stress.patch` makes the suspend land while the stream thread's innermost section is `0x152BB0`, then has the suspender take `0x152BB0`, as level setup did.
  - Fix off: the target parks holding it, and the intro load hangs forever.
  - Fix on: the target runs 129 more kernel calls, releases the lock, parks, and the run reaches the hub.
- **Plain hub loads, Start +30, three census-on and three off:** all six reach the hub at 144.0–144.8 s.
  - In every one, the intro-load suspend found the stream thread holding a lock (130 boundaries). That's the moment 18.32 hit.
  - Census off: the same 231 opens and 39 event lines as step 30's `off30`.
- **18.32's no-executor runs were this deadlock** (see the correction there).
  - With the fix, the demo loads. Unpaced, the game then cycles menu → demo, and every level load takes contiguous memory that's never returned (18.27's `MmFreeContiguousMemory` gap).
  - After ~36 s (`[CONTIG] arena exhausted`, then a failed read), an access violation at guest `0xEDD73`. That's an S7 prerequisite; with the executor on, level loads are rare.
- **S2 (`c6f1588`):**
  - **Switches:** `RECOMP_D3D_BRIDGE=1` is now the host mirror. The census moved to `RECOMP_D3D_CENSUS=1` (they combine). `drive.py` has `--bridge`, `--census`, `--tint` and `--dump=DIR`.
  - **Host side** (`twoc/src/d3d_host.{c,h}`): one bridge lock around every host call, and a window thread that creates the window and pumps its messages and never takes the lock. As the user suggested; nothing better came up.
  - **CreateDevice** (after the lifted call) creates the `xbox_d3d8` device from the XDK present parameters.
  - **Clear** is mirrored for the back buffer only: `0xF0` → TARGET, 1 → ZBUFFER, 2 → STENCIL. A partial colour mask isn't mirrored (never seen). The six render-to-texture targets wait for S4. The game always clears to `0x00000000`.
  - **Swap** copies and presents with DXGI interval 0 while the executor paces (`RECOMP_D3D_BRIDGE_INTERVAL` overrides).
  - **Host dumps** go to `<DumpDir>\hostNNN.bmp` on the executor's cadence (first 8 frames with work, then every 10 s).
  - **Checks:** a `[D3DB] host` line every 10 s. `RECOMP_D3D_BRIDGE_TINT=1` clears to the frame index (R = k & 0xFF, G = k >> 8, B = `0x80`).
  - **Compare tool:** `twoc/tools/diag/fbcompare.py` pairs host and executor dumps by time.
- **Blt model is invisible here.**
  - `xbox_d3d8`'s swap chain uses `DXGI_SWAP_EFFECT_DISCARD`. `Present` returned `S_OK`, the dumps were right, and the window was black on screen.
  - A standalone D3D11 program shows the same: blt model black with the hardware device or WARP, DPI-aware or not, from the window's thread or another; a GDI control shows; the flip model shows.
  - This machine has an RTX 4080 SUPER plus an AMD iGPU, 3840x2160 at 225%, 239 Hz, Windows 11 26200. The cause isn't known.
  - Workaround, `twoc/` only: `xbox_d3d8`'s swap chain gets a hidden window. The visible window gets a flip-model swap chain (`FLIP_DISCARD`, 2 buffers) on the same device, and each present copies `xbox_d3d8`'s back buffer into it.
- **Exit criterion met (`s2m`, tinted, splash → hub):**
  - All 25 host dumps decode to exactly their guest swap - 1.
  - Host presents = guest swaps = 2,816, with no present failures or odd statuses.
  - **Real-screen grabs, with the game's frame counter `0x25A660` read around each one:** the screen shows the frame just presented, 1–5 frames behind the counter, through splash, title, menu and intro. From the hub load on, the offset is a constant +26/+27, which is the loading-screen thread's swaps, so there's no drift.
  - Rates are unchanged (intro ~40–50/s, hub ~10.5/s).
  - Bridge off: identical to the fix-only runs (231 opens, the same events, no `[D3DB]` line).
- **Threads and locks, answered at runtime:**
  - The loading-screen threads hold `0x4EA440` for every D3D call (108/108, 666/666, 669/669). The main thread holds it for about 1% of its calls.
  - A main-thread call while another thread held the lock: 0 in every run.
  - D3DX's draws (`0x11127A`/`0x11199E` are vtable methods, `.rdata` `0x169F2C`/`0x169EBC`) never ran from splash to hub.
- **Screen capture on this machine:** `PrintWindow` returns black for D3D content even with `PW_RENDERFULLCONTENT`. The terminal covers the default window position, and Windows ignores a topmost request. `screengrab.py` moves the window to the uncovered bottom-right corner and samples a point that `WindowFromPoint` confirms is the game window.
- **For S7 (measured):**
  - Executor off, host present at interval 1: exactly 240.0 swaps/s, because the display is 239 Hz. S7 needs its own ~59.94 Hz limiter, not DXGI vsync.
  - `MmFreeContiguousMemory` has to really free first. *(Done in 18.34, `dd4be13`.)*
- **Files changed:**
  - Code commits: `twoc/src/recomp_manual.c` (`800f27b`, `c6f1588`); `twoc/src/d3d_host.{c,h}` (new), `twoc/src/d3d_bridge.{c,h}` and `twoc/CMakeLists.txt` (`c6f1588`).
  - Notes commit: `twoc/tools/diag/{fbcompare.py,step31_suspend_stress.patch}` (new), `twoc/tools/diag/{drive.py,census.py,README.md}`, the 18.32 corrections, this entry and "Start here tomorrow".

### 18.34 Step 32: Option B S3 shows the splash through the mirror (the Universal logo on the real window, 29/29 frames within 8 of the executor with its sampling), and MmFreeContiguousMemory really frees. The next S7 limit is a 1.2 MB heap leak per level load.

Report: `step32_report.md` in the session scratch folder, with every run in `runs/` and montages in `view/`. Commits: `498f321` (S3) and `dd4be13` (contiguous memory), `twoc/` only. No `xboxrecomp/` change.

- **S3 (`498f321`): DrawVerticesUP is mirrored.**
  - `mirror_draw_up` (`d3d_bridge.c`) runs after the lifted call. It reads `D3D__RenderState`/`D3D__TextureState` by XDK index and translates them to xbox_d3d8's PC numbering and values: 43 render states, 13 stage states per stage.
  - The XDK values are GL-style:
    - comparisons `0x200`-`0x207`;
    - blend factors `0`, `1`, `0x300`-`0x308`;
    - blend ops `0x8006`/`0x800A`/`0x800B`/`0x8007`/`0x8008`;
    - stencil ops `0x1E00`/`0`/`0x1E01`-`0x1E03`/`0x150A`/`0x8507`/`0x8508`;
    - cull `0`/`0x900`/`0x901`;
    - colour-write R `1<<16`, G `1<<8`, B `1`, A `1<<24`.
  - The primitive map: XDK 5/6/7/8 → 4/5/6/8 (also 1/2/4 → 1/2/3), and the vertex count → a primitive count.
  - Textures are described guest side: format, size, levels, physical Data → `0x80000000 + P` below the arena's high-water (as the executor does), and a content signature.
  - They're cached and uploaded host side (`d3d_host.c`, 1024 slots by guest header). A guest LockRect drops the cached copy.
  - Not mirrored, counted per kind on a `[D3DB] host draws` line: other render targets (S4), vertex programs (S5), pixel shaders (S4), cube/palette textures, LINELOOP/QUADSTRIP/POLYGON.
  - Translation gaps are counted too. Ops above 11 use the XDK order from memory, **not verified**. xbox_d3d8 reads a texture argument of 0 as "default", so plain `D3DTA_DIFFUSE` is passed as CURRENT on stage 0 and counted as a gap on later stages.
  - Check switches: `RECOMP_D3D_BRIDGE_POINT=1` (point sampling) and `RECOMP_D3D_BRIDGE_DUMP_EVERY=N` (a host dump every Nth swap). `drive.py` has `--point` and `--dump-every=N`.
  - A `[D3DB] host mirror time` line measures the bridge's own cost: under 1% of every 10 s interval.
- **The splash, as the log shows it:**
  - One textured quad per frame: DXT5 512x512, 10 levels; MODULATE(TEXTURE, DIFFUSE); CLAMP; LINEAR; ZFUNC ALWAYS; no blending.
  - Its vertex colour fades `FF000000` → `FFFFFFFF` over swaps 0-14, holds ~3 s while the title loads (no swaps), then fades out over 15-28.
  - Plus an untextured SRCALPHA quad (`50000000`) that multiplies to 0 with no texture, in both renderers.
  - One texture upload per run.
- **Why fbcompare's pairing can't check the splash:** the executor takes its dumps mid-frame, only for the first 8 draws that rasterise and then every 10 s. It also ignores the vertex colour, so it shows the texture at full brightness while the host fades from black (`s3a`: 0-0.4% within 8).
- **Check (`twoc/tools/diag/splashcheck.py`):** each host dump of a splash frame against the executor's texture-only `shot000` × that frame's vertex colour.
  - **With `--point`, 29/29 frames at 99.99% within 8.** So the geometry, texcoords, DXT5 upload, primitive map, blending and fade are exact.
  - With the game's own bilinear filtering, the full-brightness frame is 96.05%, and the differences are only on the logo's edges. The guest asks for LINEAR, so the default stays LINEAR: the executor is the approximation.
- **The real window** (`screengrab.py`, step 31): black at 0.4 s, grey 144 at 1.1 s, the logo on white from 1.9 to 3.3 s, black at 5.1 s. Grabs `s3g_win_001s/002s.png`.
- **Bridge off, identical:** `off34b` = `fixoff1` (231 opens in order, 39 events apart from timestamps, 94 `[HEAPSTAT]`, exit 3, no `[D3DB]`).
- **Bridge on to the hub (`on34`):**
  - hub music at 163.4 s, exit 3;
  - presents = swaps = 2,050;
  - 6,918 UP draws mirrored, 0 failures, 0 translation gaps;
  - 14,103 not mirrored, all render-to-texture. *(Corrected by 18.35: this line counted DrawVerticesUP only. The title and menu's indexed, immediate and vertex-program draws weren't counted at all; now every path is.)*
- **Frame rate:** identical runs varied 20-30% today (intro 29-39/s and 36-47/s with the bridge on). The mirror's own time is under 1%, so S3-vs-S2 fps differences from single runs aren't meaningful.
- **Contiguous memory (`dd4be13`, `recomp_manual.c`; the 18.28 pattern, twoc-only because the arena's state is internal):**
  - `MmAllocateContiguousMemory` `[0x15AAC8]`/`Ex` `[0x15AB18]` first reuse blocks the game freed: first fit, aligned, split, zeroed. Otherwise they fall back to the bridge's bump arena. Every block's page-rounded extent is recorded.
  - `MmFreeContiguousMemory` `[0x15AAD0]` returns a recorded block to a merged free list. Pinned physical addresses, size 0 and unknown addresses go to the bridge.
  - Reuse stays below the arena's high-water mark, so the executor's `xbox_ContiguousAllocatedBytes()` test still holds.
  - `MmQueryAllocationSize` `[0x15AB1C]` answers a recorded block's size (the bridge said 0). The only caller is DSOUND's allocator `sub_00137B5A` (19 calls at boot). It adds the size to its usage counter `[0x153254]` and zeroes that many bytes.
  - A `[CONTIGSTAT]` line joins `[HEAPSTAT]`.
- **Verified with the executor off** (`drive.py --no-pbexec`, 150 s):
  - 0 `arena exhausted` (331 and an AV at 36-38 s before). The high-water mark stays at 30,504 KiB through 5 menu openings.
  - 5,341 frees returned, 4,743 of 5,472 allocations reused.
  - With the executor on (`offcm`): the same opens and events as `off34b`, with 2,002 frees and 1,837 reuses on the way to the hub.
- **The next S7 limit: a general-heap leak of 1,200 KiB per menu → demo cycle.**
  - The live heap goes 39,773 → 40,973 → 42,173 → … → 49,425 KiB, one more block each cycle.
  - At ~112 s a 1,228,848-byte request (640×480×4 + 48) fails, a null function pointer loops, and the process ends with a stack overflow (`0xC00000FD`).
  - 0 VM frees went to the bridge, so the game never frees the block through `NtFreeVirtualMemory`. It's not the loading thread's stack (reused each cycle). Not traced.
  - *(Corrected by 18.40: it wasn't a game block. Each level load's loader thread got a TIB from the runtime heap that was never freed, and the heap reuses a freed block whole, so the TIB kept a freed 1.2 MB D3D block (or a bigger level block) live. Fixed in `44d29e9`.)*
- **Files changed:**
  - Code commits: `twoc/src/d3d_bridge.c`, `twoc/src/d3d_host.{c,h}` (`498f321`); `twoc/src/recomp_manual.c` (`dd4be13`).
  - Notes commit: `twoc/tools/diag/splashcheck.py` (new), `twoc/tools/diag/{drive.py,README.md}`, the 18.33 pointer, this entry and "Start here tomorrow".

### 18.35 Step 33: Option B S4 part 1. Render-to-texture, pixel shaders and P8 are in and checked, but the title and menu still show black on the real window: every draw that reaches their back buffer is indexed (fixed-function 3D), immediate, or uses a vertex program.

Report: `step33_report.md` in the session scratch folder, with every run in `runs/` and montages in `view/`. Commits: `107ebcf` (S4 part 1), `twoc/` only. No `xboxrecomp/` change.

- **What the title and menu draw** (a new census key, `combo`: path, target, vertex shader, pixel shader, texture formats):
  - The title load renders the six 128x128 A8R8G8B8 targets once, at ~5.3 s. That's 1,164 fixed-function UP draws plus 1,560 UP strips with a pixel shader, and all 1,560 also use a vertex program.
  - After that, everything on the back buffer is one of these:
    - indexed draws with FVF `0x152` (XYZ|NORMAL|DIFFUSE|TEX1): fixed-function 3D with transforms and lighting;
    - indexed draws with a vertex program;
    - immediate-mode QUADLISTs (P8 and render-target textures);
    - a few untextured UP quads that leave the frame black in both renderers.
  - P8 textures appear only in indexed and immediate draws. The render targets are sampled only by vertex-program, immediate and indexed draws.
  - **So none of part 1 reaches the real window during the title or menu**, even though each piece works.
- **Render targets (`d3d_host.c`):**
  - A draw or clear to a texture level gets a host render-target texture for that Data (level 0; the game never renders the others). It comes with a depth buffer of the same size: D3D11 won't pair a render target with a depth view of another size, and `xbox_d3d8` falls back to its 640x480 one.
  - A guest texture with that Data samples the host target.
  - No guest depth surface means depth and stencil off, as the XDK's SetRenderTarget does.
- **Two `xbox_d3d8` gaps found and worked around in `twoc/`:**
  1. Its fixed-function VS maps XYZRHW x/y through the back buffer's size, not the viewport. A 128x128 target got its quads in the top-left fifth. The bridge copies and rescales pre-transformed vertices (`rescale_rhw`); this is identity for the splash.
  2. MIPFILTER NONE still walks the mip chain (MIP_POINT, MaxLOD unbounded). The bridge gives such a stage level 0 alone. The game does use NONE (census: TSS 0.05 and 3.05 switch between 0 and 2).
- **Render-target check (`tools/diag/rtcheck.py`, `--rtlog`):**
  - The executor can't be compared here. It rasterises the swizzled target as linear rows at a wrong pitch; the guest memory is scrambled.
  - Instead, all 1,164 draws and the 2 clears are replayed in software from the log, with the sprite's own guest bytes.
  - Clears: `FF808080`. Draws: 32x32 sprite, MODULATE(texture, diffuse), SRCALPHA/ONE, alpha test GREATER 0.
  - **Game's trilinear filtering: 100% within 8 and 99.99% within 3 on all three targets, largest difference 4.**
  - Point sampling (`--point`): 99.06% and 97.99% within 8. The rest are texel ties: an 8- or 16-pixel quad over 32 texels puts every sample on a boundary.
  - Three of the six targets are only ever drawn by vertex-program draws, so the host never creates them.
- **Pixel shaders (`translate_ps`):**
  - SetPixelShader `0x102BB0` copies the def into `D3D__RenderState[0..56]` (unless device flag 0x10 is set) and PSTextureModes into RS[117].
  - `xbox_d3d8`'s combiner translator (`d3d8_combiners.c`) uses its own field order, so the bridge re-encodes. Differences:
    - inputs A-D in reverse byte order;
    - AB and CD destinations swapped;
    - registers 14/15 (V1R0_SUM, EF_PROD) swapped;
    - SHIFTRIGHT_1 is 5, not 6;
    - its mux picks AB where the hardware picks CD, so a muxing stage has AB and CD exchanged;
    - the final combiner takes the last stage's constants, so RS 43/44 go into one extra stage that writes nothing;
    - no final combiner (ABCD = EFG = 0) outputs r0;
    - 4-bit texture modes where the XDK uses 5-bit ones;
    - SAME_C0/C1 expanded per stage.
  - What it can't express is counted as "pixel-shader gaps":
    - blue reads in the alpha portion;
    - blue-to-alpha;
    - a mux on the LSB;
    - texture modes past CUBEMAP;
    - final constants with 8 stages;
    - V1R0 complement.
- **Pixel-shader check (`tools/diag/pscheck.py`, `--psprobe`):**
  - Each distinct shader is drawn once per input set (4 sets) into a 4x4 host target, read back, and compared with a CPU evaluation of the XDK def by the NV2A combiner rules.
  - **The title's 6 shaders: 24/24 probes exact (largest difference 0).**
  - All six are 1-8 stage image-processing kernels with no final combiner, reading textures as PROJECT2D.
  - One gap: shader 5 uses blue-to-alpha, which `xbox_d3d8` can't do. It's harmless there: stage 7 multiplies that alpha by `c0.a` = 0.
  - None of the 1,560 pixel-shader draws is mirrored yet: all use vertex programs (S5).
- **P8:**
  - The stage's palette is at device `+0xA88`. Its Common holds the size in [31:30] (256 >> n entries) and the type 3 in [18:16]; every one seen is `010B0001` (256 entries).
  - The host expands the indices to A8R8G8B8 through the palette, keeping the swizzle, and the palette's signature is part of the cache key.
  - **Check (`--texcheck`): the 19 P8 textures the title and menu bind, uploaded without drawing, all decode to correct art**: the copyright text with its alpha, two planets, fur and wood.
  - No P8 draw is mirrored yet: all are indexed or immediate.
- **Texture ops above 11: none, splash to hub** (census: COLOROP/ALPHAOP only 1, 4, 7). The 12+ table stays unverified and unused.
- **The real window (`tools/diag/wgcgrab.cpp`, Windows Graphics Capture):**
  - It grabs the bridge window's own content while other windows cover it, so no topmost window and no focus change are needed.
  - This time the old corner grab caught another application on top.
  - Splash: the logo fading in on white (1-3 s). **Title and menu (5.5-30 s): black**, matching the host dumps.
  - One capture frame per guest present (290 by frame counter 289).
- **Not mirrored in the title and menu, per 34 s run (`[D3DB] host draws`):**
  - 14,530 vertex-program draws (S5);
  - 18,083 indexed FVF draws (fixed-function 3D: transforms, lights, material);
  - 2,101 immediate draws (part 2);
  - against 2,056 mirrored (1,164 of them to render targets).
  - **The draw counters now cover every path.** Before, only DrawVerticesUP was counted, so 18.34's "14,103 not mirrored, all by render target" was the UP draws only.
- **Bridge off, identical:** `off35` = `off34b` (231 opens in order, 39 event lines apart from timestamps, 94 `[HEAPSTAT]`, exit 3, no `[D3DB]`).
- **Bridge on, to the hub:** `on35` reaches the hub (music at 161.2 s, exit 3). Presents = swaps = 2,323; 13,861 draws mirrored, 6,226 of them to 5 host render targets (the hub renders to textures every frame); 0 draw, upload or render-target failures. The mirror's own time is 0.3-0.4% of each interval. Both runs were slower than step 32's (hub music 18 s later with the bridge off too): another application was running on the machine.
- **Files changed:**
  - Code commit: `twoc/src/d3d_bridge.c`, `twoc/src/d3d_host.{c,h}`.
  - Notes commit: `twoc/tools/diag/{pscheck.py,rtcheck.py,wgcgrab.cpp,wgcgrab_build.bat}` (new), `twoc/tools/diag/{drive.py,census.py,README.md}`, the 18.32 and 18.34 corrections, this entry and "Start here tomorrow".

### 18.36 Step 34: fixed-function indexed draws (FVF 0x152) are mirrored, and the title, menu and hub reach the real window. What's still missing is immediate mode (the menu's planets and particles) and vertex programs (Crash's model).

Report: `step34_report.md` in the session scratch folder, with every run in `runs/` and montages in `view/`. Commits: `0bf16db` (fixed-function indexed draws), `twoc/` only. No `xboxrecomp/` change.

- **Decision (the user, Sep 25):** fixed-function indexed draws come before immediate mode and S5, since they're the largest share of the title and menu.
- **What's mirrored (`d3d_bridge.c` `mirror_draw_indexed`, `describe_ff`, `setup_draw`; `d3d_host.c` `apply_ff`):**
  - DrawIndexedVertices `(Prim, VertexCount, pIndexData)` with a vertex format:
    - 16-bit indices from the caller's memory;
    - vertices from stream 0 (`0x10F280`: stride +0, offset +4, vertex buffer +8) plus the base vertex (device `+0x1C`), as `0x108F40` (the lifted body's vertex-array setup) computes them;
    - only the index range goes to the host, indices rebased;
    - drawn with `xbox_d3d8`'s DrawIndexedPrimitiveUP.
  - Every draw with untransformed vertices (UP and indexed) carries the fixed-function state:
    - transforms at device `+0x750`, 64 bytes each, XDK order (VIEW 0, PROJECTION 1, TEXTURE0-3 2-5, WORLD 6; SetTransform `0xFEA20`);
    - material at `+0x9F0` (17 dwords, SetMaterial `0xFEB50`);
    - lights: an array at `+0x390` (count `+0x394`), 0x90 bytes each, the first 26 dwords the D3DLIGHT8. The enabled ones are a list from `+0x398` linked at `+0x8C` (SetLight `0xFF9C0` grows the array; LightEnable `0xFFBA0` links and unlinks).
  - `setup_draw` is the state setup the UP and indexed paths now share.
- **`xbox_d3d8` gaps found, worked around in `twoc/`:**
  1. Its lighting takes every colour from the material. It ignores COLORVERTEX and the material sources, and it skips lighting without normals. A draw whose source is a vertex colour is lit on the CPU in the bridge (`cpu_light`), by D3D8's rules in camera space, and drawn unlit. Specular isn't produced there (counted).
  2. DrawIndexedPrimitiveUP maps FAN/QUADLIST to list topology without converting the indices. `index_list` turns fans, polygons, quads and quad strips into triangle lists, and a line loop into a strip.
  3. DrawIndexedPrimitiveUP ignores MinVertexIndex and uploads from vertex 0, so the bridge rebases.
  4. Texture matrices apply whatever TEXTURETRANSFORMFLAGS says. The host keeps them identity, and TTF is counted (never set, splash to hub).
  5. `fvf_texcoord_size` misreads the texture-coordinate formats (1 → 1 float, not 3; 2 → 2, not 4; 3 → 3, not 1). Counted, never seen.
  6. Normals are always normalized; the CPU path follows NORMALIZENORMALS.
- **Lighting isn't exercised yet.**
  - The census's new `ff` key shows that no fixed-function draw from splash to hub has LIGHTING on: title and menu `lit=0 cv=1 src=0011` (ambient and emissive from the vertex colour), three directional lights enabled, unused.
  - LIGHTING toggles only around vertex-program draws.
  - So both lighting paths are written but untested against the game. The `[D3DB] host fixed function` line counts draws lit by `xbox_d3d8` and on the CPU.
- **Scrape check** (the first draws logged in full):
  - FVF 0x152, stride 36 (the FVF's size);
  - world scale (0.07, 0.07, 0.28) at z = 1, view identity;
  - projection 1.905 / 2.540 / 1.0003 / 1 / -0.30009 (4:3, near ≈ 0.3);
  - a worked vertex lands where the window shows it.
- **Real window (`wgcgrab`; `view/before_after.png`, step 33 above, step 34 below):**
  - 6 s: **the green 3D "LOADING"**;
  - 12-16 s: **the menu's starfield and NEW GAME / LOAD GAME**;
  - 18-30 s: **the name-entry screen** (CRASH___, letter grid, DONE/CANCEL), bevels and pulsing vertex colours included.
  - 8-11 s stays black: a load (title.nux, font, particles, cube maps) with cleared frames, black in the host dumps too.
  - Dense host dumps catch the title animation `-FastTitle` rushes through (11.7-12.1 s): a globe with a ring, a yellow planet, a floating island, "www.T…es.com" (the TT logo glyph missing, kind not identified). *(Corrected by 18.37: nothing is missing. The line is "www.t-tales.com", 15 fixed-function 0x152 glyphs drawn twice, all mirrored; the uneven letter sizes are the game's own per-letter scale wave in their world matrices.)*
  - To the hub (`on36`):
    - 60 s: LOADING on green curtains;
    - 80-120 s: a lab scene with a large monitor showing static;
    - 140-180 s: **the hub**: translucent platform with reflections, stone pillar, bamboo scaffold, the portals' electric effects, the 3D HUD number. Crash isn't there.
- **What's still missing, by kind** (new check `RECOMP_D3D_BRIDGE_SKIPTEX`, `drive.py --skiptex`, `tools/diag/skiptex.py`: every texture a skipped draw binds, once per kind; DXT decoded offline):
  - **Immediate-mode QUADLISTs, FVF `0x142`** (untransformed XYZ|DIFFUSE|TEX1 in world space, not screen quads), 2,209 per 34 s: **the menu's two P8 planets** (blue and orange) and **its particles** (a 256x256 atlas). `describe_ff` already supplies their transforms, so part 2 needs only the Begin/End vertex buffering. *(18.37: mirrored now. The particles show; the planets are camera-facing billboards that stay outside the view from swap 100 to 220, so they don't show in the menu.)*
  - **Vertex-program indexed draws**, 13,654 per 34 s: **Crash's model** (face, fur, mouth, eyes; DXT5), **the menu's props** (wood, a gold plaque, water, stone), and Crash in the hub. S5.
  - Vertex-program UP strips into three of the six title render targets, 1,560. S5.
- **Counts:**
  - Splash to menu (34 s): 18,903 indexed draws mirrored.
  - Splash to hub (`on36`, 190 s, hub music 124.2 s, exit 3): 280,622 draws mirrored, 264,498 of them indexed, **0 host draw or upload failures**.
- **Cost:** the mirror's own time went from 0.3-0.4% of each interval to **5-6% in the hub**. Most of it is DrawIndexedPrimitiveUP creating two immutable D3D11 buffers per draw. It needs a ring buffer or a vertex-buffer cache by S7.
- **Bridge off, identical:** `off36` = `off35` (231 opens in order, 39 event lines apart from timestamps and paths, 94 `[HEAPSTAT]`, exit 3, no `[D3DB]`).
- **Files changed:**
  - Code commit: `twoc/src/d3d_bridge.c`, `twoc/src/d3d_host.{c,h}`.
  - Notes commit: `twoc/tools/diag/skiptex.py` (new), `twoc/tools/diag/{drive.py,README.md}`, this entry and "Start here tomorrow".

### 18.37 Step 35: Option B S4 part 2. Immediate mode and CopyRects are mirrored: the menu's particles and the hub's portal glows show. The title's "www.t-tales.com" is complete; its uneven letters are the game's own animation.

Report: `step35_report.md` in the session scratch folder, with every run in `runs/` and montages in `view/`. Commits: `ae718cb` (immediate mode, CopyRects, draw log), `twoc/` only. No `xboxrecomp/` change. The machine may have been used for gaming during the runs, so every fps and timing number here is rough.

- **Immediate mode (`d3d_bridge.c` `d3db_vertex_data`, `imm_begin`, `imm_vertices`, `mirror_draw_imm`):**
  - The lifted bodies:
    - SetVertexData4f `0x101E60` pushes NV097_SET_VERTEX4F for register -1 and SET_VERTEX_DATA4F_M + 16·reg otherwise.
    - SetVertexData2f `0x101E20` pushes SET_VERTEX_DATA2F_M + 8·reg, which the NV2A takes as (a, b, 0, 1).
    - Begin `0x101EC0` flushes lazy state (`0x108EC0`) and pushes SET_BEGIN_END.
  - The XDK registers are the NV2A attribute slots: POSITION 0, NORMAL 2, DIFFUSE 3, SPECULAR 4, POINTSIZE 6, TEXCOORD0-3 9-12. VERTEX (-1) launches a vertex.
  - The game's only caller is `0x38300`, through its wrappers `0x3AE20` Begin, `0x3AE30` End, `0x3AE40` 4f and `0x3AE60` 2f. It sets DIFFUSE as (R, G, B, A) floats unpacked from an ARGB colour (checked in the disassembly at `0x38423`-`0x3849B`), and TEXCOORD0 as (u, v).
  - The wrappers for 2f/4f use a new `D3DB_DATA_CALL`:
    - with the bridge on, `d3db_vertex_data` only stores: an attribute into a current-value table, or, on a launch, the position and the attributes the FVF uses (read at Begin) into a buffer;
    - the full enter/leave pair runs for the census alone.
  - End builds the FVF's vertices:
    - diffuse and specular packed back to ARGB;
    - texture coordinates sized by the FVF;
    - pre-transformed ones as they are, which `rescale_rhw` handles on the host.
  - End then draws them through `setup_draw` and `d3dh_draw_up`, with the fixed-function state from `describe_ff`, like UP draws.
  - QUADLIST goes to `xbox_d3d8`'s own QUADLIST (its UP path converts it). QUADSTRIP, POLYGON and LINELOOP are gathered through `index_list`.
  - Vertex-program immediates are counted under "vertex program" (S5).
  - Attributes keep their values across vertices and draws, as the NV2A's do.
  - A new gap counts XYZ vertices with w ≠ 1 (never seen).
- **What it shows:**
  - **The menu's particles** (the 256x256 atlas): a pair of green-yellow glows beside NEW GAME / LOAD GAME and the letter grid (`view/a_grabs.png`, `view/particles_zoom.png`).
  - The hub gains red and yellow glows around the portals.
  - **The menu's two planets (P8, blue and orange) are drawn but never on screen:**
    - Each texture's draw is four camera-facing billboards, 24.6 units square, whose normal equals the view's z axis, placed around the camera at radius ~150-190.
    - Projected with the view and projection the menu letters use (same matrices, same draw), none lands inside the view from swap 100 to 220 (`planets.py` in the scratch folder, over a draw log). The game stops drawing them after that.
    - They are also drawn during the first LOADING screen.
  - The title animation now also shows the copyright text, the ringed Earth with its planets, and the 3D "UNIVERSAL INTERACTIVE STUDIOS" (`view/a_dumps.png`, swaps 55-100).
- **CopyRects (`mirror_copy_rects`, `d3dh_copy_back`):**
  - Every call from splash to hub copies the whole back buffer (rects NULL) into a 640x480 linear A8R8G8B8 (`0x12`) texture. The copies begin at the intro's green-curtain LOADING screen (~60 s) and continue in the hub. Data is `008B6000`, or `02A86000` in earlier runs.
  - The host copies its back buffer into a host render-target texture for that Data, which a draw binding it then samples. The copy is kept as R8G8B8A8 (XDK `0x3F`): D3D11 copies only within a format family, and `xbox_d3d8` makes `0x12` B8G8R8A8, while its back buffer is R8G8B8A8. Both sample the same channels.
  - **Every draw that samples the copy is a vertex-program draw.** Nothing visible uses it until S5.
  - Open for then: the NV2A addresses linear textures in texels, not 0-1, and `xbox_d3d8` doesn't. The draw log's uv range will show what the S5 draws pass.
- **"www.t-tales.com" (the title animation, `view/url_t_22_25.png`):**
  - It's the 3D font LOADING uses: FVF `0x152` indexed strips, untextured, one draw per glyph, two passes (15 + 15 draws), all mirrored.
  - The 15 glyphs are w, w, w, ., t, -, t, a, l, e, s, ., c, o, m, so nothing is missing. 18.36's "TT logo glyph missing" read "www.T…es.com" off a mid-animation frame.
  - The uneven sizes come from the game's world matrices, which the mirror reads per draw. At swap 76 every glyph is scaled 0.0241. At swap 77, "t-tales" is scaled 0.0293, 0.0351, 0.0261, 0.0174, 0.0261, 0.0351, 0.0293 against 0.0234 for the rest. That's a symmetric wave (1.25x, 1.5x, 1.12x, 0.74x, 1.12x, 1.5x, 1.25x), the game's own animation of the name. `-FastTitle` rushes through it.
  - The only other draws in those frames that the mirror skips are vertex-program draws, and they bind Crash's model textures (face, fur, eyes) and props, not a logo.
- **New check, `RECOMP_D3D_BRIDGE_DRAWLOG=A-B`** (`drive.py --drawlog=A-B`):
  - Every draw of guest swaps A to B gets one `[D3DB] drawlog swap S #N` line:
    - path, primitive, FVF, mirrored or why not;
    - texture 0 and target;
    - the screen box its untransformed vertices land in (through world, view, projection and viewport);
    - the texture-coordinate range;
    - the world matrix's diagonal and translation, the view's signature and the projection.
  - Draws of 16 vertices or fewer also print every vertex and the view.
  - With `--dump`, the host dumps each of those swaps.
  - The first 10 mirrored draws that sample a CopyRects copy are logged the same way, outside the range too.
- **Cost:**
  - The first build doubled the mirror's time per draw (A/B, back to back, menu: 22.7 µs against step 34's 11.3). An immediate draw cost ~138 µs, because `describe_texture` hashed the whole 256 KB particle atlas at each of ~100 draws a frame.
  - Fixed by remembering each texture's content signature until the next guest swap. A guest LockRect of the texture drops it, as it does the host cache entry.
  - After the fix: **12.2 µs a draw against step 34's 11.1**, immediate draws ~35 µs, menu mirror time 1.4-1.6% of the interval against 1.2-1.5%.
  - The `[D3DB] host mirror time` line now also shows the immediate draws' share.
- **Also fixed:** the indexed path's index buffer had room for 2n + 6 indices, but a fan or polygon of n indices makes 3(n-2). No drawn fan was that long so far; it's now 3n + 6.
- **Counts, splash to hub** (`on37f`, 190 s): hub music at 121.6 s, exit 3. **338,292 draws mirrored**: 283,824 indexed, 8,242 to render targets, **37,735 immediate** (all FVF `0x142`; none pre-transformed from splash to hub). **0 host draw, upload or copy failures.** CopyRects 3,300 (0 failures); 8,025 vertex-program draws sample the copy. Not mirrored: 177,417 vertex-program draws and nothing else. Mirror time in the hub: 9.9-10.7 µs a draw, 2.6-2.9% of the interval. That's against step 34's 20-32 µs and 5-6% (`on36`): the per-frame signatures help every draw that samples a large texture. Those two runs weren't back to back, so read it as rough.
- **Bridge off, identical:** `off37` = `off36` (`compare_runs.py`): the same 231 opens in order, 39 event lines that differ only in timestamps and paths, 94 `[HEAPSTAT]`, exit 3, no `[D3DB]`. The logs differ in length (12,392 against 11,853 lines) only in the timing-dependent executor lines (`[GPU] draw`, `[TEXUSE]`, `[KERNEL]` counts).
- **Files changed:**
  - Code commit: `twoc/src/d3d_bridge.{c,h}`, `twoc/src/d3d_host.{c,h}`, `twoc/src/recomp_manual.c`.
  - Notes commit: `twoc/tools/diag/{drive.py,README.md}`, the 18.36 corrections, this entry and "Start here tomorrow".

### 18.38 Step 36: Option B S5 part 1. Vertex programs are mirrored through a fixed xbox_d3d8 translator: Crash's model and the menu's logo prop reach the real window, and a CPU evaluation of the game's programs matches the host pixel for pixel.

Report: `step36_report.md` in the session scratch folder, with every run in `runs/` and montages in `view/`. Commits: `a0b306d` (`xboxrecomp/`, local only: the translator), `7af9725` (the menu, committed as soon as it worked) and a follow-up code commit, `twoc/`. The machine may have been used for gaming during the runs, so every fps and timing number here is rough.

- **The program and its data, from D3D's own state:**
  - The vertex-shader object (device `+0x380`, handle = object | 1) keeps its upload at `+0x114` as push-buffer: SET_TRANSFORM_PROGRAM runs of up to 32 dwords (`0x102130`), then the declaration's constants (`0x102340`). The microcode is 4 dwords an instruction, as the NV2A takes it. Flags `+0x04` (0x10 has a program), instruction count `+0x08`, upload dwords `+0x0C`.
  - The declaration (parsed by `0x1021A0` in CreateVertexShader `0x102440`) is 16 slots at `+0x14 + 16 r`: stream, byte offset, NV2A format (type bits 0-3, size bits 4-7; `0x02` disabled). The byte at `+0x20 + 16 r` marks tessellator registers.
  - Vertex fetch as the draw's array setup (`0x108F40`): hardware attribute i reads slot `map[i]` (byte table `0x1B11C8 + (flags & 0x10)`). The address is the vertex buffer's Data + the stream offset + the slot offset + base vertex × stride, and the format word is `(stride << 8) | type`.
  - SetVertexShader (`0x102940`) loads the program at address 0 (`0x102610`) and starts there (`0x102670`). Loading copies the program into a device shadow at `+0x1798` and the declaration constants into the constant shadow at `+0xB98` (`0x102080`).
  - SetVertexShaderConstant (`0x102AA0`) writes that shadow at slot reg + 96. It's never skipped from splash to hub (census: device flag 0x10 always clear), so `+0xB98` is the NV2A's whole constant memory by the microcode's own index.
  - **c58/c59 aren't in it.** Every XDK program ends with `MUL oPos.xyz, oPos, c[58] : RCC R1.x, oPos.w; MAD oPos.xyz, oPos, R1.x, c[59]`.
    - c58/c59 are the NV2A's SET_VIEWPORT_SCALE/OFFSET, pushed for programs by `0xFE2B0`.
    - The bridge computes them the same way (`vp_constants`): ox = X·ssx + `+0x9E8`, oy = Y·ssy + `+0x9EC` (both − 0.5 with device flag 0x8000 and `[0x10F02C]`), sx = W·ssx·0.5, sy = H·ssy·`[0x15F278]`; scale (sx, sy, (MaxZ − MinZ)·zs, 0), offset (ox + sx, oy − sy, MinZ·zs, 0).
    - The inputs: viewport `+0x9D0`, super-sample scale `+0x458/+0x45C`, z scale `+0x450`, 0.5 at `0x15AF60`.
- **The mirror (`mirror_draw_vp`, UP and indexed paths):**
  - the constants are the shadow plus c58/c59;
  - every register the program reads is fetched from its stream and expanded to a float4 as the NV2A does (`vs_fetch`: D3DCOLOR, UB_OGL, normalized and plain shorts, float 1-4, FLOAT2H, 11:11:10), in ascending order in one host stream. That's the two-stream interleave from 18.32.
  - Undeclared registers read what SetVertexShader leaves: diffuse white, specular 0, the rest (0, 0, 0, 1), counted.
  - The host makes one shader per microcode hash (`d3d8_vsh_create_shader`; xbox_d3d8's vtable has no CreateVertexShader). It uploads only the constant registers that changed, at the draw. The wrapper itself never calls the host (18.32's call-volume note).
- **Seen splash to hub:**
  - 8 programs: a screen-space 4-tap filter for the title's render targets; lit and sphere-mapped; 3-bone skinning with a morph register; 3 directional lights; skinning with ten float4 bone rows per vertex from stream 1 (stride 160, Crash in the hub); three smaller ones.
  - Formats: only FLOAT1-4 and D3DCOLOR. Streams: 0 and 1.
  - Program immediates: none.
- **xbox_d3d8's translator was wrong, fixed locally under the 18.32 rule** (`a0b306d`, `d3d8_vsh.{c,h}`; it had never run on real microcode: the smoke test stubs `d3d8_vsh_prepare_draw`):
  1. Field layout: not the NV2A's, with overlapping fields. Now xemu's: dword 0 unused; the ops, indices and source fields in dwords 1-2; the destinations, output mux, A0X and final bit in dword 3. A paired ILU op writes R1.
  2. MAC and ILU read before either writes.
  3. The VS output struct didn't link with the pixel shaders (FOG/PSIZE where the PS has TEXCOORD4). It is now `PS_IN` of `d3d8_shaders.c` (fog = saturate(oFog.x), viewpos.z = −oFog.x), and outputs default to (0, 0, 0, 1).
  4. oPos back from screen space: `xyz = (oPos.xyz − c59) / c58 · w`. This removes the XDK's +0.53125 pixel-centre bias, so program and fixed-function draws share a pixel grid.
  5. Every input register is a float4. The old fixed per-register formats (v3 R8G8B8A8, v8-11 float2) can't hold the game's FLOAT2 texture coordinates in v3 or normals in v1, so there was no `twoc/` work-around.
  6. EXP, LOG, LIT and RCC per the NV2A. Constant writes aren't supported (logged, never seen).
- **Check (the pixel-shader probe's counterpart):**
  - `RECOMP_D3D_BRIDGE_VSDUMP` (`drive.py --vsdump`) logs each program's first draw: microcode, declaration, the constants as the host gets them, the viewport, and 8 vertices as fetched. With `--drawlog` it also logs every program draw in the range, as `HASH.N`.
  - `RECOMP_D3D_BRIDGE_VSPROBE` (`--vsprobe`) has the host draw those vertices one by one as points into a sentinel-cleared target and logs the pixel and colour.
  - `tools/diag/vscheck.py` decodes with its own decoder, evaluates on the CPU (float32) and, with `--probe`, compares.
  - **56 of 56 decidable vertices exact over all 8 programs (pixel, and oD0 within 1 per channel), 0 failures.** The 4 undecided are the filter's corners, exactly on pixel edges.
  - The CPU model takes 0 × x = 0 and NaN-ignoring MIN/MAX, which the host's result matches for a prop scaled to 0 (RSQ of a zero normal).
- **Real window (wgcgrab):**
  - Menu: the ring flies in, then the 3D logo prop with the ring, button and plate around NEW GAME/LOAD GAME and name entry, in the xemu reference's layout (`view/menu_vs_xemu.png`).
  - Intro: Crash with Coco, Cortex's hologram.
  - Hub: Crash drops in at the LOAD/SAVE screen and walks, reflected in the floor (`view/hub_vs_xemu.png`, `view/h2_grabs.png`).
  - All six title render targets now hold the game's water ripple height fields and a normal map. Three of them were never created before 18.38.
- **Open:**
  - **The menu's vortex is dark with a bright rim; xemu's is an opaque blue whirlpool with ripple rings.**
    - The draw: program `8E116565`, P8 128x128 `01FEF8B0`, SRCALPHA/INVSRCALPHA, no z write, MODULATE(texture, diffuse) on colour and alpha, WRAP, v over [−1, 0], oD0 white.
    - Its texture's alpha is 0 except a gradient over the top ~10% of rows, and its palette is static. So our picture follows from the data as read.
    - The texture coordinates or another draw differ. Not resolved.
  - xemu's menu background has bluer nebula streaks.
  - In the hub, Crash's blob shadow and the monitor tubes' glow are missing (probably copy-sampling or cube-map draws).
- **Counts, splash to hub** (`h2`, 190 s, hub music 124.0 s):
  - 480,637 draws mirrored, 162,741 of them vertex programs, **0 host draw failures**, 8 shaders, 0 compile failures.
  - Not mirrored: **9,479 program draws that sample the CopyRects copy** (part 2; `RECOMP_D3D_BRIDGE_VP_COPY=1` mirrors them anyway) and **6,415 draws with a DXT5 cube texture** (`0x07780F2D`; the host has no cube upload yet), reached now that programs pass.
  - Gaps: one declared morph register on an unbound stream 1 (`8D225D6D` v6, as D3D itself skips it).
- **Cost:** ~16 µs a draw and ~6% of wall time in the hub (step 35: ~10 µs, ~2.8%): the CPU fetch of every vertex per draw, plus xbox_d3d8's per-draw buffers. S7 wants a vertex cache.
- **Bridge off, identical:** `off38` and `off39` = `off37` (231 opens in order, 39 event lines apart from timestamps and paths, 94 `[HEAPSTAT]`, exit 3, no `[D3DB]`).
- **Files changed:**
  - `xboxrecomp/`: `src/d3d/d3d8_vsh.{c,h}` (`a0b306d`, local only).
  - Code commits: `twoc/src/d3d_bridge.c`, `twoc/src/d3d_host.{c,h}`.
  - Notes commit: `twoc/tools/diag/vscheck.py` (new), `twoc/tools/diag/{drive.py,README.md}`, this entry and "Start here tomorrow".

### 18.39 Step 37: Option B S5 part 2. Cube maps upload, and program draws that sample the CopyRects copy are mirrored with their texel coordinates scaled: the hub's monitor tubes show. `run_twoc.ps1 -Bridge` runs the mirror.

Report: `step37_report.md` in the session scratch folder, with every run in `runs/` and montages in `view/`. Commits: `24e019f` (cube maps and `-Bridge`), `718cf64` (texel addressing), both `twoc/`. No `xboxrecomp/` change. Fps and timing numbers are rough, as before.

- **`run_twoc.ps1 -Bridge`** sets `RECOMP_D3D_BRIDGE=1` and clears it otherwise. The native D3D and the executor still run, so the mirror opens as a second window, "Crash TWOC - D3D bridge (host)". Example: `... -File twoc\run_twoc.ps1 -Bridge -Seconds 900`.
- **Cube maps (`24e019f`):**
  - `describe_texture` takes a swizzled or DXT cube (Format bit 2): six faces follow one another, each a full mip chain padded to 128 bytes (xemu's `NV2A_CUBEMAP_FACE_ALIGNMENT`). The hub's `0x07780F2D` is DXT5, 128 edge, 8 levels: 21,872 bytes a face, stride 21,888.
  - The host makes an `xbox_d3d8` cube texture in the same cache and uploads each face's levels (`tex_upload`). `xbox_d3d8`'s fixed-function and combiner pixel shaders already declare a `TextureCube` for a bound cube and sample it with xyz.
  - Check: `--texcheck` writes each DXT cube map's six faces raw at that stride and `skiptex.py` decodes them. All six are clean images of a lobby (the floor face is the radiation symbol): `view/cube_faces.png`.
  - Splash to hub: **6,657 stage binds of the cube, 0 texture skips (was 6,415), 1 cube upload, 0 failures.**
  - **Every cube draw is one kind**, logged once per kind (shaders, stage, texture modes): the water program `1C743958` with a pixel shader whose texture modes are `0x00064621`, which is PROJECT2D, DOTPRODUCT, DOTPRODUCT, DOT_RFLCT_SPEC. That's a bump-mapped environment reflection (texm3x3vspec, with the cube on stage 3).
  - **So the reflection itself still doesn't show:** `xbox_d3d8`'s combiner translator has no dot-product texture modes (`xps_texmode` counts them as "texture mode" gaps and gives the stage NONE). The draws now mirror; their stages 1-3 sample nothing.
- **Texel addressing for linear textures (`718cf64`):**
  - The NV2A addresses a linear texture in texels; `xbox_d3d8` samples everything over 0..1.
  - Two hub programs sample the 640x480 CopyRects copy on stage 0 (PROJECT2D): `C6C8F337`, the water refraction, with `MAD oT0.xy, R4.xyyy, c[111].xyyy, oPos.xyyy`, i.e. the pixel under the vertex plus a ripple; and `E3399685`. (`--vsdump --drawlog`, `vscheck.py --eval`: oT0 ≈ (560, 446) for a vertex at (560, 445).)
  - `texel_patch` (in `twoc/`, under the 18.32 rule, no `xboxrecomp/` change) edits the microcode of a program draw with a linear texture on stage n. Each instruction that writes oTn.xy also writes a temp the program never touches. The MAC takes it if it writes no temp itself, or the ILU if it runs alone. One instruction is appended: `MUL oTn.xy, Rk, c[K]`, with c[K] = (1/w, 1/h, 1, 1) in a constant the program doesn't read (and not within 64 above a relative read). The patched program has its own hash, so its own host shader.
  - A program that can't be patched still skips a copy-sampling draw, counted (`RECOMP_D3D_BRIDGE_VP_COPY=1` mirrors it anyway). None from splash to hub.
  - Check: a Python port of the patch, run through `vscheck.py`'s decoder and VM on the dumped `C6C8F337`, gives exactly the original oT0 / (640, 480) with oPos unchanged (`texel_check.py` in the scratch folder).
  - Splash to hub: **8,099 draws scaled, 0 unpatchable, 0 copy-sampling draws skipped (was 9,479).** Both programs use temp R11 and constant 191.
  - The oT0 of `C6C8F337` includes the XDK's +0.53125 pixel-centre bias that the translator takes out of oPos (18.38, item 4), so it samples 0.03 texel off the pixel centre. Harmless.
- **Real window (wgcgrab, `view/c1_vs_c4.png`, `view/c1_hub.png`):** **the blue monitor tubes around the LOAD/SAVE screen now show, as in xemu's `hub_spawn.png`.** The hub is otherwise as in 18.38.
- **Still open:**
  - **Crash's blob shadow:** xemu draws a dark blob under his feet; we don't. It's not a copy-sampling or cube draw (both mirror now), nor a fixed-function draw near his feet in swap 1500's drawlog. Candidates:
    - a program or UP draw (the drawlog gives no screen box for program draws);
    - a depth-bias decal: `XRS_ZBIAS` isn't translated, and `xbox_d3d8` has no depth bias at all.
    - Next: print ZBIAS and the solid-offset states in the drawlog, and a box for program draws from the CPU-fetched positions.
  - **The water's reflection:** dot-product texture modes (DOTPRODUCT, DOT_RFLCT_SPEC; texm3x3vspec) in `xbox_d3d8`'s combiner translator. That's the first `xbox_d3d8` change the mirror may need under the 18.32 rule, since there's no `twoc/` work-around for a pixel-shader texture mode.
  - **Floor reflections of Crash** show through the walkway in the hub (18.38's `h2_grabs`, and now). xemu's spawn picture shows none there, but he stands elsewhere in it. Not compared like for like.
  - **Menu vortex** (18.38): not looked at this session.
- **Counts, splash to hub** (`c4`, 190 s): 488,237 draws mirrored, 172,511 of them vertex programs, **0 host draw failures**, 0 draws not mirrored on any path, 8 host shaders (the two patched programs replace their originals), 0 compile failures. Textures uploaded 288 (P8 73, cube 1, fails 0). Pixel-shader "texture mode" gaps: 2,870 by 175 s (`c2`, before the texel patch), all dot-product modes.
- **Bridge off, identical:** `off40` = `off39` (231 opens in order, 39 event lines apart from timestamps and paths, 94 `[HEAPSTAT]`, exit 3, no `[D3DB]`).
- **Tooling:**
  - numpy wasn't installed for the user's Python 3.13 any more (`vscheck.py` needs it); `pip install --user numpy` put it back.
  - The drawlog's second line now has the pixel shader's texture modes and all four stage textures.
- **Files changed:**
  - Code commits: `twoc/src/d3d_bridge.c`, `twoc/src/d3d_host.{c,h}`, `twoc/run_twoc.ps1`.
  - Notes commit: `twoc/tools/diag/{drive.py,README.md}`, this entry and "Start here tomorrow".

### 18.40 Step 38: S7 part 1. With the executor off, the mirror alone runs splash to hub at 60 fps (`run_twoc.ps1 -HostOnly`), and the heap no longer leaks. The 18.34 leak was worker-thread TIBs, and the runtime heap was racing at every thread start.

Report: `step38_report.md` in the session scratch folder, with every run in `runs/`. Commits: `1ee8de2` (limiter and `-HostOnly`), `44d29e9` (heap guard and TIBs), both `twoc/`. No `xboxrecomp/` change. Fps numbers are rough, as before.

- **`run_twoc.ps1 -HostOnly`** implies `-Bridge` and leaves `RECOMP_PB_EXEC` unset: the fence mirror completes the push buffers and the bridge is the only renderer. Executor dumps (`shotNNN.bmp`) stop; host dumps (`hostNNN.bmp`) still come with `-DumpDir`. `drive.py --host-only` is the same.
- **The limiter (`1ee8de2`, `d3d_host.c` `pace`):**
  - With the executor off, each host present waits for its slot on a 59.94 Hz clock, divided by the game's XDK present interval (it asks for ONE; IMMEDIATE would turn it off).
  - It sleeps on a high-resolution waitable timer to ~1 ms before the slot, then spins. DXGI interval 0, so the 239 Hz monitor doesn't pace anything.
  - A present more than one period late restarts the clock rather than catching up.
  - Off while the executor runs, as before. `RECOMP_D3D_BRIDGE_HZ` overrides the rate (0 turns it off); `RECOMP_D3D_BRIDGE_INTERVAL` still overrides the DXGI interval.
  - A `[D3DB] host limiter` line every 10 s: frames paced, frames late, time waited.
  - Measured (`h2`, splash to hub): **menu 60.0/s, hub 59.9-60.0/s** (was 240 and ~125). In the hub it waits 8.2 ms a frame, so the mirror and the game take ~8.5 ms. 43 late presents, all at loads.
- **The menu idle timeout is at normal speed.** `sub_0006E130` starts the next attract demo when its idle counter reaches `0x708` (1800 updates, 30 s at 60 Hz). The rotation table is `0x19A748` (four entries: `snow_m`, `garden`, `jungle_a`, `fire_fly`), the current index is `0x23B74C`, and the level goes to `0x19C06C`. Host-only: the demo loads 30.7 s after the menu opens. The counter counts updates, not vblanks, which is why the unpaced menu of 18.32 (~180 updates/s) timed out long before autostart's press.
- **The 18.34 leak was worker TIBs** (see the correction there). `bridge_thread_main` gives every worker a TIB from the runtime heap and never frees it; only the stack goes back.
  - The heap reuses a freed block whole (first fit, no split). So a loader thread's ~0.1 KiB TIB kept whichever big block it landed in: D3D's freed 1,228,848-byte block (hence "640×480×4"), a 2.6 MB or a 1.1 MB level block.
  - The VM blocks themselves never grew: a tap of every `NtAllocateVirtualMemory`/`NtFreeVirtualMemory` block showed the same 7 blocks, 22.7 MB, at every menu opening. The growth was 2 blocks per menu -> demo cycle, one TIB for each of the two loader threads.
- **The heap was also racing.** The runtime heap (`xbox_HeapAlloc`/`xbox_HeapFree`: a block table and a bump pointer) has no lock.
  - The game's own locks keep its heap calls apart: 0 overlaps in ~4,000 calls of ordinals 14, 15, 17, 65, 184 and 199 over three demo cycles.
  - The exception is thread start. The TIB is allocated on the new host thread while the creator carries on. At the first level load the loader's TIB and the main thread's next level block (2,709,152 bytes) got the same bump address, `0x01B50000`.
  - PsTerminateSystemThread also frees the stack while other threads allocate.
- **The fix (`44d29e9`, `recomp_manual.c`, at the kernel-call boundary; the runtime's thread code is internal):**
  - A guest-heap guard, a Win32 mutex, around every guest kernel call that touches that heap and around the hand-written NtAllocate/NtFree paths. The calls are ordinals 14 and 15 (pool), 17, 65 (IoCreateDevice), 184, 199, 308, 171 (bridged frees), 255 and 258. It's a mutex because a worker's PsTerminateSystemThread never returns: its ExitThread abandons the guard, and the next waiter takes it. It is taken at depth 1, where no thread parks.
  - PsCreateSystemThreadEx holds the guard until the new thread has its TIB. The signal is the heap's live-block count growing by the stack and the TIB, or the thread's first kernel call, with a 2 s timeout that never fired. The first call is exempt: in INLINE mode it runs the game and never returns. So is `RECOMP_WORKERS=inline`.
  - PsTerminateSystemThread on a worker (a TIB other than `0x4000`) frees the TIB and logs `[THREAD] worker T ends: TIB ... freed`. Nothing reads the guest TIB after that.
  - Cooperative-suspend records are reused. The table of 64 never gave a slot back, and every level load starts a thread, so an attract loop would have filled it after ~30 cycles.
  - `[HEAPSTAT]` adds `threads: TIBs freed, spawn waits (timeouts, longest), guard abandoned`.
- **Verified (host-only, `cycle.py`, new):**
  - Before (`cy1`): menu-open heap live 39,773 → 40,973 → 44,046 → 49,425 KiB, then the 5th demo load failed its 1,228,848-byte D3D request and the process died of a stack overflow at 293 s, as in 18.34.
  - **After (`cy5`, 420 s): 39,773 KiB at every menu opening through 7 demo loads (14 level loads), no crash.** 13 TIBs freed.
- **The crash when a demo ends was the same race.**
  - Before the fix, the first demo that played to its end crashed on the way back to logos: in the lifted ApplyStateBlock (`sub_00101705_gen`, from `sub_0003A7F0`), reading a state block full of floats (guest `0xFBDFE268`).
  - It happened with the executor on too (`ex1`, bridge off, 473 s, the same address), so it was never an executor-off problem.
  - After (`nat1`, 400 s, no input): 4 demo loads, 3 natural demo ends back to the menu, no crash, heap flat.
- **Still open: the first demo crashes on the normal title path, executor on or off.**
  - Without `-FastTitle` (the title at its own speed), or with the limiter off, the first attract demo (`snow_m`) crashes as it starts, just after `snow.pad` and `ARCTICAL.wav` open.
  - The read is at guest `0xFFFFFFFC` in `sub_00036E90` (`0x36EDD`, `mov eax,[edx+eax*4]` with `edx` 0 and `eax` -1). The call is at `0x250F0` in `sub_00024EF0`. The player's current animation id picks a slot in the character model's table (`[model + id*4 + 0x3B4]`, 118 slots), and that animation has 0 frames: `sub_00090BE0` clamps the frame index to count - 1 = -1.
  - The same with the bridge off and the executor off (`nb2`), and with the executor on (`ex2`, 295 s). **So it isn't executor-off or bridge work.** It is timing-dependent: `-FastTitle` with the 60 Hz limiter got through the first demo in every run (10: `t1`, `tap1`-`tap3`, `cy1`-`cy5`, `nat1`).
  - Before the heap fix, the same runs hung at 1.4 swaps/s in the demo load, with wild ICALLs (`0x90909090`, `0x00105D95`, `0x8B098B02`).
  - `snow.anm` isn't on the disc (not found in every run, good or bad), and `snowchars.dat` is never opened.
  - `sub_00024EF0` also draws the menu's character model (flag `0x1BA68C`, frame counter `0x562CE0` mod 60). The player object is `0x8F5120`, as in 18.30.
  - Next: dump the player's animation id and the model pointer at the demo's first update, in a good run and a bad one.
- **Nothing else broke with the executor off.**
  - Fences, GET/PUT and vblank: no wait or hang anywhere from splash to hub, or over 14 level loads. The fence mirror (18.1) and the ack thread cover the D3D waits.
  - The vblank count still advances at ~32/s, not 60 (noted since step 26). The runtime's timer thread ticks every 16 ms on a 15.6 ms clock, so every other tick is skipped, and fixing that is a runtime behaviour change. At 60 fps it costs nothing: the main loop's catch-up count (`0x23C498`, vblanks since the last frame minus one, computed at `0x896DE`) stays 0. The splash's 120-vblank wait takes ~3.75 s instead of 2.
  - The hub's ~5 s stalls (60 → 30/s in three 10 s windows) are `KeQuerySystemTime` spins at its three voice lines (USCOR45, USCRN01, USCOR46). They happen with the executor on too (`off41`, the same ~56 M calls per 2 s): the DSOUND voice-off wait of 18.11.
  - Launched through `run_twoc.ps1`, the first swap comes at 2.9 s and the menu at 12.7 s. Through `drive.py` they come at 1.0 s and 5.8 s. The same with `-Bridge` (executor on), so it predates this step; not investigated. *(Explained in 18.47: `Start-Process` opens the logs write-through, and unbuffered stderr is written a byte per WriteFile. Since `1c81230` the first swap comes at 1.0 s through `run_twoc.ps1` too.)*
- **Mirror gaps the attract demos show** (not S7):
  - `snow_m`: `D3D8: CreateTexture2D failed 0x80070057 (fmt=0 256x32768)` about 60 times a second, and 3,324 host render-target failures by 106 s. A render target the bridge describes wrongly.
  - An `xbox_d3d8` combiner that doesn't compile (`undeclared identifier 'r_ef'`).
- **Bridge off, identical:** `off41` = step 37's `off40`: 231 opens in order, 39 event lines apart from timestamps and paths, 94 `[HEAPSTAT]`, exit 3, no `[D3DB]`.
- **Tooling:**
  - `drive.py` adds `--host-only`, `--no-autostart`, `--no-fast-title` and `--hz=HZ`.
  - `cycle.py` (new) loops menu → demo → menu with Start+A, or lets the demos end (`--natural`).
  - Temporary taps, not committed: a VM-block tap (every new block and release with guest return addresses) and a heap-call overlap counter.
  - `thunks.py` in the scratch folder lists the XBE's thunk slots by ordinal.
  - Symbolizing again needed `/MAP` plus the module base; the crash handler's native-stack filter assumes base `0x140000000`, and under ASLR it prints nothing.
- **Files changed:**
  - Code commits: `twoc/src/d3d_host.{c,h}`, `twoc/src/d3d_bridge.c`, `twoc/run_twoc.ps1` (`1ee8de2`); `twoc/src/recomp_manual.c` (`44d29e9`).
  - Notes commit: `twoc/tools/diag/cycle.py` (new), `twoc/tools/diag/{drive.py,README.md}`, the 18.34 correction, this entry and "Start here tomorrow".

### 18.41 Step 39: the first-demo crash was our own hand-written `sub_000EB270` leaving the guest stack 4 bytes low. Fixed. Hub → earth warp room → Tornado Alley and Arctic Antics load and play host-only.

Report: `step39_report.md` in the session scratch folder, with every run in `runs/`. Commit: `3648de8` (`twoc/src/recomp_manual.c`). No `xboxrecomp/` change. The session ended when Windows Terminal crashed (below), so item 3 (the voice-off waits) was only started.

- **The chain, end to end (item 1):**
  - The hand-written XPP port gate `sub_000EB270` (18.16) never did the original's `ret`: no `esp += 4`. Every call left the guest stack 4 bytes low. The fix is that one line.
  - Callers with a frame pointer hid it (`mov esp, ebp`). The front-end loop `sub_0002D950` (called from `sub_00087BF0` at `0x87E19`) pops `edi`/`esi`/`ebx` *before* `mov esp, ebp`, so it returned stale stack slots in all three (seen: all three `0x0002DAAB`).
  - `sub_00087BF0` keeps the level-pool size in `esi` (`0x700000`, set at `0x87C27`). At `0x87EA4` it reallocates the pool when the current size `[0x4E242C]` is bigger than `esi`: frees `[0x4F2554]`, allocates `esi` bytes with `sub_0009E300`, but sets the end `[0x4F26C0]` to base + `0x700000` (a constant). With `esi` = ~180 KB the pool got a small block whose end claimed 7 MB.
  - The demo's level data (via the bump loader `sub_00092120`, cursor `[0x4F253C]`) then ran past that block into heap memory the table counted as free. DSOUND's pool helper `sub_00137AFB` (`ExAllocatePoolWithTag`, then zeroes `ExQueryPoolBlockSize` bytes) took a freed 1 MB block for a 44-byte request (first fit, no split, 18.27), zeroed all of it, and wiped the animation at `0x020E75AC` one frame after the game loaded it. `sub_00036E90` then read frame `count - 1 = -1` of a 0-frame animation.
  - "Timing-dependent" (18.40) only because the stale slot's value decides whether the realloc happens (only when it's below the current size). Correction to 18.40: with this session's builds a `-FastTitle` run crashed the same way too (`good1`); it was never tied to the title path.
- **How it was found:** a tap at `0x250EA` showed the animation valid at frame N and zeroed at N+1; a hardware write watchpoint (DR0 on every thread, re-armed every 100 ms, a VEH logging guest and native stacks) caught DSOUND's `rep stosd`; a heap-event log showed the block's history; a second watchpoint on `[0x4F2554]` found the realloc; a `RECOMP_ABI_CHECK` build named `sub_0002D950` and `sub_000EB270`. All temporary, reverted before the commit.
  - The ABI-check build lives in `twoc/build_abi` (gitignored). Configure it from PowerShell: `cmake -S . -B build_abi -G "Visual Studio 17 2022" "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /DRECOMP_ABI_CHECK"`. Git Bash rewrites `/D...` into paths and the compiler check fails. It builds in about a minute.
  - With the fix it reports only the CRT's own convention-breakers: `sub_000F5D38` (`__SEH_prolog`), `sub_000F5D71` (`__SEH_epilog`), `sub_000F45F0` (`_alloca_probe`), `sub_000FB6C0` (`_aulldvrm`, returns in `ebx`), `sub_000F7ACB` (the x87 transcendental dispatcher, `ebx` as a table pointer). Before it, `sub_00036280`, `sub_00086D30`, `sub_000FEF20`, `sub_00039D40` and `sub_000AD520` were reported too, all downstream of the leak.
  - Audit of the other hand-writes in `recomp_manual.c`: every exit pops its return address and `stdcall` arguments. `sub_0014726F`/`sub_0014FF0C` are empty data stubs that never run.
- **Verified:**
  - `fx1`: host-only, title at its own speed, no autostart (the path that crashed every time in 18.40). 240 s: the `snow_m` demo played to its end, the menu came back, `garden` loaded, no crash.
  - `fx2`: the same with `-FastTitle`, 150 s, clean.
  - Bridge off (`offb` = HEAD before the fix, `offf` = after): 231 file opens identical and in order, event lines differ only in timestamps and paths, exit 3 both.
- **Game facts found on the way:**
  - Level pool: base `[0x4F2554]` (and `[0x4EE500]`), cursor `[0x4F253C]`, end `[0x4F26C0]`, size `[0x4E242C]`. 7 MB at boot (`0x87C27`), 10 MB from the menu on (`sub_00086600`).
  - `sub_0009E300` is the game's allocator: an arena first if `[0x36CA7C]` is set (by `sub_0009E2C0(cur_ptr, end_ptr)`, with no bounds check when the end pointer is null), then the arena at `[0x36CA74]`, else CRT `malloc` (`sub_000F4FF4`). `sub_0009E3D0` frees.
  - `sub_0007B050`: animation cache by name (list `[0x23B890]`, refcount at `+0x104`; `.bsa`/`.ani` suffixes), loading through `sub_00092120`. A character model's slots: `+0x3B4` (118 animations), written at `0x23F7D`.
  - DSOUND's pool allocations zero the whole reused heap block, not the request: a 44-byte object zeroed 1 MB. Cheap to live with, but it explains big `memset`s in profiles.
- **Item 2: hub to the first level, host-only (`wk1`, with the fix).** Driven through the input file from scratch helpers (`walk_launch.py`, `pad.py`, `trace.py`, `steer.py`, not committed: `steer.py` estimates the camera's rotation from each step and aims the camera-relative stick at a world XZ target).
  - **Nothing blocks it.** The spawn is the Load/Save pad at (0, 7.85, 1.9): standing on it opens the LOAD/SAVE HARD DISK menu (that is 18.30's "zone that captures the stick"). D-pad down to EXIT, then A, leaves it. The HDD already holds a save "CRASH 0%".
  - The central platform has six wings. Only the earth wing (warp room 1) is open; the others sit behind lightning force fields that throw Crash back (the ice wing's at about (-4.8, -4.25)). The earth wing's walkway starts near (5, 1.5) and reaches the pad ring near (8.5, 4.4).
  - Next to a pad the level's name and a preview show. Standing in pad 2 (about (11.6, 8.4)) for ~3 s starts it. The first pad entry plays a `levels\b\space` cutscene (Uka Uka, `USERTH01.wav`), then loads **Tornado Alley** (`levels\a\farm`, a glider stage, `TORNADOL.wav`). It renders, the glider steers, 60 fps. Pause → WARP ROOM → "Abandon level?" YES returns to the warp room beside the pad.
  - Pad 1 (about (8.6, 9.2)) loads **Arctic Antics** (`levels\a\snow_m`, `ARCTICAL.wav`). Crash ran ~20 units, jumped, spun, broke crates, a seal enemy was in view. No crash.
  - Heap: 96.0% of the bump range used, live 39,773 KiB in Arctic Antics (42,173 KiB in Tornado Alley), largest free block 4,235 KiB. Watch it in bigger levels.
  - The pause menu's top-left panel is solid green (probably the level view behind it, a render target the mirror doesn't show). Not checked.
- **Rhys's play test** (on a build before `3648de8`): 60 fps with micro-stutters, and 3-5 s full stops, which match the voice-off waits (18.11, 18.40). Arctic Antics played normally until the game **crashed or hung when he hit the Nitro Switch Box**. Not reproduced yet: the stack leak corrupted callee-saved registers wherever a frameless caller sat above `sub_00086D30`, so the fixed build needs a retest first.
  - Arctic Antics' crate table (in memory from about `0x9210C0`): 65 records of 48 bytes: {position xyz, 0, `type << 16 | index`, id, AABB min, AABB max}. `snow.crt` (version 4) has 65 records of 58 bytes, the same count. Types: 1×37, 2×7, 3×10, 4×1, 6×7, 7×2, 8×1. Which type is the nitro switch is **not** known.
  - A test teleport (writing player `+0x6C` from outside) to type 4's position (-4.72, 5.75, 49.91) left Crash in empty space, falling forever (there is no death plane there). Don't repeat it; play there instead.
- **Session end, not a game crash:** Windows Terminal crashed at 19:28:03 (`WindowsTerminal.exe`, `Windows.UI.Xaml.dll`, `0xc000027b`, Application log event 1000). The user was playing Arctic Antics with a controller in `wk1`, heading for the nitro switch. The game's log has **no `[CRASH]` line**; its last 10 s interval paced 600 frames (60 fps). It ends at 19:28:12, when the process tree went down. No Application Error for `crash_twoc.exe`. Whether the user reached the switch is unknown.
- **Item 3, started only (no change):** the wait is `sub_0013B682` (18.26): while `[voice+0x12] & 0x8000`, call `sub_0013ADB8`, which (when `& 0x8001 == 0x8001`) reads `KeQuerySystemTime` and, once 50,000,000 × 100 ns (5 s, `0x2FAF080`) have passed since the stop stamp `[voice+0x80]`, calls `sub_0013ACDD` to force the voice off (lock `sub_001395F1`, `inc [0x1531A8]`, `sub_0013F3ED([voice+8]+8, 1)`, `sub_0013AAF6(0)`, vtable `+0x14`, unlock). `KeQuerySystemTime` (ordinal 128) was called 1.65 billion times in the 25-minute `wk1` session.
- **Files changed:** `twoc/src/recomp_manual.c` (`3648de8`); this entry and "Start here tomorrow" (notes commit).

### 18.42 Step 40: the 96% heap was fragmentation, not level data: a pool arena and a VM zone keep it at 84% with 10 MB free through 23 levels. The voice-off stalls are gone (the APU's idle-voice interrupt, delivered). A wrapping kernel-call counter made long sessions crawl.

Report: `step40_report.md` in the session scratch folder, with every run in `runs/`. Commits:
- `e05685c` (heap, twoc);
- `33feacc` (voice-off, twoc);
- `fe75987` in the `xboxrecomp/` clone, local only, not pushed (the counter; 18.29 rule, log volume).

- **Reaching the later levels.** The name WOMBAT gives 25/25 crystals and 106%.
  - Name entry: Start at the menu opens it with the cursor on DONE, up moves to V, and the first letter typed replaces "CRASH".
  - Game tables found:
    - The level table `0x19B138`: `0x58`-byte records, 44 levels, the path string first.
    - The warp-room table `0x19A7F0`: 6 rows of 12 bytes. Pads 1-5, the boss, and the room's path id.
    - The paths `0x19C118`: pad *k* is point `3 + 2(k-1)`, 12-byte xyz.
    - `sub_0005F7E0`: pad index `[0x560F1C]`, triggered `[0x19C074]`, 60 frames, then the next level `[0x19C06C]`; current level `[0x19C068]`.
  - Main-floor gates: rooms 5-4-3-2-1 around the hub (the +z side is the Load/Save console). Room 6 (bonus levels) is upstairs via the centre elevator. The full level list per pad is in the report.
- **What filled the heap (baseline `w1`, 10 levels).**
  - Live was 39,773 KiB in every level, with bump used at 93.5% from the intro on and a 4,235 KiB largest free block (1,200 KiB at some hub reloads).
  - A temporary tap on every heap-touching kernel call (`step40_heap_tap.patch`, `heapacct.py`) found 18.7 MB of that live was slack from the heap's no-split reuse:
    - 841 DSOUND pool blocks (`sub_00137AFB`, `ExAllocatePoolWithTag`) asking 970 KiB held 16.5 MB. An 84-byte block held the freed 7 MB level pool; 24- to 428-byte blocks held 1-2.6 MB level blocks.
    - A 7 MB level pool sat in a freed 10 MB block.
  - Not a leak, and not level data: level data goes to the fixed 10 MB level pool and to contiguous memory (33.4 MB high-water of 64).
- **The fix (`e05685c`, `recomp_manual.c`).**
  - Pool: requests up to 64 KiB come from a 512 KiB arena (split, coalesced). `ExFreePool` returns them, and `ExQueryPoolBlockSize` answers for them. DSOUND zeroes each block to that size and keeps a usage count from it, `[0x153258]`.
  - VM: one zone (heap total less 10 MiB, 39,420 KiB) taken at the first new region. Every `NtAllocateVirtualMemory` new region is cut from it, page-aligned and page-rounded, best fit. `NtFreeVirtualMemory` releases merge with their neighbours. What doesn't fit goes to the bridge.
  - New `[POOLSTAT]` and `[VMSTAT]` lines. Their "heap live less zone free" is the real figure; `[HEAPSTAT]` live now counts the whole zone.
  - Dead end: cutting reused released blocks instead of a zone. The heap's exact block sizes and page padding never merged, and each bigger level buffer took 4 MB more of the bump space until `p2` ran out on its fourth level.
- **Results.**
  - Bump used 41,744 KiB (84.1%), flat through 38 level loads (`p4`, `v3`, `v5`).
  - Zone live 19.6 MB in the hub, peak 25.9 MB during loads (the pool plus the level's file buffers). Largest zone free ≥ 10,176 KiB. 0 regions sent to the heap.
  - Real live 21.4 MB in the hub (was 39.8), 28.2 MB at a load's peak.
  - Loaded with the fix: volcano, castle_a, fireball, droid, weath_r, castle, tsunami, western, space_r, avalanch, space_a, coral_c, west_a, fire_r, jungle_r, seashell, garden, fire_fly, water_r, jungle_a, castle_c, earth_r. Every one peaks the same. **No level needs more than the heap has.**
  - Not visited: the five bosses and room 6.
- **The voice-off stalls (item 3 of 18.41).**
  - The chain:
    - DSOUND polls a stopping voice through `sub_0013ADB8`: from the wait `sub_0013B682`, and from its dispatchers `sub_0013C64D`/`sub_0013D174` when the game asks for status (level setup waits for all voices). It forces the voice off after 5 s.
    - On hardware the APU's `SE2FE_IDLE_VOICE` front-end trap interrupts. DSOUND's ISR runs (KINTERRUPT `0x153260`, `0x13A0FB`, context at `+4`), going through `sub_00139F42` → `sub_00139EC1` → `sub_001399D0(handle)` → `sub_0013AAF6(1)`, which clears 0x8000. It resumes the front end with `sub_0013EE94(0)`/`(1)`.
    - DSOUND sets `FETFORCE1` 0x8000 (the trap is on).
  - The emulated APU traps but never raises `ISTS` (`set_irq` is never read) and delivers no interrupt on this vector. While trapped, its frame thread skips the voice processor.
  - Its `VOICE_OFF` doesn't unlink: the first voice turned off stays at the list head linked to itself, and every later trap names it (0x44) while the released voice (0x45) sits inactive, never reported.
- **The fix (`33feacc`): `sub_0013ADB8` is wrapped.** When a stopping voice is polled:
  - A trap on a real idle voice (DSOUND's voice on that handle stopping, the model's voice block inactive) goes to the ISR, after a 0 write to `ISTS` makes the model raise it.
  - Any other trap is dismissed with DSOUND's own resume.
  - The polled voice, if the model has idled it, is reported with `sub_001399D0(handle)`.
  - The 5 s force-off stays as the fallback. `[VOICEOFF]` lines give the counts and DSOUND's own force-off count `[0x1531A8]`.
  - Dead ends:
    - The wait loop alone (a hand-written `sub_0013B682`) just moved the stall to level setup's status polls.
    - Handing every trap to the ISR crashed in `sub_0013AAF6`: a stale trap turned off the playing voice that had been given the handle.
- **Results.**
  - `v5`, 12 levels: 1,050 voices off at their poll, 0 force-offs, no crash.
  - Host-only `drive.py` A/B: intro and hub pace 599-600 frames per 10 s; the heap-only build had 301-318 in four intervals, the 5 s full stops.
  - `KeQuerySystemTime`: 2.1 billion calls per half hour → under 52 thousand in 40 minutes.
  - Not done: natural-end idles (a voice that finishes without being released) are still dropped, as before. The APU's list bug remains (`xboxrecomp/` behaviour).
- **The kernel-call counter (`fe75987`, local).** `g_kernel_call_count` is an `int`, and `KERNEL_LOG_ON()` is `count <= budget`.
  - Past 2³¹ (the voice-off spins got there in about 30 minutes) it read negative and every kernel call was logged and flushed. `p4` fell to ~7 fps, with 3.2 million such lines.
  - It now saturates; output is identical before that.
  - Possibly what Rhys hit after 25 minutes in 18.41 (1.65 billion calls then); not verified.
- **Bridge off:** `offb` (HEAD) vs `offh` (heap) and `offv` (both). 231 opens identical and in order, event lines differ only in timestamps and paths, exit 3. With item 2 the hub is reached 23.5 s sooner.
- **Files changed:** `twoc/src/recomp_manual.c` (`e05685c`, `33feacc`); `xboxrecomp/src/kernel/kernel_bridge.c` (`fe75987`, local); `twoc/tools/diag/{levelheap.py,README.md}`, this entry and "Start here tomorrow" (notes commit).

### 18.43 Step 41: the bosses ended early because of a lifter gap (a never-taken `je` in the boss-script interpreter), fixed in the lift. Playtest triage: the APU reads the wrong memory (no sound), fog is missing in `xbox_d3d8`'s combiners, the stutter is present pacing, and the model glitch is not timing.

Report: `step41_report.md` in the session scratch folder (triage table, plan, audio scoping), runs in `runs/`. Commit `edfa555` (twoc). Nothing in `xboxrecomp/` changed. Rhys's playtest: `twoc/reference/playtest/` (xemu on Vulkan for the reference shots).

- **The boss fights (Drain Damage = `water_b` 23, the final boss = `space_b` 25).**
  - Reached with a poke: warp-room pad bytes `0x19A7F0` (room 1 pad 1 → level N), then walk onto the pad (scratch `gopad.py`).
  - The level-exit check `sub_0006D470` (per frame, from `sub_0002B2D0`) switches on the level (table `0x6E030`/`0x6E04C`). Bosses 23 and 25 return to the hub 60 frames after `[0x23B854] >= 2`. The script callbacks `0x11930`/`0x11D70` set it to 1 + the room's "beaten" bit (so 2 with a finished save such as WOMBAT).
  - Boss scripts: descriptors at `0x174A10`, 0x1C bytes (script, 16-char name, flags): "atlas crunch", "water crunch" `0x172870`, "space crunch" `0x173460`, "space cortex" `0x1739A0`, and the four "space" elementals. Bytecode: 3 dwords (opcode, arg, float). Interpreter `sub_000120A0`, PC at `obj+0x22C`, jump table `0x152BC`. Opcodes seen: 0x85 call arg, 0x74-0x77 condition slot type (0x76/0x77: type 0x14 with a callback), 0x78 the slot's label, 0x8A wait (timeout at `+0x1B0`/`+0x1B4`), 0x8B label, 0x8C goto, 0x90 end. Conditions evaluated at `0x14910`, jump table `0x15540` (types 0-0x14). Type 0x12 is "hit": `[obj+0x18A] != 0`. Type 0x13: `[obj+0xCE0] == 2`.
  - **The gap:** the `je` at `0x14D69` ends types 9, 0x12 and 0x14. It's reached after `test al, al` and after `test eax, eax`, and lifted as `if (_flags ...)`. `_flags` is a variable nothing assigns, so the je was never taken and the condition always fired. Drain Damage ran its four hurt phases 27 frames apart and called "beaten" 3-5 s in. In the final boss, Crunch punched Cortex at once and "beaten" came ~12 s in (both traced with a temporary tap at `loc_000121E0`).
  - Why: `translator._merge_flag_states` merges cmp/test snapshots with different operands but requires equal width. For ZF and CF the width is irrelevant: `_fa`/`_fb` are masked where the cmp/test runs.
  - **Fix (`edfa555`):** `twoc/tools/lift_flag_join.py` runs `tools.recomp` in-process with one more merge rule (same-kind cmp/test, any width → `__snap_<kind>`, conditions only for `je/jne/jz/jnz/jb/jae/jbe/ja` and their setcc/cmovcc). `lift.ps1` runs it. xboxrecomp is untouched on disk; the rule is the same as `wrap_callers.py` (a twoc tool shaping twoc's own generated code). Two lines of lifted C change: `0x14D69`, and `0x15797F` in the XPP driver (behind the OHCI gate).
  - Results: Drain Damage ran 2 min on the clean build (attacks every ~168 frames). The final boss ran until the idle Crash lost (game over). Bridge off and host-only splash to hub, HEAD vs fix: 231 opens identical and in order, exit 3. Winning either fight is not tested (needs a player). The eight boss scripts use only condition types 0x12, 0x13 and 0x14.
  - Still lifted as `_flags`: 52 sites (`flagsites.py` in scratch). Most are at the starts of mis-detected fragments. Real ones mix setter kinds: `0x14D08` and `0x13749` in the interpreter (types 4/6/8/0xB/0xD always fire; no boss script uses them), `0x62500` in `sub_00062240` (player update, `jge`), `0x6ECA4`/`0x7012A`, `sub_00066680`, `sub_00076A80`, `sub_0011AE20`, `sub_00121717`.
- **Triage (cheapest check each; details and the plan in the report).**
  - *No sound:* the APU and XAudio2 start. But the APU reads RAM at `ram_ptr (guest VA 0) + (phys & 0x03FFFFFF)`, while DSOUND's voice array, SGE lists and buffers are contiguous memory (phys = VA − `0x80000000`, a separate window: "deliberately NOT a view", `xbox_memory_layout.c`). Its stream packets are heap addresses passed through (measured with a temporary tap on `MmGetPhysicalAddress`: VA `0x80B8xxxx` from `0x13F03A`/`0x13EF5E`/`0x13FA2A`, and `0x0368xxxx` from `0x13DDBD`). So the VP reads, and writes, low guest memory instead of the voices. 18.42's picture of the model's voice list was read from that memory.
  - *Model glitch (idle pose, sinking, pads flying):* 24-grab bursts while running the hub ring show it host-only at 60 fps and with the executor on at 9.4 fps (floating pad-like objects, Crash upright mid-run). So it isn't frame-rate or vblank timing. Crash's own position (`0x8F5120+0x6C`) is exactly constant while idle (1 ms polling). Renderer vs game logic is still open: both modes draw through the mirror.
  - *Fog:* FOGENABLE on 309 of 332 draws in an Arctic Antics frame. `xbox_d3d8`'s combiner shader sets `r_fog = fog_color` and has no fog input, so pixel-shader draws never fog. Program draws depend on `oFog` and FOGTABLEMODE, not yet checked.
  - *Flat shading:* no fixed-function draw is ever lit (23 levels). A level frame is ~177 program draws, 131 FVF `0x152`, 75 with a pixel shader; stage 0 texture × diffuse. The xemu ground's smaller, sharper cobbles point at texture coordinates or a second pass. Not pinned yet.
  - *Snow, box breaks:* the bridge doesn't mirror DrawVertices (8,537 skipped over 23 levels), plus the `snow_m` target description. *Water, invisible Wa-Wa:* the dot-product texture modes (18.39).
  - *Pause still:* reproduced (flat lavender). The frame end is GetBackBuffer → CopyRects → Release → Swap → Clear every frame (`sub_00039E00`). The still is probably a CPU read of guest pixels that host-only never writes.
  - *Micro-stutter:* the game's frames are steady (60.0/s, p99 16.95 ms, max 22.5 ms over 40 s; the limiter is never late). The host presents with sync interval 0 and a 59.94 Hz QPC limiter not locked to the display's refresh (239 Hz here). Present pacing.
  - *Vblank at ~32/s:* `kernel_vblank_tick` sets `next_ms = now + 16` on `GetTickCount64` (15.6 ms steps), so two quanta per tick. Game logic uses the TSC instead: `sub_00037540` counts 1/60 s ticks (min 1, max 6 per frame), `sub_000ED3AF` is QueryPerformanceCounter.
- **Files changed:** `twoc/tools/lift_flag_join.py` (new), `twoc/lift.ps1` (`edfa555`); this entry, "Start here tomorrow", and mouse look taken off the wish list (sections 1, 2, 4, 12, 14.10) (notes commit).

### 18.44 Step 42: the mixed-setter flag joins are lifted (14 branches). The flying pads are game side: the game submits their transforms in the air, and the mirror draws exactly that. Audio step 0 is in (the APU reads the right memory), but nothing is heard yet: the monitor throws the VP's output away.

Report: `step42_report.md` in the session scratch folder, runs in `runs/`. Commits: `264ae78` (flag joins), `85d3114` (motion log), `a591bb2` (MmGetPhysicalAddress) in twoc; `853b2ee` in `xboxrecomp/` (local, never pushed; the 18.44 rule refinement at the top of section 18).

- **Mixed-setter flag joins (`264ae78`, `twoc/tools/lift_flag_join.py`).**
  - When a join's predecessors set the flags with different instructions (cmp on one edge, test or dec on another), each predecessor now evaluates the conditions its own state answers, at its end and before its jump, into a bit set `_fj` (bit 0 je, 1 jb, 2 jbe, 3 jl, 4 jle, 5 js, 6 jo, 7 jp), and the consumer reads its bit. A bit counts only if every predecessor can answer it; an answer that reads `_cf` only where the function declares `_cf`. The merged state `__mat_<mask>_<origin>` passes through blocks without a flag setter and merges again. `_fj` is its own variable, so the `_flags` fallback stays 0.
  - Only joins whose result a consumer reads write `_fj` (the origin names the join), only the bits read are written, and a join whose assumed predecessor state differs from what the emitted block ends with is refused (the translator's back-edge probe stops after 3 rounds). A function with a new join is lifted again (at most 8 times).
  - Result: 14 consumer branches in 9 functions, 39 predecessor writes. `0x13749`, `0x14D08` (boss interpreter), `0x62500`, `0x666D8`, `0x667BA`, `0x6ECA4`, `0x7012A` (×2 fragments), `0x77061`, `0xE78A5/A9/AD` (js), `0x11B040`, `0x121829`. 38 `_flags` sites remain, all at fragment entries with no known predecessor (plus `0x31407` in 8 fragments and `0xB85FE`'s jp/loopne).
  - Bridge off, `drive.py` 150 s, HEAD vs this: 356 `[PATH]` lines identical and in order, same events, exit 3.
  - Per-site trace (a temporary tap build counting taken/not taken at all 14): **none ran** in the hub, Arctic Antics (`snow_m`) or Tornado Alley (`farm`). The interpreter's types 4/6/8/0xB/0xD can't run with this game's data: the 8 boss scripts use only 0x12-0x14 (18.43).
  - **Correction to 18.43:** `sub_00062240` isn't the player update. It's camera heading code (camera globals `0x941Dxx`); the fixed `jge` is on the path for one camera-hint object `[0x942068]`.
- **The model and pad glitch: game side (motion log, `85d3114`).**
  - `RECOMP_D3D_BRIDGE_MOTIONLOG=PATH` logs one compact line per mirrored draw while `PATH.on` exists. Program draws: constant slots 96-99 (the XDK's c0-c3: every hub program reads its transform there, `vscheck.py` on step 41's `tri2`) and 124-126 (the skinned programs' first bone), the draw's caller and the call chain of the last write to c0. Fixed-function draws: the world translation. `tools/diag/motionlog.py` finds draws that leave their path and come back, and tracks one mesh. Inert when unset.
  - Hub ring runs (`mot2`-`mot5`, frame-tagged grab bursts): the orange pad pieces show in the air above the domes at the same point of the walk every run.
  - They are program `F1CEC538`, 77 vertices, 5 instances: the game's own transforms put them about halfway between the camera and the floor pads (w ≈ 5-7 against 14-16), high on screen, smoothly, for as long as they're in view (frames 1842-2055, 3.5 s). The floor rims (same program, 272 vertices) sit on the floor. They look like "a few frames" because the rock pillars hide them most of the time: the grabs show them exactly when the data puts them in open sky.
  - No other writer can bypass what the mirror reads: the only other `SET_TRANSFORM_CONSTANT` writers in D3D (`0x1026F0`, `0x102850`) load the fixed-function program's slots 60-62 and 0-1, and no game code pushes constants itself. Fixed-function draws: no world-translation excursion over 0.05 units in 850 swaps.
  - Both rim kinds are drawn by `0xB7240` from `0xA5D63` (c0 set through `0x3AD82`); Crash's skinned meshes by `0xB7C5E`. Crash's first bone moves smoothly (≤ 0.1 NDC wobble); the "idle pose" frames weren't caught in these runs (only the first bone is logged).
  - Not fixed (not small): next is where the pieces' object transform is written.
- **Audio step 0: done (`a591bb2` + `853b2ee`). Step 1: blocked, needs a decision.**
  - twoc's hand-written `MmGetPhysicalAddress` (`[0x15ABD4]`): a DSOUND caller (return address in `0x135AA0-0x1533E4`) asking about a low-RAM address gets it back with bit 26 set; anything else goes to the runtime. XPP's 4 calls are untouched and D3D never calls it. Seen: `0x00140000` (from `0x13D74A`) and stream packets `0x0368xxxx` (from `0x13DDBD`).
  - `853b2ee`: the APU's physical accessors send a marked address to low RAM and any other to the contiguous window (host = RAM pointer + `0x80000000` + offset).
  - Bridge off, 150 s: 356 `[PATH]` lines identical to the flag-join build, same events, exit 3, the same 11 `[VOICEOFF]` lines.
  - **Still silent**, measured with `tools/diag/loopcap.exe` (WASAPI loopback of the default device, per-second RMS): 115 s of digital silence, splash to hub. The reason is in the monitor: every 8th APU frame `mcpx_apu_monitor_frame` clears `frame_buf` and fills it with the test tone and the HLE software mixer only, overwriting the 256 samples `mcpx_apu_dsp_frame` just wrote from the VP's mixbins, on both the XAudio2 and the waveOut path. It also submits 1,024 samples for every 256 produced.
  - Experiment (uncommitted, reverted; `experiment_monitor.patch` in scratch): keep the DSP stub's 256 samples, mix the software voices on top, submit in 1,024-sample buffers. Then sound reaches the device: 5-10 s (logos), 26-35 s (intro), 82-85 s (hub music opening), each stopping after a few seconds. The same experiment without step 0 gives sound only at 5-10 s and two blips.
  - What plays is not yet the right audio: cross-correlation with the decoded source files (`LOGOS.wav` is a rising sweep; `ThemeL.wav`) is 0.2-0.4 at inconsistent offsets, and the capture is tonal lines plus broadband clicks. Candidates: decode/format/pitch in the VP, and the streams stopping because nothing refills them (`[VOICEOFF] ... ISR runs 0`: the APU interrupt never reaches DSOUND).
  - Decision needed: the monitor fix is `xboxrecomp/` behaviour outside the 18.44 refinement.
- **The final build (all three commits):** bridge off, 150 s: `[PATH]` lines identical to HEAD, exit 3. Host-only, 120 s, splash to hub: exit 3, no `[CRASH]`, `[PATH]` lines identical to the step 0 build's host-only run.
- **Files changed:** `twoc/tools/lift_flag_join.py` (`264ae78`), `twoc/src/d3d_bridge.c` (`85d3114`), `twoc/src/recomp_manual.c` (`a591bb2`); `xboxrecomp/src/apu/{apu_shim.h,apu_vp.c}` (`853b2ee`, local); `twoc/tools/diag/{motionlog.py,loopcap.cpp,loopcap_build.bat,README.md}`, the 18.44 rule refinement, this entry and "Start here tomorrow" (notes commit).

### 18.45 Step 43: audio step 1 (stereo). The game is heard: the title music, the logo and intro voices, the hub's ambience and its music play from the emulated voice processor at their own pitch, continuously, matching the source files. Present pacing is locked to the display.

Report: `step43_report.md` in the session scratch folder, runs in `runs/`. Commits: `a8bd0c6` (the APU interrupt, DSOUND's IRQL lock), `063f04a` (one idle reported twice), `03d755b` (vsync pacing) in twoc, plus the notes commit; `31b7165` (monitor) and `4b439c7` (VP resampler) in `xboxrecomp/` (local, never pushed; the 18.45 rule refinement).

- **1. The monitor (`31b7165`).**
  - `mcpx_apu_monitor_frame` keeps the VP's 256 samples, adds the test tone and HLE mixer only when on, submits exactly 256 (XAudio2: a ring of 16 one-frame buffers; waveOut: 8 frames a buffer), and clears `frame_buf` after the submit (a frame whose pipeline didn't run stays silent).
  - With that alone the APU made 40-45 kHz and the device ran dry 12-30 times a second: `throttle` waits in timer quanta (15.6 ms) and restarts its clock when a wait overshoots. With a device it now waits for the queue (fewer than 6 frames queued); the clock stays as the no-device fallback.
  - `[APU-OUT]` every 10 s: 48.0 kHz, 0 underruns, 0 dropped, from splash to hub. Bridge off: 356 `[PATH]` lines identical to HEAD, exit 3.
- **2. The APU interrupt (`a8bd0c6`, `063f04a`).**
  - The model records interrupts (notifier status in ISTS, a trapped front end in FECTL, a `set_irq` flag) and never raises them. DSOUND's ISR (`0x13A0FB` → `0x13A05F`, KINTERRUPT `0x153260`) acts only with GINTSTS set: bit 4 (trap) runs `sub_00139F42` (an idle voice → `sub_001399D0(handle)`, then the front end is resumed), bit 6 queues its DPC (`ctx+0x770`, `0x139F1B` → `0x139E7F`: `KeSynchronizeExecution` moves the status to `ctx+0x768`, then `sub_001398CD` services every voice's and stream's notifiers: packets complete and the next are queued).
  - `twoc` has a thread that is the interrupt line: every millisecond it reads ISTS/IEN/FECTL; when something is pending it writes 0 to ISTS (the model's write path runs `update_irq`) and calls the ISR. Guest stack from the heap (not a worker slice, below), its own TIB. Idle-voice traps the model still has playing, or for a voice DSOUND no longer has linked, are resumed as stale.
  - **DSOUND's IRQL made a lock.** DSOUND keeps interrupt-time code apart from its API by raising IRQL (guard `sub_001395F1`/`sub_00139624`: the library's only `KfRaiseIrql`/`KfLowerIrql`, 67 regions). The runtime's IRQL is a per-thread number, so the ISR on another thread turned off a voice a game thread was unlinking (crash in `sub_0013AAF6`). The outermost raise now takes a recursive host lock, released by guard address; the ISR and the DPC (`sub_00139F1B`, wrapped) hold it; cooperative suspend counts it as a held lock. The interrupt thread only tries its locks. Re-lift needed (three new `_gen` bodies; nothing else changes in the generated code).
  - **A hang it exposed, fixed in the VP (`4b439c7`):** `voice_resample` returned 0 when a voice gave nothing, and `voice_process` loops until it has 32 samples: a persist stream with an empty SSL (waiting for its next packet) spun the frame thread with the APU lock held, every guest `VOICE_LOCK` waited on it, and the hub never loaded. Found by sampling every thread at a 2 s hold of DSOUND's lock.
  - **One idle reported twice (`063f04a`):** 18.42's voice-off poll reports a stopping voice's idle itself, and the pending trap reported it again; the second `sub_001399D0` unlinked an unlinked voice (null prev) and `sub_0013AAF6` crashed (2 of ~14 runs, at the intro skip). Both paths now report only a voice still linked (`vw_voice_linked`). Six host-only runs past the intro skip: no crash, hub every time.
  - Stale traps: the model runs PIO methods while trapped (the hardware queues them), so a game thread can overwrite FEDECMETH before the thread looks (`[APU-IRQ] stale trap: method 0000037C`). Resumed; the VP traps on the idle voice again next frame.
  - A 30 s `[APU-IRQ]` line counts ISR runs, idle traps and stale traps (hub run: ~130 ISR runs, ~15 traps in 95 s); `[APU-IRQ] DSOUND's IRQL lock held N ms` reports a hold over 2 s; `[DSIRQL]` lines report a guard that left its frame raised (none seen).
- **3. VP decode against the sources (`4b439c7`; `tools/diag/srcmatch.py`, `timeline.py`, `residual.py`).**
  - ADPCM and format are right: before the resampler the capture matches `ThemeL.wav` and `INTRO1.wav` with correlation 1.000 at speed 48000/44100. The files are Xbox ADPCM at 44.1 kHz, 64 samples per 36-byte block (the headers say 24,806 B/s; step 42's decoder made 65, part of its low scores).
  - Pitch: `voice_resample` ignored the rate. Now linear interpolation with a 0.32 fixed-point position per voice (reset at VOICE_ON). At speed 1.0: title music 1.000, intro voice 1.000, each advancing one second per second.
  - Host-only capture, splash to hub (`i12`): 6-26 s the title music (`ThemeL.wav` 0.2 s → 17.2 s) with the logo voice (`LOGOS.wav`) over it; 28-33 s the intro voice; 35-124 s the hub ambience `HUB_BG.wav` (0.93-0.99, looping every ~13 s). The hub music (`ThemeL.wav` again, from 82.7 s) plays 8-10 dB under the ambience: with the ambience subtracted it matches from 0.4 s and runs continuously 100-123 s; one ~0.35 s slip between 88 and 100 s (Crash starts walking), with no output underrun, so a late packet.
- **Bridge off, final build:** 356 `[PATH]` lines identical to HEAD and in order, same timeline, exit 3, no crash, no 5 s force-offs.
- **Present pacing (`03d755b`, 18.43's micro-stutter).** DWM reports 240.00 Hz (not 239.76). When the refresh is within 0.5% of N × the game's rate (N ≤ 4), the host presents at DXGI interval N with no limiter and waits on the swap chain's frame-latency object (2 frames) outside the bridge lock; otherwise, and with `RECOMP_D3D_BRIDGE_HZ`/`_INTERVAL` set, the old limiter. A `[D3DB] host presents ... by DWM compositions since the last` line: vsync pacing, 600 of 600 presents one composition after the last in steady play; the limiter (`--hz=59.94`), ~10% of presents in the same composition as the next (never shown) and uneven gaps. DWM composes on demand, so these are compositions, not refreshes.
- **Side finding (not fixed): worker stacks inside game data.** `XBOX_WORKER_STACK_BASE` is `0x00780000`, inside Crash's `.data` (to `0x945758`): the 3 MB block `0x5EB840`-`0x8EB840` that `sub_00032CC0` clears and carves into pieces (`sub_00036010`). The timer thread takes slice 0 (stack top `0x7BFFF0`, 1.83 MB into it) and runs every DPC and the vblank ISR there. OHCI's ISR takes a slice too. A candidate for glitches no trace explains; runtime layout, so it needs its own decision.
- **Open:** the balance of the mix (the ambience is louder than the music; no GP/EP mixdown, every bin summed at unit gain), the stream slip, 3D/HRTF voices and effects not checked by source, the DSP (reverb), surround.
- **Files changed:** `twoc/src/recomp_manual.c`, `twoc/src/main.c` (`a8bd0c6`, `063f04a`), `twoc/src/d3d_bridge.c`, `twoc/src/d3d_host.{c,h}` (`03d755b`); `xboxrecomp/src/apu/{apu_core.c,apu_xaudio2.c,apu_xaudio2.h}` (`31b7165`, local), `xboxrecomp/src/apu/{apu_vp.c,apu_state.h}` (`4b439c7`, local); `twoc/tools/diag/{srcmatch.py,timeline.py,residual.py,README.md}`, the 18.45 rule refinement, this entry and "Start here tomorrow" (notes commit).

### 18.46 Step 43 follow-up: the reported slowdown isn't reproduced; step 43 costs ~2% of a core off the game threads. The vsync wait is bounded, the log counts stutter, and the APU interrupt can be switched off.

Report: `step43b_report.md` in the session scratch folder, runs in `runs/` and `lvl/runs/`. Commit `dd841bc` (twoc).

- **The report:** a playtest (`-HostOnly -FastTitle`, window visible) felt much slower with stutters, and `RECOMP_D3D_BRIDGE_HZ=59.94` didn't help. The one surviving log (`run_twoc.err.log`, 22:11, ~50 s) says `present interval 4 (vsync paced), frame limiter off`, so that run didn't have the variable: in PowerShell it has to be `$env:RECOMP_D3D_BRIDGE_HZ = '59.94'` in the same session. That log's hub reached 58.5 swaps/s with every present one DWM composition after the last. Its draws took 13-17 µs each, like ours (12-14 µs); the slow first 12 s (271 µs a draw) is the title's first texture creation.
- **Measured from outside** (`tools/diag/perfmon.py`, `perfsum.py`: frame counter polled every ~1 ms, per-thread CPU, the bridge window kept topmost), HEAD vs the step 42 build (`f751b7d` + `xboxrecomp` `853b2ee`), same unattended paths:
  - Hub, 38 s: both 60.0 fps, p99 17.8 ms, 0 gaps over 25 ms. With the old limiter the same.
  - Intro played through (not skipped): both 60.0 fps.
  - Loads (intro skip → hub), 10 runs (4 vsync, 3 limiter, 3 step 42): all 59.9-60.0 fps, p99 17.7-17.9 ms, the same load gaps (26 s, 33.7 s).
  - Arctic Antics, 44 s played (new game WOMBAT, warp room 1 pad 1, running and jumping): HEAD 60.0 fps p99 17.96 ms, step 42 59.9 fps p99 17.86 ms, HEAD with `RECOMP_APU_IRQ=0` 60.0 fps p99 17.85 ms; 1-2 gaps over 25 ms each, none over 33 ms.
  - Window minimized 15 s in the hub: no change in any build.
  - CPU in the hub: `nv2a_ack_thread` spins at ~99% of a core in both builds (not new), the main thread 49-50% (HEAD) vs 53-55% (step 42), the interrupt thread 1.2-1.4%, the APU frame thread 0.6-1.1%.
  - A first measurement showed a gap over 25 ms about once a second in every build: the monitor's own thread snapshot stalled its polling. Frames and CPU are now two processes.
- **The one defect found:** once in six vsync runs, the game stood still 1-2 s during a load (gaps of 1,003 and 2,258 ms): the present waited on the frame-latency object with a 1 s timeout and the display hadn't released a frame. Now at most 50 ms; three misses in a row and the limiter paces for the rest of the run (`[D3DB] host: the display released no frame ...`). 0 misses in 18,814 presents of a level run.
- **For the next playtest:** `[D3DB] host swap gaps this interval: over 25 ms N, over 50 ms N, over 100 ms N, longest X ms` every 10 s measures the stutter in the log itself. `RECOMP_APU_IRQ=0` turns the APU interrupt off (as before step 43; streams stop a few seconds in) to compare, and `$env:RECOMP_D3D_BRIDGE_HZ = '59.94'` the vsync pacing. The requested step 26 sampler wasn't used: the external monitor needs no rebuild of either build, and thread-level CPU showed nothing new on the game threads to break down further.
- **Bridge off:** `[PATH]` lines identical, exit 3.
- **Files changed:** `twoc/src/{d3d_host.c,d3d_bridge.c,recomp_manual.c}` (`dd841bc`); `twoc/tools/diag/{perfmon.py,perfsum.py,README.md}`, this entry and "Start here tomorrow" (notes commit).

### 18.47 Step 44: the 2 s / 10 s stutter was the log file. `run_twoc.ps1` opens its logs write-through, and the CRT writes unbuffered stderr one byte per WriteFile, so the periodic stat lines held a game thread 60-140 ms. Fixed; the periodic hitches are gone.

Report: `step44_report.md` in the session scratch folder, runs in `runs/` and `boff/`. Commits `1c81230` (twoc) and `d8ce164` (`xboxrecomp`, local, never pushed).

- **The report:** CapFrameX captures (`twoc/reference/perf/`, `run_twoc.ps1 -HostOnly -FastTitle`, window visible). 60 s in Arctic Antics: median 16.7 ms, but a 40-110 ms hitch every 2.000 s and a 140-146 ms one every 10 s, whatever the player does. The user's log counted exactly 6 swap gaps over 50 ms in every steady 10 s interval, in the menu, the hub and the level. 18.46's `drive.py` runs of the same build had 0.
- **What runs on those timers:** every 2 s, `heap_watch` (`[HEAPSTAT]`, `[CONTIGSTAT]`, `[POOLSTAT]`, `[VMSTAT]`, ~1.3 KB) and `xboxrecomp`'s `[KERNEL] summary` (~0.3 KB), on whichever guest thread makes the kernel call past the mark. Every 10 s, the `[D3DB] host` block (~2.6 KB), on the swapping thread. None of it computes much.
- **Why they stall:**
  - Windows PowerShell's `Start-Process -RedirectStandardOutput/-Error` creates the log files with `FILE_WRITE_THROUGH`: the child's stderr has file mode `0x22` under `run_twoc.ps1`, `0x20` under `drive.py`. Each WriteFile then waits for the disk, ~60 µs against ~3 µs.
  - `main.c` makes stdout/stderr `_IONBF`, and the UCRT buffers an unbuffered stream around one `fprintf` only when it is a console: into a file, every byte is a WriteFile.
  - A test built with the same compiler (11 lines, 1,650 bytes): 91-110 ms per burst through `Start-Process` (the first 11.3 s), 4.2-4.7 ms through `Popen`, and 0.03-0.7 ms as one `_write`.
  - The same cost on all boot logging is 18.40's unexplained slower start under `run_twoc.ps1`.
  - Ruled out by runs: the console and Windows Terminal (a hidden launch stutters the same), input (`RECOMP_DEV_INPUT_FILE` set, same), inherited `RECOMP_*` variables (none set).
- **Fix:**
  - `main.c`'s `clear_write_through` clears `FILE_WRITE_THROUGH` on a disk-file stdout/stderr at startup (`NtSetInformationFile`, `FileModeInformation`) and logs `[LOG] the launcher opened stdout and stderr write-through; cleared (notes 18.47)`. Output stays unbuffered, so it still reaches the file before each `fprintf` returns (crash blocks, `_exit` paths).
  - `twoc/src/log_block.h` formats a block of lines into one buffer and hands it to `_write` once under stderr's lock (same bytes). The 2 s stat block (`heap_watch_lines`, outside the per-kernel-call `heap_watch`) and the 10 s `[D3DB]` block use it.
  - `d8ce164` does the same for the `[KERNEL] summary` (18.29 perf-only rule, output identical).
  - The lines stay on; a block now costs one WriteFile.
- **Measured** (`run_twoc.ps1 -HostOnly -FastTitle` in a new Windows Terminal window, as the user runs it; `perfmon.py --frames` and `--cpu --topmost` as two processes; the attract demo plays Arctic Antics, `a\snow_m`):

  | window | fps (old → new) | p99 (old → new) | gaps over 50 ms (old → new) |
  |---|---|---|---|
  | menu | 58.5 → 60.0 | 29.7 → 17.6 ms | 16 → 0 |
  | Arctic Antics demo | 58.8 → 60.0 | 30.8 → 17.7 ms | 7 → 0 |

  - Every steady `[D3DB] host swap gaps` line reads 0/0/0. The only gaps over 50 ms left are level loads.
  - The first swap comes at 1.0 s instead of 12.7 s.
  - PresentMon needs an elevated prompt here, so it wasn't run; the user's CapFrameX is the final check.
- **Bridge off:** 356 `[PATH]` lines identical, the same file-open order, exit 3, 0 force-offs. The `[VOICEOFF]` count varies run to run in the old build too (the APU interrupt races the poll).
- **Files changed:** `twoc/src/{main.c,log_block.h,recomp_manual.c,d3d_bridge.c}` (`1c81230`); `xboxrecomp/src/kernel/kernel_bridge.c` (`d8ce164`, local); this entry, the 18.40 pointer and "Start here tomorrow" (notes commit).

---

## Appendix: links

**Sent by the user:**
- https://github.com/sp00nznet/xboxrecomp
- https://github.com/rexglue/rexglue-sdk
- https://github.com/ran-j/PS2Recomp *(unverified)*
- https://github.com/dougchansan/suyu-v0.0.4 *(unverified)* and https://github.com/suyu-emu/suyu-v0.0.4
- https://github.com/ExpansionPak/GCRecompiler
- https://github.com/hedge-dev/XenonRecomp
- https://github.com/YesterMester/TheSimpsonsGameRecomp
- https://github.com/MaxDeadBear/NaughtyBear_ReStuff
- https://github.com/masterspike52/redahm
- https://github.com/sonicnext-dev/MarathonRecomp
- https://github.com/birabittoh/KameoRePowered
- https://github.com/patchzyy/wiicompiled
- https://github.com/goliathret/EdgeOfTimeRecomp
- https://github.com/doldecomp/melee
- https://github.com/999sian/melee-pc
- https://github.com/coccofresco/TriAevum

**Found while researching:**
- https://github.com/zeldaret/tp
- https://github.com/TwilitRealm/dusklight
- https://github.com/encounter/aurora
- https://github.com/encounter/objdiff
- https://github.com/doldecomp/sms
- https://github.com/hedge-dev/UnleashedRecomp
- https://github.com/TeamWheelWizard/WheelWizard
- https://github.com/rexglue/rexglue-sdk/wiki
- https://github.com/SolarRecomps/rexglue-ostentation
- https://github.com/denzi-gh/crashwoc-decomp
- https://github.com/calmsacibis995/crash-ps2
- https://open-travelers.github.io/OpenCrashWOC/
- https://github.com/NeoKesha/OpenSanityNeo
- https://github.com/Smartkin/twinsanity-reversed
- https://github.com/wurlyfox/c1
- https://github.com/CTR-tools/ctr-native
- https://github.com/Matteo842/CrashBandicoot-Launcher
- https://consolemods.org/wiki/Xbox:Games_with_Alternate_Display_Modes
- https://www.mobygames.com/game/5531/crash-bandicoot-the-wrath-of-cortex/specs/
- https://tcrf.net/Crash_Bandicoot:_The_Wrath_of_Cortex
- https://wiki.pcsx2.net/Crash_Bandicoot:_The_Wrath_of_Cortex

---

## Start here tomorrow (Sep 29, 2026; updated after step 44)

**Current state.** The recompiled game builds and runs (`twoc/build/Release/crash_twoc.exe`). Run it with `powershell -NoProfile -ExecutionPolicy Bypass -File twoc\run_twoc.ps1` (add `-DumpDir dumps` for framebuffer BMPs). It gets through XAPI/CRT startup, D3D and DSOUND init, the splash screen, and the title/logo level loads, past `logos2.cut` into particle-system and cubemap loading, and runs a full 240 s with no crash and no freeze (exit code 3, the normal watchdog end). **Audio now initialises for real (18.25):** `run_twoc.ps1` sets `RECOMP_AC97_READY`, DirectSoundCreate succeeds and DSOUND streams exist. The title animation finishes, the main menu opens (129 s in since 18.29; banner `[GAME] MAIN MENU OPEN`), and a real controller drives it into the next level. **The 18.26 level-load deadlock is fixed (18.27, `6f51e4b`):** `NtSuspendThread`/`NtResumeThread` are cooperative, so a suspended thread parks only at a kernel-call boundary and never holds a host lock. **The 18.32 hub-load deadlock is fixed too (18.33, `800f27b`):** a suspended thread also parks only once it owns no guest critical section. **The intro-load heap exhaustion is fixed (18.28, `145f8ef`):** a hand-written `NtFreeVirtualMemory` returns released blocks to the guest heap. The intro level loads and its cutscene renders (~47-50 fps since 18.30; ~24 after 18.29, ~6.8 before). **The intro plays to its end (~10 min) and loads the hub (18.30); Start skips it.** The hub hung on a second GP DSP mailbox wait, now fixed (`sub_0013937A`, `c262d7d`). **Crash walks in the hub:** pushing the stick moves the player object `0x8F5120` (position at `+0x6C`) at ~0.3 units/frame, and he stops at walls. The screen is still garbage, and the hub runs ~10 fps; a `[HEAPSTAT]` line every 2 s watches the heap (93.5% bump high-water, flat once the cutscene runs). **Fast unattended testing (18.26):** `run_twoc.ps1 -FastTitle -AutoStart` opens the menu ~10.5 s after launch (~21 s before 18.29), presses "start game" by itself (virtual pad if none is connected), and reaches the intro load ~35 s after launch, then the intro cutscene at ~45 s (18.28, 18.29); one Start press then skips to the hub. `RECOMP_DEV_INPUT_FILE=<path>` (18.30, test-only) drives port 0 from a one-line text file while the game runs, and `twoc/tools/diag/crashpos.py` prints Crash's position under scripted input; both switches are off by default and inert when unset. **Input now reaches the game (18.23):** a hand-written `XInputOpen`/`XInputClose` pair (`sub_00154249`/`sub_0015429F`) arms controller slots with per-port tokens, and real button presses arrive in the game's state buffer every frame. The `twoc/` fixes so far: the D3D fence mirror in `main.c` (18.1), 52 seeded function starts in `seed_functions.json` (18.6), a hand-written D3D interrupt DPC `sub_00106E60` with vblank forced on (18.7), a hand-written CRT memmove `sub_000F5DD0` (18.8), a hand-written `GetExitCodeThread` (`sub_000ECB2B`, 18.9), a hand-written null-interface guard on `sub_000B9890` for the DSOUND 3D-listener chain (18.13), hand-written XPP poll/vibration entries `sub_001542AB`/`sub_0015431A` plus one `xbox_InputInit()` call (18.15), a hand-written `sub_000EB270` port-arming gate (18.16), hand-written XPP open/close with port-aware poll/vibration (18.23), and the DSOUND bring-up (18.25): `RECOMP_AC97_READY` in the run script, a hand-written GP DSP handshake `sub_001391DE`, GP/EP DSP memory left as plain RAM in `main.c`, and a main-menu banner wrapper on `sub_0002BCC0`, plus the test-only `-FastTitle`/`-AutoStart` options (18.26), cooperative `NtSuspendThread`/`NtResumeThread` (18.27; no park while owning a guest critical section, 18.33), and a hand-written `NtFreeVirtualMemory` plus the `[HEAPSTAT]` heap watch (18.28), and a hand-written GP DSP post `sub_0013937A` (18.30), and hand-written contiguous memory (`MmAllocateContiguousMemory[Ex]`/`MmFreeContiguousMemory`/`MmQueryAllocationSize`, 18.34: freed blocks are reused, with a `[CONTIGSTAT]` line next to `[HEAPSTAT]`). Reusable diagnostics (hang sampler patch, map symbolizer, run comparer, VM tap, time-profile sampler) are in `twoc/tools/diag/` with a README. `xboxrecomp/` has two small additive read-only accessors, committed locally only in that clone and not pushed: `xbox_bridge_resolve_handle` (`52531ac` on `766ecef`) and `xbox_HeapStats` (`f8da7ec`, 18.28), plus two perf-only changes with identical output, `694cb98` (cached `RECOMP_PB_EXEC_VERBOSE`) and `5012b2d` (rate-limited `[NV2A] DMA_PUT` line), 18.29, and `fe406e4` (O(1) survey lookups), 18.30 (see the two rule refinements at the top of section 18). Since 18.44 also `853b2ee`, the APU's physical addressing (its own rule refinement), and since 18.45 `31b7165` (the APU monitor) and `4b439c7` (the VP resampler; flagged as beyond the approved monitor change), and since 18.47 `d8ce164` (the `[KERNEL] summary` in one write, perf only). **Since 18.45 the game is heard:** the APU interrupt reaches DSOUND (`twoc` thread + DSOUND's IRQL made a lock), the title music, logo and intro voices, hub ambience and hub music play at their own pitch and match the source files (`tools/diag/srcmatch.py`/`timeline.py` on a `loopcap.exe` capture). Frame rate: `twoc/tools/diag/fpsrun.py` + `fps.py` read the game's frame counter `0x25A660` from outside the process. **Option B: S1, S2 and S3 done (18.32-18.34):** 24 D3D wrappers in `recomp_manual.c` (`sub_X` → lifted `sub_X_gen`) feed `twoc/src/d3d_bridge.c` (guest side) and `twoc/src/d3d_host.c` (host side). All of it is inert unless switched on. `RECOMP_D3D_BRIDGE=1` opens a second window, "Crash TWOC - D3D bridge (host)", with an `xbox_d3d8` device. It mirrors the game's back-buffer clears and, since 18.34, its DrawVerticesUP draws (state translated from XDK to PC values, textures cached and uploaded): **the splash's Universal logo fades in and out on that window.** Since 18.35 (S4 part 1) it also mirrors render-to-texture, pixel shaders (re-encoded for `xbox_d3d8`'s combiners) and P8 textures on those UP draws. **Since 18.36 it mirrors fixed-function indexed draws (FVF `0x152`) with their transforms, material and lights: the LOADING text, the title animation, the menu and name entry, and the hub's world show on that window.** **Since 18.37 it also mirrors immediate mode (Begin/SetVertexData/End: the menu's particles, the hub's portal glows) and CopyRects (back buffer to texture, the hub).** **Since 18.38 (S5 part 1) it also mirrors vertex programs** (UP and indexed; xbox_d3d8's translator fixed locally, `a0b306d`): **Crash's model and the menu's logo prop show on that window.** **Since 18.39 (S5 part 2) cube maps upload, and program draws that sample the hub's CopyRects copy are mirrored with their texel coordinates scaled; no draw is skipped on any path from splash to hub.** `run_twoc.ps1 -Bridge` runs the mirror (18.39). **Since 18.40 (S7 part 1), `run_twoc.ps1 -HostOnly` runs the mirror alone with the executor off, at 60 fps (a 59.94 Hz limiter at the host present), splash to hub; the heap is flat over repeated level loads (worker TIBs are freed and the runtime heap is guarded across threads).** **Since 18.42, with WOMBAT, every warp room is open and 23 levels have loaded; the heap has a pool arena and a VM zone (bump 84.1%, at least 10 MB contiguous free at every load), and voice stops no longer stall the game.** Every path is counted on a `[D3DB] host draws` line. With `-DumpDir` it writes `hostNNN.bmp` next to the executor's `shotNNN.bmp`. `RECOMP_D3D_BRIDGE_TINT=1` clears to the frame index as a check, `RECOMP_D3D_BRIDGE_POINT=1` point-samples as the executor does, `RECOMP_D3D_BRIDGE_DUMP_EVERY=N` adds host dumps, `RECOMP_D3D_BRIDGE_SKIPTEX=1` (18.36) writes the textures of the draws the mirror skips, `RECOMP_D3D_BRIDGE_DRAWLOG=A-B` (18.37, `drive.py --drawlog`) logs every draw of guest swaps A-B with its screen box and dumps each of those swaps, `RECOMP_D3D_BRIDGE_VSDUMP`/`_VSPROBE` (18.38, `--vsdump`/`--vsprobe`, checked by `tools/diag/vscheck.py`) dump each vertex program and probe where the host draws its vertices, `RECOMP_D3D_BRIDGE_VP_COPY=1` also mirrors the copy-sampling program draws, and `RECOMP_D3D_CENSUS=1` is 18.32's census. `twoc/tools/diag/drive.py` runs splash → hub unattended (`--bridge`, `--tint`, `--point`, `--census`, `--dump=DIR`, `--dump-every=N`), `census.py` summarises a census, `fbcompare.py` compares host dumps with executor dumps, and `splashcheck.py` is S3's per-frame splash check. To regenerate: `twoc\analyze.ps1` (disasm with the seeds), then `twoc\lift.ps1` (it now also runs `twoc\tools\wrap_callers.py`, without which callers bypass the wrappers), then `cmake --build twoc\build --config Release --parallel`.

**Resolved (18.25): the `*(0x427A58)` "3D listener" gap.** It was never a listener: it is the sound manager's `IDirectSound*`, null only because DirectSoundCreate failed with DSERR_NODRIVER (no AC'97 codec reported). With `RECOMP_AC97_READY` it is populated and the 18.13 guard is inert. Remaining audio limits: no DSP, so the effects image (reverb) does not run; the GP handshake is acknowledged, not executed.

**Also known, accepted gap (bypassed for gamepads by 18.23's hand-written open/close; still true for hot-plug and any other USB device).** The real XPP arming path is gated behind real OHCI hardware at *two* levels, both confirmed by exhaustive full-binary xref checks (no write anywhere in the binary, same method used for the DSOUND listener gap): (1) `[0x1534B0]`/`[0x153434]`, the root-hub-status-change interrupt gate (ISR `sub_001596F4`, traced in 18.16), and (2) the host-controller-context table at `0x153404`/`0x153408`/`0x15340C` that `sub_001547DA` looks a port up in before `sub_000EB5A0` can arm a slot (traced in 18.17, confirmed with a real controller connected — `xbox_InputIsConnected(0)=1` but `sub_000EB5A0(0)` still returns `slot[0]=0`). Both are the same underlying gap: this recomp's USB/host-controller emulation (14.4/15.1) doesn't walk far enough to populate either. 18.16's fix (a gated one-shot call into the game's own real insertion handler) is correct and reachable but cannot get past gap (2) on its own; building real OHCI host-controller-registration and root-hub-status emulation would be the faithful fix for both, not yet attempted. **18.18 scoped a lighter-weight alternative (fabricate a fake context object to satisfy gap (2)'s lookup) and found it isn't lighter-weight at all**: a match falls straight into a real two-phase blocking USB SETUP-packet transfer (`sub_001554A1`) needing 2-3 fabricated sub-structs, two more untraced callees, and a `KeWaitForSingleObject` wait this recomp can't ever complete (fails fast, doesn't hang, but still fails) — realistically the same size/category as option 1, not a quick data fix.

**Also known, now well-scoped — and confirmed to need a full swap, not incremental bridging.** *(Corrected by 18.31: the all-or-nothing constraint only applies to replacing the native device. A mirror bridge that calls the lifted body first and never writes guest memory is incremental. See item 1 of "Next step".)* The NV2A executor (`nv2a_pgraph_d3d11.c`, driven by `RECOMP_PB_EXEC=1`) is a hard-coded 2D-only renderer built for a different title's menus (Burnout 3 — hard-coded texture names, a fixed 5-dword pre-transformed vertex layout, `NV097_SET_TRANSFORM_PROGRAM`/`_CONSTANT` and vertex-array-format uploads all silently ignored) — this is why 91% of triangles are "fully off-surface" (18.14): real 3D vertices get reinterpreted as already-in-screen-space pixels. **18.19 found the toolkit already has a full, working, tested fix for this sitting unused**: `xbox_d3d8`'s `d3d8_vsh.c` is a complete, already-wired-up NV2A vertex-microcode-to-HLSL translator (validated by `tests/d3d8_smoke`), reachable only via 15.1's *original*, not-yet-taken plan (replace the game's ~91-99 D3D calls at their API entry points, rather than running over the push-buffer executor). That route turns out cheaper than 15.1 feared — Crash's D3D library is a normal, cleanly-bounded library, not the harder LTCG case `d3d8ltcg-device-context.md` warns about — and would fix the vertex-program gap as a side effect, with no new toolkit code. **18.20 tried the incremental version of this plan (bridge `CreateDevice`, leave the other ~90 entry points on the old push-buffer path) and confirmed by direct observation that it cannot work**: `CreateDevice` succeeded (real `xbox_d3d8` device, vertex-shader translator initialized), but the very next unbridged D3D call crashed immediately, because a host-shaped `xbox_d3d8` device object and the native, push-buffer-shaped object every other D3D function still expects cannot coexist at the same address, and unbridged code dereferences raw offsets directly rather than through an accessor a handle layer could intercept. **Option B is therefore an all-or-nothing swap**: `CreateDevice` plus the entire wrapper cluster (all ~91-99 entry points) have to be bridged together in one batch before any of it can be tested, not incrementally a few functions at a time. **18.21 then did the scoping pass this implies, over all 99 entries via the disassembler's own full xref database rather than by hand**: they split cleanly into 4 cost tiers -- CreateDevice itself (done once, 18.20); 4 shared-plumbing entries covering 33% of call sites that gate one object/refcount-model design decision; 36 confirmed (+48 probable) simple single-push-buffer-opcode writers that become ~1 line of C each once an NV097 method-number table is built (extending the 21 constants `nv2a_pgraph_d3d11.c` already has); and 10 large entries (30-85-function closures each, CreateDevice-class effort) that are the real cost driver despite being only 7% of call sites. Real-size estimate: roughly **10-15x what CreateDevice alone took in 18.20**, concentrated in those 10 functions and the object-model decision, not spread evenly across all 99.

**Next step.**
0. **S7 (18.40): part 1 done** (`1ee8de2`, `44d29e9`). `run_twoc.ps1 -HostOnly` = `-Bridge` with the executor off. Menu and hub 60.0/s; menu idle timeout 30 s, as the game counts it (1800 updates); 14 level loads with the heap flat at 39,773 KiB (`cycle.py`); natural demo ends return to the menu; bridge off identical.
   - ~~The first attract demo crashes on the normal title path~~ **fixed in 18.41 (`3648de8`)**: our hand-written `sub_000EB270` never popped its return address, and the stack leak handed `sub_00087BF0` a garbage level-pool size.
   - **Playable (18.41):** host-only, the hub's earth warp room (the only open wing) leads to Tornado Alley (pad 2) and Arctic Antics (pad 1). Both load and play at 60 fps; pause → WARP ROOM returns to the hub. The spawn is the Load/Save pad (it opens the LOAD/SAVE menu; EXIT with d-pad down + A).
   - **Every area (18.42):** start a new game named WOMBAT (25/25 crystals, 106%). The warp-room table `0x19A7F0` and the pad paths `0x19C118` give every pad's level and position; scratch `visit.py`/`newgame.py` (step 40) drive them. 23 of the 25 main levels have been loaded host-only.
   - ~~Heap: 96% of the bump range~~ **fixed in 18.42 (`e05685c`)**: it was no-split slack (16.5 MB held by small DSOUND pool blocks), not level data. A pool arena and a VM zone keep bump used at 84.1% with ≥ 10 MB contiguous free at every load; no level needs more than the heap has.
   - ~~The voice-off waits~~ **fixed in 18.42 (`33feacc`)**: `sub_0013ADB8` is wrapped to deliver what the APU's idle-voice interrupt would. 0 five-second force-offs over 12 levels; the hub and level changes no longer stall.
   - **Long sessions (18.42):** the runtime's kernel-call counter wrapped past 2³¹ after ~30 min and then logged every call (~7 fps). Saturated in the `xboxrecomp/` clone (`fe75987`, local), and the voice-off fix removed the spins that got it there.
   - **The bosses (18.43, `edfa555`):** Drain Damage and the final boss no longer end early. A never-taken `je` in the boss-script interpreter made its "hit" condition always fire; `twoc/tools/lift_flag_join.py` (run by `lift.ps1`) fixes that class of join. Re-lift with `twoc\lift.ps1` after any lifter change.
   - **Step 42 (18.44):** the mixed-setter flag joins are lifted (`264ae78`, 14 branches; none ran in the hub or two levels). The flying pads are **game side**: `RECOMP_D3D_BRIDGE_MOTIONLOG` (`85d3114`, read by `tools/diag/motionlog.py`) shows the game submits the pieces' transforms in the air (program `F1CEC538`, 77 vertices), and the mirror draws exactly that. Audio step 0 is in (`a591bb2` + `xboxrecomp` `853b2ee`), still silent: the APU monitor discards the VP's output.
   - **Step 43 (18.45): audio step 1 done.** Monitor (`xboxrecomp` `31b7165`), VP resampler and starved-voice hang (`4b439c7`), the APU interrupt and DSOUND's IRQL lock (`a8bd0c6`, `063f04a`): host-only, splash to hub, sound every second, matching `ThemeL.wav`, `LOGOS.wav`, `INTRO1.wav`, `HUB_BG.wav` and (under the ambience) the hub's `ThemeL.wav`. Present pacing locked to the display (`03d755b`). Side finding: the timer thread's stack is inside the game's 3 MB pool at `0x5EB840` (worker slices at `0x780000`).
   - **Step 43 follow-up (18.46):** a playtest felt slower; not reproduced. HEAD and the step 42 build measure the same in the hub, the intro, loads and Arctic Antics with the window visible (`tools/diag/perfmon.py`/`perfsum.py`). The vsync wait is bounded (`dd841bc`), `[D3DB] host swap gaps` counts stutter in the log, `RECOMP_APU_IRQ=0` switches the interrupt off. Next playtest: send the log (or two, with and without `$env:RECOMP_D3D_BRIDGE_HZ = '59.94'` / `$env:RECOMP_APU_IRQ = '0'`), and say where it stutters.
   - **Step 44 (18.47): the playtest stutter is fixed.** A 40-110 ms hitch every 2 s and a 140-146 ms one every 10 s (the user's CapFrameX): `run_twoc.ps1`'s `Start-Process` redirection opens the logs write-through, and unbuffered stderr went out a byte per WriteFile, so the periodic stat lines cost 60-140 ms. `1c81230` clears the write-through at startup (a `[LOG]` line says so) and writes each periodic block in one call; `xboxrecomp` `d8ce164` does the same for the `[KERNEL] summary`. Menu and Arctic Antics (attract demo) through `run_twoc.ps1` in Windows Terminal: p99 30 → 17.7 ms, 0 gaps over 50 ms; the boot's first swap at 1.0 s (was 12.7 s). Next: the user's CapFrameX capture to confirm; then the 0.8 s / 1.4 s level-load freezes.
   - **Rhys's playtest (18.43), plan in order** (details and audio scoping in `step41_report.md`, step 42 in `step42_report.md`):
     1. ~~The model/pad glitch: game or mirror~~ game side (18.44). Next: find where the floating pieces' object transform is written (drawn by `0xB7240` from `0xA5D63`; tap the draw for `F1CEC538`/77 vertices and walk back to the object's matrix), and log all of Crash's bones to catch the idle-pose frames.
     2. ~~The remaining mixed-kind `_flags` joins~~ done (18.44, `264ae78`). 38 sites left are fragment entries with no known predecessor.
     3. Audio: ~~step 0, the physical addressing~~ done (18.44). ~~Step 1, stereo~~ done (18.45). Next: the mix balance (the hub ambience is 8-10 dB over the music: a native GP/EP mixdown with the bins' headroom, step 41's scoping), the ~0.35 s stream slip in the hub, 3D voices and effects against their sources, then surround. *(Before 18.45:* step 1 needed a decision: the monitor (`mcpx_apu_monitor_frame`) clears the VP's 256 samples before every submit, an `xboxrecomp/` behaviour outside the 18.44 refinement. With an experiment that keeps them (`experiment_monitor.patch`, step 42 scratch) sound reaches the device but doesn't match the source files and stops after a few seconds (streams not refilled: the APU interrupt never reaches DSOUND, `ISR runs 0`). Verify with `tools/diag/loopcap.exe` and the WAV clips in step 42's `runs/`.)
     4. Fog in `xbox_d3d8`'s combiner and program paths.
     5. DrawVertices in the mirror (snow, box breaks).
     6. Water / Wa-Wa: dot-product texture modes.
     7. Flat shading, pause still, ~~present pacing~~ (18.45: DXGI interval 4 on the 240 Hz display), load times, Aku Aku.
     8. The worker stacks inside game data (18.45): move the runtime's worker slices out of `0x780000` (a runtime layout change: needs a decision), then see whether any unexplained glitch goes with it.
   - **Earlier, in order (step 40):**
     1. *Done (18.43): Rhys's playtest says the Nitro Switch Box no longer hangs and Arctic Antics plays start to finish.* Retest Rhys's report on the current build: Arctic Antics crashed or hung when he hit the Nitro Switch Box (build before `3648de8`, 25 minutes in, which is also when the kernel-call counter was about to wrap). The 18.41 retest ended when Windows Terminal crashed; the game log had no `[CRASH]` line. If it recurs, grab the `[CRASH]` block.
     2. *Bosses: Drain Damage and the final boss loaded in 18.43 (by poking a pad's level byte); heap not checked.* The bosses (on the warp rooms' boss byte, not a pad) and room 6 (bonus levels, upstairs) haven't been loaded; check them and their heap with `levelheap.py`.
     3. DSOUND: natural-end voice idles (a voice that ends without being released) still never reach DSOUND, as before 18.42; and the emulated APU's `VOICE_OFF` leaves voices in its list (`xboxrecomp/` behaviour, not changed).
   - Mirror gaps the demos show: a render target the bridge describes as 256x32768 format 0 (`snow_m`), and a combiner that `xbox_d3d8` can't compile (`r_ef`).
   - Still to do for S7: the vertex cache for program draws (below); the vblank count at ~32/s is harmless at 60 fps but slows the splash's timed wait.
1. **Seeing the game: Option B as a mirror bridge (18.31). S1 (18.32), S2 (18.33), S3 (18.34), S4 part 1 (18.35), fixed-function indexed draws (18.36) and S4 part 2 (18.37) done. S4 is complete; S5 part 1 (vertex programs, 18.38) and part 2 (cube maps and texel addressing, 18.39) done. What's left of S5 is below.**
   - **S5 part 2 (18.39, `24e019f`, `718cf64`): nothing is skipped any more from splash to hub.** Cube maps upload (6,657 binds, 0 skips) and 8,099 program draws that sample the CopyRects copy have their texel coordinates scaled by a microcode patch (`texel_patch`), 0 skips. The hub's monitor tubes show. Bridge off identical. `run_twoc.ps1 -Bridge` for playing with the mirror.
     - **Next for S5:**
       - **The water's reflection:** every cube draw is `1C743958` with texture modes PROJECT2D, DOTPRODUCT, DOTPRODUCT, DOT_RFLCT_SPEC (texm3x3vspec). `xbox_d3d8`'s combiner translator has no dot-product modes, so stages 1-3 sample nothing. It needs a local `xbox_d3d8` change (18.32 rule: no `twoc/` work-around exists for this one).
       - **Crash's blob shadow** (xemu has it). Not a cube or copy draw. Log ZBIAS/solid offset and a screen box for program draws in the drawlog, then find it. `xbox_d3d8` has no depth bias.
       - The menu vortex (18.38), not looked at in 18.39.
   - **S5 part 1 (18.38, `7af9725` + follow-up; translator `a0b306d` local in `xboxrecomp/`): vertex programs are mirrored.** Splash to hub: 162,741 program draws, 0 failures, 8 programs; the vertex-program probe is exact on 56 of 56 decidable vertices (`vscheck.py --probe`). Crash (intro, hub spawn, hub) and the menu's logo prop reach the real window; bridge off identical.
     - Program from the shader object's upload (`+0x114`), declaration at `+0x14 + 16 r` through D3D's attribute map `0x1B11C8`, constants from the shadow `+0xB98` plus c58/c59 (the viewport scale/offset `0xFE2B0` pushes; computed in `vp_constants`). Every register is fetched and expanded to a float4 on the CPU, so stream 1 interleaves.
     - ~~S5 part 2: copy-sampling program draws, cube textures~~ done in 18.39. Still open from this list: the menu vortex (dark with a bright rim; xemu: opaque blue whirlpool; details in 18.38) and Crash's missing blob shadow.
     - Cost: ~16 µs a draw, ~6% of wall time in the hub (rough). S7 wants a vertex cache for program draws.
   - **S4 part 2 (18.37, `ae718cb`): immediate mode and CopyRects are mirrored.** Splash to hub: 338,292 draws mirrored, 37,735 of them immediate, 3,300 copies, 0 failures; mirror time in the hub down to ~2.8% of the interval (rough). The menu's particles and the hub's portal glows show. The menu's planets are drawn but stay outside the view. Bridge off identical.
     - SetVertexData2f/4f only store (`D3DB_DATA_CALL`, `d3db_vertex_data`); End builds the FVF's vertices and draws them. The game's only caller is `0x38300` (DIFFUSE as R, G, B, A floats; TEXCOORD0 as u, v).
     - CopyRects: the whole back buffer into a 640x480 linear A8R8G8B8 texture, from the intro's LOADING screen on. The host keeps the copy as R8G8B8A8 for that Data. **Only vertex-program draws sample it**, so it matters from S5 on. Linear textures are addressed in texels on the NV2A, not 0-1: check what those draws pass.
     - "www.t-tales.com" is complete: 15 glyphs, FVF 0x152, all mirrored. Its uneven letters are the game's per-letter scale wave. 18.36's "TT logo glyph missing" was a misreading.
     - Texture signatures are remembered per guest frame (a 256 KB atlas hashed per draw had doubled the mirror's cost); a guest lock drops them.
   - **Fixed-function indexed draws (18.36, `0bf16db`): the LOADING text, the title animation, the menu, name entry and the hub's world now show on the real window.** 264,498 indexed draws mirrored splash to hub, 0 failures; bridge off identical.
   - **What's still missing** (`[D3DB] host draws`; `--skiptex` shows what each kind draws):
     - ~~Vertex programs~~ mirrored since 18.38; ~~those that sample the hub's CopyRects copy~~ since 18.39. No vertex-program immediates from splash to hub.
     - ~~Draws with a cube texture~~ mirrored since 18.39, but their dot-product texture modes aren't drawn (above).
     - Nothing is counted as not mirrored from splash to hub since 18.39.
   - **Fixed-function facts to build on (18.36):**
     - Lights: array at device `+0x390` (0x90 bytes each, D3DLIGHT8 first), enabled list from `+0x398` linked at `+0x8C`. Transforms at `+0x750` (XDK order), material at `+0x9F0`.
     - **Lighting is untested against the game:** no fixed-function draw from splash to hub has LIGHTING on (census `ff` key). `xbox_d3d8` lights a draw whose colours all come from the material; a vertex-colour source (the menu's setting: ambient and emissive from COLOR1) is lit on the CPU in the bridge (`cpu_light`). Check both against the first level that lights fixed-function geometry (the `[D3DB] host fixed function` line counts them).
     - `xbox_d3d8` gaps worked around: lighting ignores COLORVERTEX and the material sources; DrawIndexedPrimitiveUP doesn't convert fan/quad indices and ignores MinVertexIndex; texture matrices ignore TEXTURETRANSFORMFLAGS; `fvf_texcoord_size` misreads the texture-coordinate formats; normals are always normalized.
     - Cost: the mirror is 5-6% of wall time in the hub (was 0.3-0.4%), mostly two immutable D3D11 buffers per indexed draw inside `xbox_d3d8`. Needs a ring buffer or a vertex-buffer cache by S7. *(18.37: ~2.8% in the hub, 9.9-10.7 µs a draw, after texture signatures became per frame; much of the 5-6% was hashing textures of up to 256 KB at every draw. Rough: not measured back to back. The per-draw buffers remain.)*
   - **S4 facts to build on (18.35):**
     - A host render target per guest Data: level 0 plus a same-size depth buffer; a texture with that Data samples it.
     - Two `xbox_d3d8` gaps are worked around in `twoc/`:
       - XYZRHW is mapped through the back buffer's size, so pre-transformed vertices are rescaled to the viewport;
       - MIPFILTER NONE still mips, so such a stage gets level 0 alone.
     - `translate_ps` re-encodes the XDK combiner def for `xbox_d3d8` (its field order differs in eight ways, listed in 18.35).
     - Blue-to-alpha has no route there (it's counted). It's the first candidate for a local `xbox_d3d8` fix under the 18.32 rule once a mirrored draw needs it; shader 5's use of it is harmless.
     - P8 is expanded through the stage palette (device `+0xA88`) on upload.
     - Checks:
       - `pscheck.py` (`--psprobe`): the title's 6 shaders, 24/24 exact;
       - `rtcheck.py` (`--rtlog`, `--dump-rt`): 100% within 8 in trilinear;
       - `--texcheck`: the 19 P8 textures decode correctly.
     - The executor isn't a reference for render targets or the title/menu: it rasterises swizzled targets as linear rows, and 3D as screen space.
     - Real-window grabs: `tools/diag/wgcgrab.cpp` (Windows Graphics Capture). It works while other windows cover the bridge window and takes no focus. With `drive.py --exe=` (a renamed copy) its frame counter prints -1 (18.36).
     - No texture op above 11 is used from splash to hub.
   - S3 facts to build on (18.34):
     - The state translator (`translate_rs`/`translate_tss`, `xl_*` in `d3d_bridge.c`) covers every state `xbox_d3d8` reads. Texture ops above 11 follow the XDK order from memory, not verified: each use is counted as a "texop" gap, so check the first one seen.
     - `xbox_d3d8` reads a texture argument of 0 as "default", so plain `D3DTA_DIFFUSE` on stage > 0 has no route yet (counted).
     - `xbox_d3d8` recreates 4 sampler states on every draw, and the texture signature hashes whole textures up to 256 KB. Both are cheap at S3's counts; revisit for the hub.
     - The executor point-samples and ignores vertex colour, and its dumps are mid-frame: compare with `splashcheck.py`-style references, not pairing by time.
   - S2 facts to build on (18.33):
     - One bridge lock around every host call, and a window thread that owns the window and pumps its messages.
     - Presents go through a flip-model swap chain of the bridge's own, because blt-model presents never reach the screen on this machine. `xbox_d3d8` renders into its own swap chain on a hidden window, and each present copies its back buffer across.
     - Threads: the loading screens hold `0x4EA440` for all their D3D calls, and nothing interleaves with them. No D3DX draw ran from splash to hub.
     - The gamma ramp isn't mirrored. (Render-target clears are, since 18.35.)
   - **S7 needs (measured in 18.33):**
     - A vertex cache for program draws: every draw fetches and expands its vertices on the CPU (18.38).
     - ~~A ~59.94 Hz limiter at the host present~~ done (18.40, `1ee8de2`).
     - ~~A real `MmFreeContiguousMemory`~~ done (18.34, `dd4be13`): with the executor off the arena stays at 30.5 MB over 5 menu → demo cycles.
     - ~~**A general-heap leak of 1,200 KiB (640×480×4) per level load**~~ fixed in 18.40 (`44d29e9`): worker TIBs, never freed (18.34). With the executor off, menu → demo cycles then run the 48.5 MiB heap out at ~112 s: a 1,228,848-byte request fails, then a null function-pointer loop and a stack overflow. The game never frees the block through `NtFreeVirtualMemory`. Find what allocates it.
   - Census facts to plan around (18.32):
     - render-to-texture (six 128x128 targets) every frame from the title on, and CopyRects back buffer → texture in the hub. Both move up to S4. *(Corrected by 18.35: in the title and menu the targets are drawn once, at the title load, and again at each level load; the hub draws to them every frame.)*
     - streams 0+1 from the intro on;
     - DrawVertices and the `0xFEC00` fast path never ran;
     - no readback needed;
     - VS constants (~6800 a frame in the hub) and immediate data must be buffered in the wrappers.
   - xemu references: `twoc/reference/` (the user captures them; gitignored).
   - Earlier plan details, kept for reference:
   - **Design:**
     - Native D3D keeps running.
     - 15 wrappers (`sub_X` calls the lifted `sub_X_gen` first, then `xbox_d3d8`) render from host-side state. Everything else is read at draw time from D3D's own shadows (`D3D__RenderState` `0x10EE18`, `D3D__TextureState` `0x10EC18`, the device at `0x10C110`).
     - Guest memory keeps the native XDK objects as the only representation.
     - Gated by `RECOMP_D3D_BRIDGE=1`, so the default run is unchanged.
   - **Decisions (18.31; all three taken, 18.32):**
     1. Adopt the mirror design.
     2. `xbox_d3d8` gaps: work around in `twoc/` first; local `xbox_d3d8` commits only if a translator bug has no work-around.
     3. A 3D reference (xemu captures of the intro and the hub spawn).
   - **S1 (done, 18.32):**
     - Add the 15 `_gen` wrappers, log-only, and run a census over the splash, title, intro and hub: call counts, primitive types, FVFs, texture formats, RS/TSS indices used, render targets, lock/readback use, draw threads.
     - Confirm the probable identities (`0xFD680`, `0xFEC90`, `0x1012CC`/`0x10191E`, `0xFDC90`).
     - One run with `RECOMP_PB_EXEC` unset, to confirm the fence mirror alone still gets to the menu.
   - **Then:**
     - S2: host device, Clear, Swap (done, 18.33).
     - S3: the splash, compared with executor dumps (done, 18.34).
     - S4: title and menu (part 1 done; fixed-function indexed draws done in 18.36; part 2, immediate mode and CopyRects, done in 18.37).
     - S5-S6: 3D.
     - S7: executor off, fps.
     - S8: buffer.
   - Plan, entry table and `xbox_d3d8` gap list: `step29_option_b_plan.md`.
2. **Frame rate. Executor perf work is dropped (18.31): Option B replaces the executor.** The intro runs ~47-50 fps and the hub ~10 fps on the executor.
   - Dropped: profiling the hub's 10 fps on the executor. S7 measures fps with the bridge instead.
   - Done (18.30): `note()`/`note_unhandled()` O(1) (`fe406e4`), identical output checked on 348 M lookups; intro 23.1 → 46.9 fps.
   - Done (18.29): the per-method `getenv` is cached (`694cb98`) and the per-kick `DMA_PUT` line is rate-limited (`5012b2d`). The intro cutscene went from 6.8 to 24.0 fps, and the title without `-FastTitle` from 226 s to 129 s.
   - **Proposal 3 (hand-written KickOff): not now.** The gain is capped at the game's ~5% share. It's also unsafe: the ack thread sets GET = PUT *before* it executes a segment, so running ahead could overwrite unexecuted pushbuffer. Fixing that is a behaviour change in `xboxrecomp/`.
   - **Watch:** `[VMSTAT]` (18.42): zone live, peak, largest free range, and "to the heap" (regions the zone couldn't hold; 0 so far). *(Before 18.42: the bump high-water mark was 93.5% from the intro on, most of it no-split slack.)*
   - **Also open:** the timer thread's DPC poll `sub_00106720` (12.9% of a core after 18.29, was ~45%) and the D3D vblank count advancing ~30/s instead of 60; ~~the 2 x 5 s DSOUND voice-off waits~~ (fixed in 18.42).
   - Test with `run_twoc.ps1 -FastTitle -AutoStart -Seconds 180`: about 45 s to the intro cutscene, unattended.
   - Done: the 18.26 stderr deadlock (cooperative suspend, 18.27); the `NtFreeVirtualMemory` leak and intro OOM (18.28); the executor overhead (18.29); the survey lookups and the hub DSP hang (18.30).
   - Done (18.33): the 18.32 hub-load deadlock (`800f27b`). A suspended thread parks only once it owns no guest critical section. `twoc/tools/diag/step31_suspend_stress.patch` forces the old timing, to re-check the fix after a change. It was also what stopped step 30's no-executor runs at the attract demo's load.
   - **Also watch (18.30):** two more DSOUND spins on an APU voice bit (`0x13BE31`, `0x13C479`), not hit yet. ~~Crash can start in a hub zone that captures the stick~~ identified in 18.41: the spawn is the Load/Save pad, and standing on it opens the LOAD/SAVE menu.

   Earlier XPP history, superseded: **(18.16/18.17/18.18) — needs a real decision, both routes scoped and both nontrivial.** Confirmed with a real controller connected (18.17): detection works (`xbox_InputIsConnected(0)=1`), but `sub_000EB5A0(0)` still returns `slot[0]=0x00000000` because `sub_00154249`'s own lookup (`sub_001547DA` against the host-controller-context table at `0x153404`-`0x15340C`) never finds a registered controller. 18.18 scoped the "fabricate a fake table entry" shortcut and found it requires 2-3 fabricated structs, two untraced functions (`sub_00154B69`, `sub_00155C28`), and a hand-written replacement for half of `sub_001554A1` (564 bytes) to route around a transfer-completion wait that can never succeed — not meaningfully smaller than real OHCI emulation. Choose: invest in one of the two (real OHCI emulation benefits every USB path; a hand-written pipe-object builder is narrower but still nontrivial), or set input aside for now.
   Earlier Option B history, superseded by 18.31: **NV2A vertex programs / Option B (18.19/18.20/18.21).** 18.20 confirmed by direct observation that bridging `CreateDevice` alone crashes immediately (a host-shaped `xbox_d3d8` device object and the native push-buffer-shaped object every other D3D function still expects cannot coexist at the same address) -- there is no safe, additive subset to bridge one at a time; the crashing bridge was reverted, repo re-verified clean. 18.21 then scoped the resulting all-or-nothing batch over all 99 external entry points using the disassembler's full xref database: they split into CreateDevice itself (done once already), 4 shared-plumbing entries gating one object/refcount-model decision (33% of call sites), ~84 entries that become mechanical (~1 line each) once an NV097 method-number lookup table is built, and **10 large entries needing CreateDevice-class individual tracing** -- the real cost driver. Estimate: **roughly 10-15x CreateDevice's own effort**, concentrated in those 10 functions and the object-model decision. **Next actual step here, if pursued: trace the 10 Tier D-big entries (list with addresses and current best guesses at identity in 18.21 / `step19_d3d_wrapper_cluster_scoping.md`) and resolve the Tier B object-model question -- both still open, neither started.** Otherwise, explicitly weigh setting this aside (it's now sized well enough to make that call on purpose rather than by default).
3. DSOUND without a DSP: the effects image (`dsstdfx.bin`) is acknowledged but never run (18.25). A real GP/EP DSP56300 would be needed for reverb/effects.
4. Later: the rest of DSOUND audio (scoped in 18.43), then the wish list (60 fps, 16:9, surround). Also still open: 13.8k+ unhandled NV2A methods, `NtFreeVirtualMemory` failures (18.4; explained in 18.27, fixed in 18.28), the `in al, dx` in `sub_00106720`, and the executor's fixed 10 s dump cadence.
