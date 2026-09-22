# Recomp / Decomp / PC-Port Reference Notes

Compiled Sep 21, 2026 from a research chat. Facts come from READMEs, wiki pages and search snippets read that day. Star counts and project status change quickly, so re-check before relying on them. Anything marked **(unverified)** is a link I never opened.

---

## 1. The Crash project at a glance

- **Goal:** a native Windows PC version of *Crash Bandicoot: The Wrath of Cortex* (the **Xbox** version) using static recompilation, worked on together with Claude Code.
- **Wish list:** stable 60fps (xemu currently gives a choppy 30-40fps), 16:9 widescreen, surround sound, mouse look.
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
6. Then graphics debugging, then features (60fps, 16:9, surround, mouse look).

**First prompt to paste into Claude Code** (from `crash/`):
```
I want to statically recompile the original Xbox version of Crash Bandicoot:
The Wrath of Cortex into a native Windows exe, using the xboxrecomp toolkit in
./xboxrecomp. Goals: playable, stable 60fps, widescreen (16:9), surround sound,
mouse look. I own the disc; the extracted files are in ./game_files (never
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

### Mouse look
- **Easy:** convert mouse movement into right-stick input (Kameo RePowered does this, WASD as the left stick).
- **Medium:** true mouse look by finding the camera's yaw/pitch and writing to it from a hook.
- **Caveat:** reviewers describe fixed camera angles, so a free camera may not exist for much of the game. Check once the game runs.

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
7. How much of the camera is fixed vs free (matters for mouse look)?
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
- Widescreen, 60+ fps and mouse look need work: 640×480 is hard-coded, the timer runs faster than real time if unlimited, and the camera and projection are not yet found.

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

## Start here tomorrow (Sep 23, 2026)

**Current state.** The recompiled game builds and runs (`twoc/build/Release/crash_twoc.exe`). Run it with `powershell -NoProfile -ExecutionPolicy Bypass -File twoc\run_twoc.ps1` (add `-DumpDir dumps` for framebuffer BMPs). It gets through XAPI/CRT startup, D3D and DSOUND init, the splash screen, and the title/logo level loads — and now runs a full 90 s with no crash and no freeze, past `logos2.cut` into particle-system and cubemap loading, with the picture actively changing (draws 3,186 → 16,000, 924,000 triangles rasterised, no longer the frozen green quad). The `twoc/` fixes so far: the D3D fence mirror in `main.c` (18.1), 52 seeded function starts in `seed_functions.json` (18.6), a hand-written D3D interrupt DPC `sub_00106E60` with vblank forced on (18.7), a hand-written CRT memmove `sub_000F5DD0` (18.8), a hand-written `GetExitCodeThread` (`sub_000ECB2B`, 18.9), and a hand-written null-interface guard on `sub_000B9890` for the DSOUND 3D-listener chain (18.13). `xboxrecomp/` has one small additive change — a `xbox_bridge_resolve_handle` accessor, committed locally only in that clone (commit `52531ac` on `766ecef`), not pushed (see the rule refinement at the top of section 18). To regenerate: `twoc\analyze.ps1` (disasm with the seeds), then `twoc\lift.ps1`, then `cmake --build twoc\build --config Release --parallel`.

**Known, accepted gap (not a blocker).** `*(0x427A58)`, the cached DSOUND 3D-listener interface pointer, is never populated by real game code (traced exhaustively in 18.10-18.12; no write to it exists anywhere in the binary, and DSOUND's interrupt-connect routine doesn't look like it's what's stuck — 18.13). The 18.13 guard makes the per-frame 3D-listener maintenance chain a no-op while that pointer is null, instead of crashing. Effect: 3D audio positioning silently does not update. Other audio (music, non-positional SFX) is unaffected. Revisit properly with an unbounded trace when there's time; the real setup call was never found, only worked around.

**Next step.**
1. Run longer than 90 s to see what the game does next — no new blocker was hit within the watchdog window; the game was still actively rendering when it fired.
2. Revisit the real DSOUND 3D-listener setup call when there's time for a proper, non-time-boxed trace (18.13's guard is a known workaround, not a fix).
3. The NV2A executor has no vertex-program support (the top unhandled methods are `SET_TRANSFORM_PROGRAM` / `_CONSTANT` uploads, viewport, clip), so world geometry is skipped. Now that the game runs longer, this is the next likely ceiling on picture quality/correctness — decides whether the D3D route in section 15.1 stays.
4. Later: XPP input, the rest of DSOUND audio, then the wish list (60 fps, 16:9, surround, mouse look). Also still open: 13.8k+ unhandled NV2A methods, `NtFreeVirtualMemory` failures (18.4), the `in al, dx` in `sub_00106720`, and the executor's fixed 10 s dump cadence.
