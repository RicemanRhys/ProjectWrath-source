# Sharing Project Wrath: the checklist

Step 107 (notes 18.110), updated in steps 108 and 109 (notes 18.111, 18.112). This is a list of what to look at before the game goes
to other people, in plain words. **It is not legal advice and makes no legal decisions.** Where something is a legal
question it says so; those are for a lawyer (ideally one who knows software licensing and game emulation or porting).

The Share package (`make_package.ps1 -Target Share`) is built for sharing: it carries no game files, the player
supplies their own disc image, and the first start sets the game up (notes 18.110). Whether and how to share it is
still your call, after the points below.

**Decided in step 108 (your choices):** this project's own code is MIT (part 2); the LGPL audio code is its own DLL,
`xemu_apu.dll`, shipped with its source (part 1); the Visual C++ runtime is linked statically, so no runtime DLL
ships (part 2). **Decided in step 109:** the project's name, Project Wrath (part 4). **Still open:** the copyright
holder's name (a placeholder in `twoc/LICENSE`), where people report problems (ISSUES_URL, a placeholder in
`twoc/ISSUES_URL.txt`: part 7), whether the name is free to use (searches that are yours or a lawyer's: part 4), the
recompiled code in the exe (part 3, a lawyer's question).
**`docs/RELEASE_CHECKLIST.md`** is the short, ordered list of what to do before giving the Share package to anyone.

## 1. The LGPL audio code from xemu: xemu_apu.dll (decided in step 108)

**What it is.** The Xbox audio processor emulation (`xboxrecomp/src/apu`: `apu_core.c`, `apu_vp.c`, `apu_dsp.c` and
their headers) comes from xemu and is under the GNU Lesser General Public License, version 2.1 or later
(`xboxrecomp/NOTICE`, `LICENSES/`, part 7 of `THIRD_PARTY_LICENSES.txt`). This build's copy has this project's
changes: 13 local xboxrecomp commits in `src/apu`, never pushed upstream.

**Your choice (step 108): the audio code is its own DLL, shipped with its source and licence, so a recipient can
swap it.** Since step 108:

- **The DLL.** `xemu_apu.dll`, beside `ProjectWrath.exe` in every package. It is built by `twoc/apu_dll/CMakeLists.txt`
  from xboxrecomp's APU files, unchanged (the xemu-derived files, plus xboxrecomp's own MIT `apu_xaudio2.c` and
  `apu_mmio_hook.c`), and this project's `xemu_apu_dll.c` (its one export, MIT). The exe no longer contains any of
  that code (it links xboxrecomp's other libraries one by one, without `xbox_apu`), and the DLL contains nothing
  else of the exe.
- **The interface** (`twoc/apu_dll/xemu_apu_api.h`, version 1): one export, `xemu_apu_get_api(version)`, handing the
  exe a table of C functions: start the APU over the guest's memory, a register access the exe's exception handler
  caught (decoded and done in the DLL), register read and write by offset, the music / effects gains, the HRTF,
  the output's channel count. Nothing calls back into the exe.
- **Loading.** At the start, from the exe's own folder by full path (never the current folder or PATH), its
  interface version checked. Missing, damaged or another version: a message box in plain words (what is wrong,
  the file's full path, what to do) and the game doesn't start. The log names the DLL loaded, with its size and
  SHA-256 (`[APU-DLL]`).
- **Its source.** `twoc/tools/package/make_apu_source_package.ps1` makes `xemu_apu-source_xboxrecomp-<commit>_twoc-<commit>.zip`:
  the DLL's files exactly as committed (it refuses if any has changes not committed), this project's changes to
  them (the 13 local commits as patches, listed in `NOTICE.txt`), the LGPL-2.1 text and the two MIT texts,
  `NOTICE.txt` (xemu's attribution as xboxrecomp gives it, each file's licence, what was changed, the commits),
  `README.txt` (how to rebuild and swap) and `build.cmd` (Visual Studio 2022's compiler through CMake). The Share
  package carries it in `Source\`. Built with the same compiler, the rebuilt DLL is byte for byte the shipped one
  (`/Brepro`; proved in step 108 from the unpacked zip alone).
- **How a recipient replaces it:** change the source, run `build.cmd`, close the game, copy the new `xemu_apu.dll`
  over the one next to `ProjectWrath.exe`. Nothing else is rebuilt or relinked.

**What `make_source_package.ps1` is for now.** It still makes a zip of the whole program's source (twoc's source and
build files, xboxrecomp at the commits built, its local commits as patches), without the recompiled code, the
game's files, `docs/option_b`, the harness and the notes. It is **no longer the LGPL mechanism** (the DLL's own zip
is); keep it for developers or for a public repository later. It includes no game-derived generated code: the
recompiled code is made by each builder from their own disc (its `BUILD.txt`, step 3).

**Still open (yours):**

- Where the source would be hosted besides the package (the zip travels with the Share package; a public link is
  optional), and whether to publish xboxrecomp's local commits (a fork) so upstream can see them.
- Whether to offer the changes to xemu's code upstream (to xboxrecomp): not needed for the licence, a courtesy.

## 2. Licence texts

`THIRD_PARTY_LICENSES.txt` (made by `twoc/tools/package/third_party_licenses.py` from each component's own licence
file) covers Dear ImGui and stb_truetype, SDL 3 (with hidapi and the SunPro libm notice), XeGTAO, SMAA, volk,
Vulkan-Headers, xboxrecomp with its NOTICE and the LGPL text, and DXVK (only for the DXVK test folder, which is not
part of the Share package).

**What is not a licence text in the file, and why:**

- **The Visual C++ runtime: linked statically since step 108 (your choice 3).** `ProjectWrath.exe` and
  `xemu_apu.dll` are built with `/MT` (`TWOC_STATIC_CRT`, every target: the exe, xboxrecomp's libraries, SDL 3,
  the DLL), so they import no Microsoft runtime DLL and **no `vcruntime140*.dll` ships in any package**;
  `package_audit.ps1` fails if one appears, and `make_package.ps1` stops if the exe or the DLL imports one.
  Players need no Visual C++ Redistributable. The runtime code inside them is Microsoft's (Visual Studio 2022's
  MSVC 14.44 and the Windows SDK's Universal C Runtime); part 8 of `THIRD_PARTY_LICENSES.txt` says so and links
  Microsoft's terms (https://visualstudio.microsoft.com/license-terms/), without pasting text that couldn't be
  read from an official file here. The old gap (the "Distributable Code" section for shipped DLLs) no longer
  applies, as no DLL is shipped. Measured in step 108: no slower (cast_bug's start, the game and render threads),
  the same sound, the crash handler and logs as before; the exe 0.33 MB larger (14.78 MB) and the DLL 0.16 MB (0.23 MB).
- **Windows components** the exe uses (Direct3D 11, DXGI, the HLSL compiler, XInput, XAudio2, Media Foundation,
  bcrypt): part of Windows, not shipped: nothing to include.
- **Fonts**: the setup screen and the F1 menu load Segoe UI (and, when a path needs it, Microsoft YaHei, MS Gothic
  or Segoe UI Symbol) from the PC's own `C:\Windows\Fonts`; none is shipped.
- **This project's own code: MIT (step 108, your choice).** `twoc/LICENSE` holds the MIT text; its copyright line
  reads `Copyright (c) 2026 [NAME TO BE CHOSEN BY THE OWNER]` until you put your name (or a group's) there. That
  line is the one place the name is written: `make_package.ps1` copies it into every package's `LICENSE.txt`, the
  source zips carry the file itself, and `package_audit.ps1` warns loudly (yellow, "NOT READY TO SHARE") while
  the placeholder is there. `twoc/NOTICE` says what the MIT licence covers and what it doesn't (part 2a below).

### 2a. Whose code is what

What `twoc/LICENSE` (MIT) covers, and what keeps its own licence. Every third-party file is kept as it came, with
its own notice and licence file, and none is relabelled MIT (checked in step 108: each folder's `LICENSE*` file
and `README.twoc.txt`, the files' own headers, unchanged since they were added).

| Files | Whose | Licence |
|---|---|---|
| `twoc/src/*.c`, `*.cpp`, `*.h`, `crash_twoc.manifest` (47 files) | this project | MIT (`twoc/LICENSE`); see the two notes below |
| `twoc/CMakeLists.txt`, `lift.ps1`, `analyze.ps1`, `run_twoc.ps1`, `seed_functions.json`, `tools/` (package, capturekit, diag, the lift helpers) | this project | MIT |
| `twoc/data/disc_usa.tsv` | this project (file names, sizes and SHA-256 of the disc: no game data) | MIT |
| `twoc/docs/SHARING.md`, `twoc/NOTICE` | this project | MIT |
| `twoc/third_party/imgui` (Dear ImGui 1.92.9b, with stb_truetype / rect_pack / textedit) | Omar Cornut; Sean Barrett | MIT (stb: MIT or public domain), `LICENSE.txt` |
| `twoc/third_party/smaa` (SMAA.hlsl, AreaTex.h, SearchTex.h) | Jorge Jimenez et al. | MIT, `LICENSE.txt` |
| `twoc/third_party/xegtao` (XeGTAO.h, XeGTAO.hlsli) | Intel Corporation | MIT, `LICENSE.txt` |
| `twoc/third_party/volk` (volk.c, volk.h) | Arseny Kapoulkine | MIT, `LICENSE.md` |
| `twoc/third_party/vulkan` (Vulkan-Headers) | The Khronos Group | Apache-2.0 or MIT, `LICENSE.md`, `LICENSES/` |
| SDL 3.4.18 (fetched by CMake into the build folder, built static) | SDL's authors; HIDAPI; SunPro (libm) | zlib; HIDAPI BSD-style; SunPro's notice |
| `twoc/apu_dll/` (`CMakeLists.txt`, `xemu_apu_dll.c`, `xemu_apu_api.h`, `build.cmd`) | this project | MIT (the LGPL files it builds keep the LGPL) |
| `xboxrecomp/` (the toolkit: kernel, D3D8 on D3D11, input, the recompiler) | xboxrecomp's authors | MIT, `xboxrecomp/LICENSE` |
| `xboxrecomp/src/apu/apu_core.c`, `apu_dsp.c`, `apu_vp.c`, `apu_regs.h`, `apu_state.h`, `apu_debug.h`, `fpconv.h`, `apu_shim.h`, and `apu.h` (its header says it is extracted from xemu; NOTICE doesn't list it, so it is treated as LGPL too); `xboxrecomp/src/nv2a/nv2a_regs.h` (not used by the DLL) | xemu's authors (espes, Jannik Vogel, Matt Borgerson) | LGPL-2.1-or-later, `xboxrecomp/NOTICE`, `LICENSES/LGPL-2.1.txt`: in `xemu_apu.dll` only (part 1) |
| `xboxrecomp/src/apu/apu_xaudio2.c`, `apu_xaudio2.h`, `apu_mmio_hook.c`, `src/nv2a/qemu_shim.h`, `src/platform/xbox_winnt.h` | xboxrecomp's authors | MIT; built into `xemu_apu.dll` with the files above |
| `twoc/src/recomp/gen` (the recompiled code, made by `lift.ps1` from the game's `default.xbe`; gitignored) | translated from the game's program | the MIT licence is **not** claimed for it; part 3's open question |
| `twoc/docs/option_b` (a disassembly listing of the D3D library in the game's program) | from the game's program | not claimed; never packaged |
| the game's files (`game_files`, the disc) | the game's owners | not included anywhere; the player's own copy |

Two notes on `twoc/src`:

- `ao.c` follows XeGTAO's sample shaders (`vaGTAO.hlsl`) and ports `XeGTAO::GTAOUpdateConstants` to C; it names
  Intel's copyright in its header, and that part stays under XeGTAO's MIT notice (part 3 of
  `THIRD_PARTY_LICENSES.txt`). `aa.c`'s FXAA is written after Timothy Lottes' description (no code taken);
  `d3d_host.c` uses NVAPI's call ids (numbers from NVIDIA's MIT headers, no header code).
- `recomp_manual.c` holds hand-written C for some of the game's own functions (where the lifted code needed a fix
  or a hook). It is written here, but from studying the game's program, so it belongs to the same open question
  as the recompiled code (part 3).

**Licence headers in the source files:** not added; `twoc/NOTICE` is the directory-level note instead. A comment at
the top of each of the 47 files would be safe (no code changes), but it would say no more than `NOTICE` does for the
whole folder, and the owner's name (still a placeholder) would then be in 48 places instead of one. If you want
them later: a one-line `SPDX-License-Identifier: MIT` comment per file needs no name and can be added by a script.

## 3. The recompiled code is derived from the game's machine code

`ProjectWrath.exe` (the build's `crash_twoc.exe`) holds C translated, function by function, from the game's own program (`default.xbe`), compiled to
x64. The exe does not contain the game's data (levels, models, textures, sounds, music: the player's disc supplies
those), but its code is a mechanical translation of the game's code, and of the Microsoft XDK libraries linked into
the game (D3D, DirectSound, XAPI). **Whether distributing that is allowed is a legal question, for a lawyer.** The
ways projects deal with it, with what each would mean here:

| Way | Projects that do it | Work for this project | What players download | Risks and costs |
|---|---|---|---|---|
| **A. Ship the generated code compiled into the exe** (what the Share package does now) | Zelda64Recomp and other N64Recomp ports: the player supplies the ROM, which is checked by hash and read for its data; the released exe holds the recompiled code | None: done (the setup and the check are step 107) | About 14 MB | The exe contains a translation of the game's code; this is exactly the legal question above. Simplest for players by far. |
| **B. Compile it on the player's PC at the first start** | The Splinter Cell port, as you described it (not checked here) | Large: ship the lifter (now Python, through xboxrecomp's tools; would need an embedded Python or a rewrite in C) and a compiler (MSVC can't be redistributed; clang/LLVM can, with its own licence), the runtime as libraries, and a build step in the setup screen with progress, error reporting and a retry. Weeks of work, and a test on each kind of PC. | Several hundred MB (a compiler and libraries) | The first start takes many minutes (the generated code is 32 MB of C: about 5 x 190,000 lines; the desktop builds it in minutes, a handheld would take much longer); a compile can fail on a PC the setup can't predict; antivirus programs often distrust a program that compiles and runs code; no link-time or profile-guided optimisation unless it runs there too (PGO gave 1.4-4.7%, notes 18.98). The download holds no game code. |
| **C. Ship source only; players build it themselves** | Many decompilation projects | Small: `make_source_package.ps1` already makes the zip, minus `BUILD.txt` being tried on a clean PC | A source zip (a few MB) | Players need Visual Studio, CMake, Python and an hour; most won't. Nothing derived from the game is shipped. |

A fourth idea sometimes used: ship the exe's game-derived parts as a patch that only applies to a file made from
the player's own disc. Not studied here.

## 4. Naming and trademarks

"Crash Bandicoot" and "The Wrath of Cortex" are trademarks of their owners. Points to look at (decisions are
yours):

- **Decided in step 109: the project's name is Project Wrath** (notes 18.112 has the inventory of every name a
  player sees, renamed or kept, and why). The packaged exe is `ProjectWrath.exe` (Windows file properties: product
  "Project Wrath", description "Project Wrath - an unofficial fan port (not affiliated with the owners of the
  game)", no company), the window "Project Wrath", the setup "Project Wrath - Setup", `ProjectWrath.ini`,
  `ProjectWrath.*.log`, `%LOCALAPPDATA%\ProjectWrath`, "Play Project Wrath.bat", `ProjectWrath_logs_*.zip`, the
  folders `ProjectWrath-Share`, `ProjectWrath-LegionGo`, `ProjectWrath-Nitro5`. The name and the window title don't
  contain the game's name; the game is named only to say which disc is needed (the setup, the README's "What you
  need", `LICENSE.txt`'s point 5). The names follow the running exe's file name (`twoc/src/names.h`): the
  repository's own build, `crash_twoc.exe`, keeps the old names for the development tools. The audit (part 5,
  check 8) fails if the old names show in a Share package.
- **Still to do, not by these tools:** whether "Project Wrath" is free to use. Search the exact name on GitHub
  (repositories, organisations), the web (games, mods, apps, companies), the app stores, and the trademark
  databases: the USPTO's trademark search (USA), EUIPO's eSearch (EU), WIPO's Global Brand Database (elsewhere).
  Judging what a result means is a lawyer's job (or yours); nothing here decides it.
- The settings menu's help lines name the game's own character where a setting is about him (Character fur "Off
  for Crash only", the 3D sound help, the title animation), and the README names a few levels in its known
  quirks. They describe the game's content, not the project; whether to keep them is your call.
- The package uses no logo, box art, screenshots or music of the game; the setup screen and README use the game's
  name as text only. Keep it that way in anything published (a web page, a video, an icon).
- The README, the setup screen, the About tab and the exe's file description say it is an unofficial fan port,
  not affiliated with, endorsed by or sponsored by the owners of the game. Keep that wording wherever the program
  is offered.
- The game's own title screen and logos appear when it runs: they come from the player's disc.

## 5. The audit

`twoc/tools/package/package_audit.ps1 -Dir PACKAGE` checks a package before it goes anywhere, and fails loudly (red
lines, exit code 1):

1. no game file: no file whose name, or size and SHA-256, is one of the disc's 2,267 (the manifest,
   `twoc/data/disc_usa.tsv`), and no `.xbe`, `.iso` or `.xiso` at all;
2. no trace of this PC: the home folder's path, the user name in a `\Users\<name>\` path, or the PC's name, in any
   file name or contents (text and binary, 8-bit and UTF-16);
3. no logs, saves or dumps (`*.log`, `*.dmp`, `*.sav`, `*.xbx`, `*.pdb`, `*.muted`, `*.zip` except the source
   zips in `Source\`, `logs\`, `UDATA\`, `UDATA_backups\`, `TDATA\`), no setup leftovers, and no ini of the
   game's (`ProjectWrath.ini`, `crash_twoc.ini`) in the Share package;
4. the licences (step 108): `LICENSE.txt` is there and is the MIT licence with a copyright line, and
   `THIRD_PARTY_LICENSES.txt` is there. While the copyright line still holds `[NAME TO BE CHOSEN BY THE OWNER]` it
   **warns** loudly in yellow ("NOT READY TO SHARE") and ends "AUDIT PASSED WITH 1 WARNING(S)";
5. the audio library (step 108): `xemu_apu.dll` beside every `ProjectWrath.exe` (or developer `crash_twoc.exe`); in the Share package
   `Source\xemu_apu-source_*.zip`; every zip in `Source\` is unpacked into a temporary folder and audited the same
   way (it must hold the LGPL text and its notice);
6. no Microsoft runtime DLL (step 108: `vcruntime*`, `msvcp*`, `concrt*`, `vccorlib*`, `ucrtbase`,
   `api-ms-win-crt-*`);
7. where players report problems (step 109): `PACKAGE.txt`'s "Where to report problems" line, from
   `twoc/ISSUES_URL.txt`. While ISSUES_URL is the placeholder it **warns** loudly in yellow ("NOT READY TO SHARE"),
   as for the copyright name; an unfilled `@ISSUES_URL@` left in a text file fails;
8. the names before Project Wrath (step 109): in a Share package (and the source zip inside it), no `crash_twoc`,
   `CrashTWOC` or `Crash TWOC` (any case) in a file's name or in a readable text file. Not looked into: the exe and
   the DLL (binaries; the exe holds the old names on purpose, for the developer's build and to read an old
   `crash_twoc.ini` or `%LOCALAPPDATA%\CrashTWOC`), and the audio source zip's `src\` and `changes\` (xboxrecomp's
   LGPL files and their patches, exactly as built and committed: their comments and commit messages keep the old
   names). The allow-list: a line saying what the project was "formerly" called (none so far), and the source
   zip's `NOTICE.txt` lines that list xboxrecomp's commits as committed.

`make_package.ps1 -Target Share` runs it at the end and fails if it fails; `make_source_package.ps1` and
`make_apu_source_package.ps1` run it before zipping. The test packages (Legion Go, Nitro 5) carry `game_files\` for your own devices: the audit says so and skips
the game-file check inside it only. Run it again on anything you are about to send.

## 6. Logs and privacy

- The game writes `ProjectWrath.err.log` (the last run; `run_twoc.err.log` for the developer's `crash_twoc.exe`),
  `logs\` (earlier runs), `xbox_kernel.log` and `ProjectWrath.power.log`, all in its own folder (or
  `%LOCALAPPDATA%\ProjectWrath`), plus xboxrecomp's sparse Xbox hard-disk images in `%LOCALAPPDATA%\xboxrecomp`
  (a few MB on the disk, no saves), and **sends nothing over the network** (the program has no network code; the
  About tab's "Report a problem" only opens the player's browser on ISSUES_URL, when they press it).
- The About tab's **Copy build info** (step 109) puts a text block on the clipboard for a report: the build,
  Windows, the processor, the graphics card and driver, the display and refresh rate, the picture, the Frame rate
  setting, the settings changed from the defaults and the game files' check. It is scrubbed as Collect logs scrubs
  (the user's name, the PC's name, the game's, exe's, home and profile folders, any other path left), and logs how
  many it took out; nothing leaves the PC unless the player pastes it somewhere.
- They hold: the folder paths (with the Windows user name in them), the graphics card and driver, the display's
  mode, the processor, Windows' version, the power plan and battery, the controllers' names, the settings, and the
  game's technical messages. No account names, no files from outside the game's folder.
- `Collect logs.bat` zips them with a system summary and Windows' crash and power events. Its scrub replaces the
  home folder's path with `<user-home>`, the user name with `<user>` and the PC's name with `<pc>` in every text
  file of the zip, and lists what it replaced in `scrub.txt`. In the Share package it also leaves out the list of
  other running programs and the display test. It never sends anything; the README asks players to look inside
  before they send it.
- The setup screen looks for `.iso` files in the game's folder, Desktop, Downloads and Documents (one folder down)
  to list them; it reads only the image's header and `default.xbe`'s certificate, keeps nothing, and logs only the
  number found.

## 7. Support and bug reports

**Prepared in step 109, nothing published:** ISSUES_URL, one value in `twoc/ISSUES_URL.txt` (a marked placeholder
until you choose where reports go: a GitHub issue tracker, a Discord, a web form). The About tab shows "Report a
problem" only once it is a real `https://` address; the Share README says "Where to report problems: to be
announced" until then; the audit warns. `.github/ISSUE_TEMPLATE/` holds a bug report form (the build from Copy
build info, Windows, graphics card and driver, display and refresh rate, the Frame rate setting, the level and
where, whether it also happens at Game (60), what was done, what happened, what should happen, the Collect logs zip,
screenshots, and a "Verify game files" check box), a feature request form, and `config.yml` (blank reports off, a
link to the README's FAQ whose `OWNER/REPOSITORY` is to be filled in once the repository is published). No personal
information is in them. The Share README has "Reporting a problem" and a FAQ (setup, the frozen first logo, the
audio DLL message, SmartScreen for the unsigned exe, antivirus false positives, saves and logs). Whatever the
channel, ask for: Copy build info, the zip from `Collect logs.bat`, what happened and where in the game, the frame
rate setting, and whether it happens again. The logs' `[FILES]` line says whether the game files
were the supported disc's; the `[ABOUT]` line names the build. Expect most reports to be about the disc image (the
setup screen's messages name the common cases) and about slow PCs (the README's Frame rate part).

## 8. Minimum requirements

| | Measured | Not measured |
|---|---|---|
| Windows | Windows 11 (desktop, Nitro 5, Legion Go) | Windows 10 (the exe asks for nothing newer than Windows 10; the UTF-8 file names need 1903 or later) |
| Processor | Ryzen 7 7800X3D (desktop), Core i5-12500H (Nitro 5), Ryzen Z1 Extreme (Legion Go): all hold the game's 60 updates a second | anything slower than the Legion Go; 4 threads or fewer |
| Graphics | RTX 4080, RTX 3050 Laptop 4 GB, the Z1 Extreme's Radeon 780M | AMD Radeon and Intel Arc desktop cards, Intel's built-in graphics on their own; the game creates a Direct3D 11 device and asks for no particular feature level |
| Memory | the game's own use: 0.5-0.9 GB while playing (private bytes, notes 18.103) | the PC's total: "4 GB" in the README is an estimate |
| Disk | the game files 0.93 GB; the setup asks for 0.97 GB free (plus a margin); logs a few MB a run | |
| Disc image | the USA disc as a trimmed XISO (the user's own) | a real full ("Redump-style") dump: tested only as a synthetic one built from the XISO (notes 18.110) |
