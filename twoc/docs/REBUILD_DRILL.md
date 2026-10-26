# Restore drill, step 116 (2026-10-26)

**Result: PASSED.** A fresh clone of the private GitHub backup, plus the generated-code backup from `Reports`,
builds a `crash_twoc.exe` that behaves exactly like the current one. The code regenerated from the disc image is
byte for byte the backup. Nothing needed to rebuild the game was missing. What *was* missing is about convenience
and the harness: pinned Python packages, the Visual Studio component list, line ends for shell scripts, build
scripts tied to the Community edition's folder, and written-down versions. Those are fixed in four commits on
`master`, not pushed. The things that still exist only on this PC are listed in section 3; none of them is
needed to build or fix the game.

Everything ran in `%TEMP%\pw116` (C:\Users\User\AppData\Local\Temp\pw116). Nothing of the owner's was written:
a fingerprint (size, time and SHA-256 under 64 MB) of `%LOCALAPPDATA%\xboxrecomp` (the toolkit's disk images),
`%LOCALAPPDATA%\ProjectWrath`, `game_files` (UDATA, TDATA, backups, default.xbe), `twoc\build\Release` (the exe and
the ini), `twoc\src\recomp\gen`, `Reports\recomp-gen-backup` and the disc image was the same before and after (90
entries). GitHub was only cloned.

## 1. What was done, and the results

| Step | How | Result |
|---|---|---|
| Clone | `git clone -b main` and `-b xboxrecomp` of `RicemanRhys/ProjectWrath-source` into `pw116\r` and `pw116\r\xboxrecomp` | e82b0b6 and 2654828: the same commits as the working repositories and as `twoc-repo-v1.0.0.bundle` / `xboxrecomp-repo-v1.0.0.bundle` (both bundles verify as complete histories; clones made from them are identical to the GitHub ones) |
| Generated code | copied from `Reports\recomp-gen-backup` | all 9 files' SHA-256 identical: backup = restored copy = working tree's `twoc\src\recomp\gen` |
| Build | `cmake -S twoc -B twoc\build -G "Visual Studio 17 2022" -A x64`; `cmake --build twoc\build --config Release --parallel` | configure 19 s, build 56 s, exit 0; 71 compiler warnings (the usual kinds) + 16 MSB8029 (the folder is under %TEMP%), no errors. SDL3 3.4.18 downloaded and matched its pinned SHA-256. No path of this PC in the exe |
| Build in a bare environment | the same, from `env -i` with only Windows' own variables and CMake + Git on PATH (no Visual Studio developer prompt) | exit 0, same warnings: the build needs nothing from the shell |
| Same output | the clone's `xemu_apu.dll` vs yours | byte-identical (SHA-256 ee75e909...). The exe differs only by the version line ("1.0.0, 8f20f19" vs your build folder's older "0.1.0, 5687650": only VERSION.txt, a version.rc comment and a recomp_manual.c comment changed since) and by the build folder's name inside SDL's two source-path strings (same section sizes) |
| Identity (default run) | `twoc\tools\diag\step110\drive110.py` from the clone, 150 s, bridge off and host-only, in scratch roots with a copy of `game_files` (default.xbe copied, Crashdat copied to scratch); the clone's exe (A) vs a copy of your `twoc\build\Release\crash_twoc.exe` (U); compared with the clone's `idcmp110.sh` + `compare_runs.py` | **identical**: 231 CdRom file opens in the same order (last THEMEL.wav, the hub), 356 `[PATH]` lines identical in both modes, 20 host device/target lines identical, **8 of 8** first host dumps byte-identical, no `[CRASH]`. Other differences are timing only (APU counters, timestamps) |
| F1 and exit code | the clone's exe started in its folder with the launcher's switches; F1 posted at the main menu, a window grab, F1 again, WM_CLOSE | `[OVL] F1: the settings overlay opens` / `closes`, the menu drawn (grab), **exit code 0** |
| Regenerate from the disc image | the clone's exe in `pw116\r` with no game_files, your `.xiso.iso` dropped onto its first-run setup (read only); then `twoc\analyze.ps1` and `twoc\lift.ps1` with a new `.venv` (capstone 5.0.9) | setup: 2,267 files copied and every SHA-256 checked in 16 s, the game reached its main menu, exit 0. analyze 14 s, lift 18 s, exit 0. **All 9 generated files byte-identical to the backup** |
| Build from the regenerated code | bare environment, `twoc\build_clean` | exit 0 |

How the identity runs stayed off your data: the developer exe normally writes the toolkit's disk images to
`%LOCALAPPDATA%\xboxrecomp` (it asks Windows for that folder, so setting `LOCALAPPDATA` doesn't move it). Every run
here had `RECOMP_SAVES_OUTSIDE=1` and `RECOMP_DATA_DIR` in scratch (the same path for A and U, so the `[PATH]` lines
compare exactly), which moves the saves and the disk images there. Both exes ran the same way.

## 2. The harness from the clone

| Check | Result |
|---|---|
| `bash -n` on all 114 tracked `.sh` | 0 failures |
| every tracked `.py` (239) compiles; the third-party modules import in the new venv | 0 failures; numpy, Pillow, SciPy, OpenCV, pefile, capstone, pytest, imageio-ffmpeg |
| the toolkit's tests (`xboxrecomp`: `python -m pytest tools/`, LLVM's clang on PATH) | 388 passed, 94 subtests (as in the notes) |
| the seven native tools (`wgcgrab`, `loopcap`, `vrrprobe`, `wgctimes`, `ddtimes`, `devprobe`, `chantone`) | all build from the clone; with the fixed `.bat` files also from a bare environment and with cmd's `NoDefaultCurrentDirectoryInExePath` set |
| run from the clone: `drive110.py`, `compare_runs.py`, `idcmp110.sh`, `tools\package\windows.py`, `wgcgrab.exe` | worked (the identity and F1 checks above) |
| `step110\t110.py` (the saves-outside suite) from the clone: `new`, `newself`, `mig`, `both`, `replaced`, `nonascii` | **35 of 35 checks PASS** |
| a fresh venv from the committed requirement files | the same 13 packages and versions as the repo's `.venv` |

**What a developer has to install for the harness:** Git for Windows (Git Bash), Python 3.13 with
`twoc\tools\requirements.txt` + `twoc\tools\diag\requirements.txt` in a venv, activated in Git Bash (many `.sh` call
plain `python`), Visual Studio 2022 for the native tools, LLVM for the toolkit's tests. Optional: xemu (reference
and its gdb stub), PresentMon (elevated), dsp56300 + Rust (`gpfx`), ffmpeg (from imageio-ffmpeg).

**Not tested (and why):**
- the rest of `t110.py` (`dev`, `portable`, `inpkg`, `kit`, `dxvk`, `ro`, `full`, `copyfail`, `movefail`,
  `killcopy`, `killcommit`, `restore`): the first five run layouts that write the toolkit's disk images to your
  `%LOCALAPPDATA%\xboxrecomp`, which the drill had to leave alone; the others weren't run for time. `t110b.sh`,
  the step 107 setup suites (`t6a-e.sh`), the sweep harness (`sweep\`), level/position scripts (`newgame.py`,
  `gopad.py`), the capture kit, PGO training (`pgo_train.sh`) and the packaging scripts (`make_package.ps1`,
  `make_release.ps1`, `smoke_test.ps1`): not run. Each needs its step's scratch set-up (its README), and most
  `.sh` there name this PC's scratch folders (section 3).
- `run_twoc.ps1` (the drill started the exe through the harness's drivers instead).
- a different PC, another Visual Studio edition, Windows 10, an AMD GPU: only this PC was used.
- `gpfx_build.bat` (needs the dsp56300 checkout).

**A harness bug found:** `idcmp110.sh` counts CdRom opens with `grep 'open.*cdrom0'`, which matches nothing in
today's logs (the opens are `[PATH] \Device\CdRom0\...` lines), so its "CdRom opens identical in order (0)" is
trivially true. The same check through `compare_runs.py` (which `idcmp110.sh` also runs) is the real one: 231.
Not fixed (harness source, not a build script).

## 3. Everything the drill needed that wasn't in the clones, the bundles or Reports

| Item | Where it came from | Pinned? | Fix | Done |
|---|---|---|---|---|
| Visual Studio 2022 17.14.41, MSVC 14.44.35207 (cl 19.44.35229), Windows SDK 10.0.26100.7705, MSBuild 17.14.60 | installed on this PC | only in prose ("Desktop development with C++") | `twoc\docs\REBUILD.md` (versions, where) and `twoc\docs\rebuild.vsconfig` (this PC's component list, importable in the installer) | docs commit |
| CMake 4.4.3, Git 2.55.0.windows.3, Python 3.13.15, LLVM 23.1.1, PowerShell 5.1 | installed | no | REBUILD.md (versions, download links) | docs commit |
| SDL 3.4.18: https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz | downloaded by CMake at configure | **yes**: `URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3` (checked) | keep a copy for offline builds (`-DFETCHCONTENT_SOURCE_DIR_SDL3`); REBUILD.md says how | documented; **copy not made** (your call where) |
| Python packages (PyPI) | downloaded by pip | **no** (capstone only named in prose; the harness's not at all) | `twoc\tools\requirements.txt`, `twoc\tools\diag\requirements.txt` (exact versions of your `.venv`) | commit 6b90f80 |
| the venv itself (`.venv`) | made fresh | | REBUILD.md step 5 | docs commit |
| `wgcgrab.exe` and the other six native tools | built from tracked source | the source is tracked; the `.bat` files hard-coded `...\2022\Community\...\vcvars64.bat` | the `.bat` files find VS 2022 with vswhere | commit 17ab9be |
| line ends | Git's `core.autocrlf=true` (installer default) | no `.gitattributes` but `tools\diag\sweep`'s: 12 `.sh` elsewhere came out CRLF (Git Bash tolerated it; other shells don't) | root `.gitattributes`: `.sh` LF, `.bat`/`.cmd` CRLF (a fresh clone checked: 114 LF, 35 CRLF, clean status) | commit 18c50e4 |
| the disc image / `game_files` | `Emulators & Roms\...\Crash Bandicoot - The Wrath of Cortex (USA).xiso.iso` (also `crash rom\`), your `game_files` | `default.xbe`'s SHA-256 and every file's are in `twoc\data\disc_usa.tsv` | keep a copy of the image off this PC (by design it's in no backup) | documented |
| environment variables | none needed to build (checked from a bare environment). Tests: `RECOMP_*` (documented per script); `DSP56300_SRC` for `gpfx`; this PC's `NoDefaultCurrentDirectoryInExePath` makes cmd skip `.bat` files named without a path | | REBUILD.md | docs commit |
| absolute paths | none in the build (CMake, `analyze.ps1`, `lift.ps1` are all relative). In the harness: 69 files in `tools\diag\sweep` and the step folders' `.sh` set `S=`/`W=`/`PY=` to old scratch folders by design (edit per README); `fxgain.py`, `fxlog.py`, `gpwin.py`, `lrbal.py`, `musicfit.py` put the old repo folder on `sys.path`; `imgcmp.py`, `revtab.py` read your `game_files`; `step109\kittest.py`, `prep109.py`, `prepla.py`, `step108\aud108.sh`, `step110\t110b.sh`, `tools\package\smoke104.py` name this PC's folders | | REBUILD.md says so; fixing them is harness source, left for you to decide (each is a one-line change to a path relative to the script) | documented |
| `third_party_licenses.py --dxvk` default `Desktop\Crash\vulkan-tools\dxvk-3.1.1\LICENSE` | this PC only | | only needed to write `THIRD_PARTY_LICENSES.txt` again (it's committed); keep that LICENSE file, or vendor it into `twoc\third_party` | documented |
| git identity (`user.name`/`user.email`, repo-local) and GitHub sign-in (Git Credential Manager) | this PC's `.git\config` and Windows credentials | | REBUILD.md step 2 says to set them | documented |
| Files only on this PC that the drill **didn't** need but a fresh PC would miss | Claude Code's project memory (31 files, 172 KB, `%USERPROFILE%\.claude\projects\...\memory`); `twoc\build_pgo\crash_twoc.pgd` + 3 `.pgc` (~29 MB: the released exe's PGO profile); `twoc\reference\` (141 MB: xemu captures, device logs); `Desktop\Crash\` (vulkan-tools, CaptureKit, DXVK-test, packages); your saves and ini | | back them up beside the bundles (REBUILD.md section 7 lists them) | **not done** (outside the repo and Reports) |

**Public repository note:** `make_public_tree.ps1` exports an allow list, so none of the new files go public. The
public `CONTRIBUTING.md` still says `pip install capstone` (unpinned); adding `twoc/tools/requirements.txt` to the
allow list would let it say `-r twoc\tools\requirements.txt`. Not changed.

## 4. Commits (on `master`, not pushed)

- `18c50e4` line ends on checkout (`.gitattributes`)
- `6b90f80` the Python packages pinned
- `17ab9be` the native tools' `.bat` files find Visual Studio through vswhere
- the docs commit: `twoc\docs\REBUILD.md`, `twoc\docs\rebuild.vsconfig`, this report

The drill's folder `%TEMP%\pw116` (~3.8 GB: the clone, two builds, game file copies, runs) can be deleted.
