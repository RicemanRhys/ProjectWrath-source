# Sharing Crash TWOC: the checklist

Step 107 (notes 18.110). This is a list of what to look at before the game goes to other people, in plain words.
**It is not legal advice and makes no legal decisions.** Where something is a legal question it says so; those are
for a lawyer (ideally one who knows software licensing and game emulation or porting).

The Share package (`make_package.ps1 -Target Share`) is built for sharing: it carries no game files, the player
supplies their own disc image, and the first start sets the game up (notes 18.110). Whether and how to share it is
still your call, after the points below.

## 1. The LGPL audio code from xemu

**What it is.** The Xbox audio processor emulation (`xboxrecomp/src/apu`) comes from xemu and is under the GNU Lesser
General Public License, version 2.1 or later (`xboxrecomp/NOTICE`, `LICENSES/`, and part 7 of
`THIRD_PARTY_LICENSES.txt`). It is compiled into `crash_twoc.exe` (linked statically). This build's copy has local
changes: xboxrecomp is 41 commits ahead of its upstream (`origin/main`), never pushed, 13 of them in `src/apu`.

**What the LGPL asks for, in short** (read the licence itself: section 6 is the one about programs that use the
library): someone who gets the program must be able to get the library's source as it was built, including your
changes, and must be able to change that library and rebuild or relink the program with their changed copy.

**What a recipient would need, concretely:**

- the source of `xboxrecomp/src/apu` exactly as built, with the local commits;
- the rest of xboxrecomp as built (the APU links against it), and its build files;
- twoc's own source and build files (`twoc/src`, `CMakeLists.txt`, the lift scripts), so the whole exe can be
  rebuilt with a changed APU;
- build instructions;
- the licence texts (already in `THIRD_PARTY_LICENSES.txt`) and a written offer or the source itself with the
  program.

**The script.** `twoc/tools/package/make_source_package.ps1` makes that zip (default
`Desktop\Crash\SourcePackage\crash-twoc-source_twoc-<commit>_xboxrecomp-<commit>.zip`): twoc's source and build
files and xboxrecomp at the commits built (from git, so uncommitted changes are left out and it warns), the 41 local
commits again as patch files, `SOURCE.txt` (what is in and what is left out) and `BUILD.txt` (the steps). It runs the
package audit on the folder before zipping (no game files, no trace of this PC). **It publishes nothing.**

**Left out of it, on purpose:** the recompiled game code (`twoc/src/recomp/gen`, 32 MB of C made from the game's
`default.xbe` by `lift.ps1`), the disc's files, `twoc/docs/option_b` (a disassembly listing of the D3D library inside
the game's program), the test harness and the project notes.

**Open questions (yours, or a lawyer's):**

- With static linking, rebuilding needs the rest of the program too. A recipient can only rebuild the exe with
  their own copy of the game (the recompiled code comes from it, `BUILD.txt` step 3). Is that enough, or would the
  object files of the rest of the program have to be offered (which contain the recompiled game code; see part 3)?
- An engineering option that changes the question: build the APU as its own DLL (`xbox_apu.dll`), so a recipient
  can swap in a changed copy without rebuilding anything else. Work: a CMake change, an exported C interface
  (`mcpx_apu_init_standalone`, the MMIO hook, the output functions; about a dozen functions), a DLL in the package,
  a day or two with testing; no change to how the game plays. Not done: it is a choice to make first.
- Where the source would be hosted (with the package, or a link that stays up for as long as the program is
  shared), and pushing or publishing xboxrecomp's local commits (a fork) so upstream and recipients can see them.

## 2. Licence texts

`THIRD_PARTY_LICENSES.txt` (made by `twoc/tools/package/third_party_licenses.py` from each component's own licence
file) covers Dear ImGui and stb_truetype, SDL 3 (with hidapi and the SunPro libm notice), XeGTAO, SMAA, volk,
Vulkan-Headers, xboxrecomp with its NOTICE and the LGPL text, and DXVK (only for the DXVK test folder, which is not
part of the Share package).

**Gaps:**

- **The Visual C++ runtime** (`vcruntime140.dll`, `vcruntime140_1.dll`, shipped next to the exe): Microsoft lets
  them be redistributed under the Visual Studio 2022 licence terms' "Distributable Code" section. Part 8 of the
  file quotes the published redistribution list (REDIST.txt) and the URL, **not the section's own words**: Visual
  Studio doesn't install the licence terms on this PC and Microsoft's page builds them with script (notes 18.96).
  To finish: open https://visualstudio.microsoft.com/license-terms/ (the Visual Studio 2022 Community or
  Professional terms, whichever is installed), copy the "Distributable Code" section into part 8 of
  `third_party_licenses.py`, and run it again. Or ship no DLLs and ask players to install the "Microsoft Visual C++
  Redistributable for Visual Studio 2015-2022" (x64) instead (one more step for them, no text needed).
- **Windows components** the exe uses (Direct3D 11, DXGI, the HLSL compiler, XInput, XAudio2, Media Foundation,
  bcrypt): part of Windows, not shipped: nothing to include.
- **Fonts**: the setup screen and the F1 menu load Segoe UI (and, when a path needs it, Microsoft YaHei, MS Gothic
  or Segoe UI Symbol) from the PC's own `C:\Windows\Fonts`; none is shipped.
- **This project's own code: no licence chosen.** `LICENSE.txt` in the Share package says so. Which licence to use
  is your decision (it has to sit with the LGPL part and the question in part 3). Not chosen here.

## 3. The recompiled code is derived from the game's machine code

`crash_twoc.exe` holds C translated, function by function, from the game's own program (`default.xbe`), compiled to
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

- The program's names say "Crash": the folder `Crash TWOC`, `crash_twoc.exe`, the window titles "Crash TWOC - D3D
  bridge (host)" and "Crash TWOC - Setup", the zip and folder names `SharePackage`. A neutral project name with a
  plain description ("a PC port of ... for your own copy") is the usual way fan projects keep clear of looking
  official.
- The package uses no logo, box art, screenshots or music of the game; the setup screen and README use the game's
  name as text only. Keep it that way in anything published (a web page, a video, an icon).
- The README and the setup screen say it is unofficial and not affiliated with the owners. Keep that wording
  wherever the program is offered.
- The game's own title screen and logos appear when it runs: they come from the player's disc.

## 5. The audit

`twoc/tools/package/package_audit.ps1 -Dir PACKAGE` checks a package before it goes anywhere, and fails loudly (red
lines, exit code 1):

1. no game file: no file whose name, or size and SHA-256, is one of the disc's 2,267 (the manifest,
   `twoc/data/disc_usa.tsv`), and no `.xbe`, `.iso` or `.xiso` at all;
2. no trace of this PC: the home folder's path, the user name in a `\Users\<name>\` path, or the PC's name, in any
   file name or contents (text and binary, 8-bit and UTF-16);
3. no logs, saves or dumps (`*.log`, `*.dmp`, `*.sav`, `*.xbx`, `*.pdb`, `*.zip`, `logs\`, `UDATA\`,
   `UDATA_backups\`, `TDATA\`), no setup leftovers, and no `crash_twoc.ini` in the Share package.

`make_package.ps1 -Target Share` runs it at the end and fails if it fails; `make_source_package.ps1` runs it before
zipping. The test packages (Legion Go, Nitro 5) carry `game_files\` for your own devices: the audit says so and skips
the game-file check inside it only. Run it again on anything you are about to send.

## 6. Logs and privacy

- The game writes `run_twoc.err.log` (the last run), `logs\` (earlier runs), `xbox_kernel.log` and
  `run_twoc.power.log`, all in its own folder, never anywhere else, and **sends nothing over the network** (the
  program has no network code).
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

To decide: where players report problems (a GitHub issue tracker, a Discord, an e-mail address) and whether you
want reports at all. Whatever the channel, ask for: the zip from `Collect logs.bat`, what happened and where in
the game, the frame rate setting, and whether it happens again. The logs' `[FILES]` line says whether the game files
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
