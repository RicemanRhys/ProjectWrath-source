# Rebuilding Project Wrath on a fresh PC

How to get from an empty Windows PC to a working `crash_twoc.exe` (and the test harness) using only the backups:
the private GitHub repository `RicemanRhys/ProjectWrath-source` (or the `.bundle` files in `Desktop\Crash\Reports`),
the generated-code backup (`Reports\recomp-gen-backup`) or your own disc image. Step 116 (2026-10-26) tested every
step below on this PC, in a fresh folder; `REBUILD_DRILL.md` beside this file is that test's report.

## 1. What to install

The versions are the ones the 1.0.0 build used. Newer minor versions probably work too, but only these were tested.

| What | Version used | Needed for | Where to get it |
|---|---|---|---|
| Windows | 11 Home 25H2 (10.0.26200), 64-bit | everything | |
| Visual Studio Community 2022 | 17.14.41 (September 2026), build 17.14.37710.0 | the build (the compiler, MSBuild, the Windows SDK) | https://visualstudio.microsoft.com/vs/older-downloads/ (2022); fixed-version installers: https://learn.microsoft.com/visualstudio/releases/2022/release-history |
| - workload | "Desktop development with C++" (`Microsoft.VisualStudio.Workload.NativeDesktop`) with its default components | | in the installer, or import `twoc\docs\rebuild.vsconfig` (Installer > More > Import configuration): this PC's exact component list, less IntelliCode and Copilot |
| - the two components that matter | "MSVC v143 - VS 2022 C++ x64/x86 build tools" (`...Component.VC.Tools.x86.x64`): toolset **14.44.35207**, `cl` **19.44.35229**; "Windows 11 SDK (10.0.26100)" (`...Component.Windows11SDK.26100`): installed as **10.0.26100.7705** | | if a later VS update moves the default toolset, add the side-by-side "MSVC v143 ... (v14.44-17.14)" from Individual components |
| - MSBuild | 17.14.60.43110 (comes with Visual Studio) | the build: CMake's "Visual Studio 17 2022" generator drives it | |
| CMake | **4.4.3** (`C:\Program Files\CMake`); needs 3.24 or newer | configuring the build | https://cmake.org/download/ (https://github.com/Kitware/CMake/releases/tag/v4.4.3) |
| Ninja | not used (Visual Studio ships 1.12.1; the build uses MSBuild) | | |
| Git for Windows | **2.55.0.windows.3**, installer defaults (`core.autocrlf=true`) | cloning; the build's About line (commit and date); Git Bash for the harness | https://github.com/git-for-windows/git/releases/tag/v2.55.0.windows.3 |
| Python | **3.13.15**, 64-bit, from python.org | regenerating the code from the disc; the harness | https://www.python.org/downloads/release/python-31315/ |
| - pip packages | `twoc\tools\requirements.txt` (regenerating: `capstone==5.0.9`), `twoc\tools\diag\requirements.txt` (the harness: numpy, pillow, scipy, opencv, pefile, pytest, imageio-ffmpeg) | | PyPI, through pip |
| Windows PowerShell | 5.1 (built into Windows; 5.1.26100 here) | `analyze.ps1`, `lift.ps1`, `run_twoc.ps1`, the packaging scripts | built in; PowerShell 7 isn't needed or tested |
| LLVM (clang) | 23.1.1 | only the toolkit's own lifter tests (`python -m pytest tools/` in `xboxrecomp`) | https://github.com/llvm/llvm-project/releases/tag/llvmorg-23.1.1 |
| Claude Code | 2.1.310 (native install, `%USERPROFILE%\.local\bin\claude.exe`) | working on the project as before (optional) | https://claude.com/claude-code (PowerShell: `irm https://claude.ai/install.ps1 \| iex`) |
| wgcgrab.exe | built from `twoc\tools\diag\wgcgrab.cpp` by `wgcgrab_build.bat` (`cl /std:c++17 /EHsc /O2`, the SDK's C++/WinRT headers) | grabbing the game window: the harness, `tools\package\smoke_test.ps1` | build it yourself (step 6) |
| A graphics driver with Direct3D 11 | NVIDIA 617.42 (RTX 4080 SUPER) here | running the game | |

Downloaded by the build itself: **SDL 3.4.18** (`SDL3-3.4.18.tar.gz` from
https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz), pinned in `twoc/CMakeLists.txt`
by its SHA-256 `9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3`: CMake refuses any other file.
Without internet, unpack a saved copy and configure with `-DFETCHCONTENT_SOURCE_DIR_SDL3=<that folder>`.

Not needed for the build: Node.js, Rust, gh, 7-Zip, xemu, PresentMon, DXVK (some harness tools use them, step 6).

## 2. Get the source

Use a **short folder** (here `C:\pw`): MSBuild's file tracker fails on paths over 260 characters, and a build under
`%TEMP%` only adds harmless MSB8029 warnings.

```
git clone -b main https://github.com/RicemanRhys/ProjectWrath-source.git C:\pw
git clone -b xboxrecomp https://github.com/RicemanRhys/ProjectWrath-source.git C:\pw\xboxrecomp
```

`main` is this repository (the folder that holds `twoc\`, `crash claude md\` and `.github\`); `xboxrecomp` is the
toolkit with this project's local commits, which `twoc/CMakeLists.txt` expects beside `twoc\` (`XBOXRECOMP_DIR`
defaults to `twoc\..\xboxrecomp`). Without GitHub: `git clone -b master twoc-repo-v1.0.0.bundle C:\pw` and
`git clone -b main xboxrecomp-repo-v1.0.0.bundle C:\pw\xboxrecomp` from `Reports` (the newest bundles hold the same
commits). Before committing, set the repository's own identity as before: `git config user.name ...` and
`git config user.email ...` in `C:\pw` (it isn't in the clone).

## 3. Put the translated code in place

The translated game code (`twoc\src\recomp\gen\`, 9 files, ~33 MB) is never committed. Either:

- **copy the backup:** everything in `Desktop\Crash\Reports\recomp-gen-backup` into `C:\pw\twoc\src\recomp\gen\`; or
- **regenerate it from your disc** (step 5).

Either way the files must have these SHA-256 values (the 1.0.0 build's):

```
cb59806a481e7281ac7a2c46c01aab11d82ebfbf3cd46da04ba0a0efcc94f9f5  recomp_0000.c
4106dd03075ec5b3f555fbfc75199ce3ce6e68f7ea0f9bca3c31c85a36133f40  recomp_0001.c
9de54b69a8d27081f29e45ed93eca6e33673831481f531b1a36d44bf160edaec  recomp_0002.c
4b1f824bedf8f532245c8b9acfe8a938ef272c001700bcd888f1fae1a5af6c13  recomp_0003.c
171d99689008dfeda66e813fe3e1ef0c19eeae716973c94320a91e8020658c38  recomp_0004.c
37e9c88e286a2411491f1d83ced3a5a92887e1098c5d977d9ccbd9a9c741d9ae  recomp_dispatch.c
5f6fc8fc05283b4ddccb0f2d95a59aef526de45dd75c4aad3e4d91e5f0cd82e7  recomp_funcs.h
66d838100c9bb789b94f7389042fb860ec7e8a8f7a5e4c6f74329d6c5b38369f  recomp_stubs_unresolved.c
9d1bcde872efebc6d03207946cd9840f1c03149ce70ee302a362fc386b6d2bb2  recomp_types.h
```

(PowerShell: `Get-FileHash -Algorithm SHA256 C:\pw\twoc\src\recomp\gen\*`.)

## 4. Build

From any prompt (no developer prompt needed; CMake finds Visual Studio itself):

```
cd C:\pw
cmake -S twoc -B twoc\build -G "Visual Studio 17 2022" -A x64
cmake --build twoc\build --config Release --parallel
```

Configuring takes ~20 s (it downloads SDL), the build ~1 minute on this PC (Ryzen 7 7800X3D). The result:
`twoc\build\Release\crash_twoc.exe` and `xemu_apu.dll` beside it (the game doesn't start without the DLL).
Expect ~71 compiler warnings (C4005, C4100, C4210 and the like, the same as always) and no errors; under `%TEMP%`
also MSB8029, harmless. `xemu_apu.dll` comes out byte for byte the same as the 1.0.0 build's; the exe differs only
by its build commit and date and by the build folder's name (SDL keeps two of its source paths, relative to the
repository, so a folder named other than `twoc\build` moves its strings).

To run it: `game_files\` (the extracted disc: `default.xbe` and `Crashdat\`) in the folder it's started from, then
`powershell -File twoc\run_twoc.ps1` from `C:\pw`, or copy the exe and DLL into a folder with `game_files\` and
double-click. The developer exe keeps its saves in `game_files\UDATA` and the toolkit's disk images in
`%LOCALAPPDATA%\xboxrecomp`; set `RECOMP_SAVES_OUTSIDE=1` and `RECOMP_DATA_DIR=<a folder>` to keep both somewhere else.
Started from another folder with its output redirected (a script's `cwd` elsewhere), it looks for `game_files\` in
that folder and opens the first-run setup when there is none.

The released package (`ProjectWrath.exe`, profile-guided): `twoc\tools\package\make_release.ps1` and
`make_package.ps1 -Pgo` (see `RELEASE_CHECKLIST.md`); PGO's training runs drive the harness (step 6).

## 5. Regenerate the translated code from your disc (instead of the backup)

1. **The disc's files in `C:\pw\game_files\`.** The game's own first-run setup does it (16 s here): in a prompt,
   `cd C:\pw` then `twoc\build\Release\crash_twoc.exe` (so it starts in `C:\pw`, with no `game_files\` there), and
   choose (or drop) your disc image
   (`Crash Bandicoot - The Wrath of Cortex (USA).xiso.iso`); it reads the image only, checks every file against
   `twoc\data\disc_usa.tsv` and copies them into `game_files\`. Any other extracted copy works if its `default.xbe`
   has the SHA-256 `e8d7cbf225d899eb88227c11d1f40434c34e27c1b23ed946fb2c3471168d2f4d`.
2. **Python:** `python -m venv C:\pw\.venv`, then
   `C:\pw\.venv\Scripts\python.exe -m pip install -r C:\pw\twoc\tools\requirements.txt`.
3. **The pipeline:** `powershell -ExecutionPolicy Bypass -File C:\pw\twoc\analyze.ps1` (14 s: the XBE's analysis,
   disassembly, function finding, ABI), then `powershell -ExecutionPolicy Bypass -File C:\pw\twoc\lift.ps1` (18 s:
   the lifter with the project's two fixes, then the wrapper callers). Both must be run in that order: the lifter
   reads the analysis outputs. The result is byte for byte the backup (tested in step 116). Outputs: `twoc\src\recomp\gen\`, and
   `xboxrecomp\tools\*\output\` and `game_files\default_analysis.json` on the way (all ignored by git).
4. Check the hashes (step 3) and build (step 4).

## 6. The test harness (twoc\tools\diag)

Private, not needed to build. To use it:

- **Python:** the same `.venv`, plus `pip install -r twoc\tools\diag\requirements.txt`. Many `.sh` files call plain
  `python`: open Git Bash and activate the venv first (`source /c/pw/.venv/Scripts/activate`), so `python` is the
  venv's.
- **Git Bash** (Git for Windows) runs the `.sh` files. Git checks them out with CRLF line ends on Windows unless
  `.gitattributes` says otherwise (it now does for `*.sh`); Git Bash copes with either.
- **The small native tools** (untracked, built from tracked source): `wgcgrab_build.bat`, `loopcap_build.bat`,
  `vrrprobe_build.bat`, `wgctimes_build.bat`, `ddtimes_build.bat`, `devprobe_build.bat`, `chantone_build.bat`
  (each finds Visual Studio 2022 through `vswhere`, any edition or folder). Run each **by its full path** (on this PC
  cmd won't run a `.bat` named alone from the current folder: `NoDefaultCurrentDirectoryInExePath`); the
  "'vswhere.exe' is not recognized" line they print comes from inside `vcvars64.bat` and is harmless.
  `gpfx_build.bat` also needs dsp56300 (github.com/mborgerson/dsp56300, Rust) and `DSP56300_SRC`.
- **Tests that write outside their scratch folder:** the developer exe without `RECOMP_SAVES_OUTSIDE=1`, and any
  exe with `portable.txt` or `RECOMP_SAVES_IN_PACKAGE=1`, puts the toolkit's disk images in
  `%LOCALAPPDATA%\xboxrecomp` (it takes that folder from Windows, not from the `LOCALAPPDATA` variable). In
  `step110\t110.py` that is the `dev`, `portable`, `inpkg`, `kit` and `dxvk` tests (and `ro`/`full` may fall back to
  it); on a fresh PC that's harmless, on yours it rewrites `Partition5.img`'s header.
- **Paths:** most step folders were written for one scratch folder: their `.sh` files set `S=`/`W=`/`PY=` to this
  PC's paths, and each README says what to copy where. Edit those lines for the new PC. A few `.py` files name the
  repository's old folder (`C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp`) for imports or
  game files; `step109/kittest.py` and `tools\package\smoke104.py` name `Desktop\Crash\CaptureKit` and the old
  `wgcgrab.exe`.
- **Outside tools some tests use:** xemu (the reference emulator, with its gdb stub for `gdbmem.py`), PresentMon
  (needs an elevated prompt), the DXVK test folder and `vulkan-tools` downloads (`Desktop\Crash`), DXVK 3.1.1's
  LICENSE for `tools\package\third_party_licenses.py --dxvk` (only to write `THIRD_PARTY_LICENSES.txt` again).
- **Keep the game's files read-only:** run tests from a scratch copy (each step's README says how), with
  `RECOMP_DATA_DIR` (and for the developer exe `RECOMP_SAVES_OUTSIDE=1`) pointing at scratch.

## 7. What isn't in the backups (keep these yourself)

- your disc image (or an extracted `game_files\`), your saves (`%LOCALAPPDATA%\ProjectWrath\saves`, or
  `game_files\UDATA` for the developer exe) and your `ProjectWrath.ini` / `crash_twoc.ini`;
- the generated code (`Reports\recomp-gen-backup`, or regenerate it, step 5);
- Claude Code's project memory (`%USERPROFILE%\.claude\projects\C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp\memory`,
  31 files): the harness notes the sessions keep; it isn't in the repository;
- the PGO profile the released 1.0.0 `ProjectWrath.exe` was optimised with (`twoc\build_pgo\crash_twoc.pgd` and its
  three `.pgc` files, ~29 MB): without it, `make_package.ps1 -Pgo` trains a new one (~6 minutes, through the harness),
  which plays the same but isn't byte for byte the released exe;
- `twoc\reference\` (141 MB, ignored by git): the xemu reference captures and the Legion Go / Nitro 5 log zips;
- `Desktop\Crash\vulkan-tools` (DXVK 3.1.1 and its LICENSE), `CaptureKit`, `DXVK-test` and the device packages;
- for building offline: a copy of `SDL3-3.4.18.tar.gz` (section 1).
