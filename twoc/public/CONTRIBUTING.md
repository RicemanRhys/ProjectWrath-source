# Contributing

Thanks for helping. Three rules first:

1. **Never post game files, disc images or saves** in an issue, a pull request or anywhere else in this repository:
   no `default.xbe`, nothing from `game_files\` or `Crashdat\`, no `.iso`, no `crash.sav`, no screenshots of the
   game's own files. Everyone supplies their own copy of the game.
2. **Attach the "Collect logs" zip** to a problem report: double-click `Collect logs.bat` in the game's folder after
   the problem happened. It makes `ProjectWrath_logs_<date>_<time>.zip` with the game's logs and a short PC summary,
   your user name, home folder and PC name replaced (`scrub.txt` in the zip lists where). Look inside before you
   attach it. Also paste the text from **F1 > About > Copy build info**.
3. Pull requests contain hand-written source only. The generated code (`twoc/src/recomp/gen/`) is never committed.

## Building the developer build

You need Windows 10 or 11 (64-bit), Visual Studio 2022 (Desktop development with C++), CMake 3.24 or newer, Git,
Python 3.11 or newer, and **your own disc** of Crash Bandicoot: The Wrath of Cortex (USA, original Xbox): the build
translates the game's own program (`default.xbe`), which this repository doesn't contain.

1. Clone this repository, then make the toolkit beside `twoc/`:
   `powershell -File toolkit\fetch_toolkit.ps1` (upstream xboxrecomp at the pinned commit, this project's patches
   applied; see `docs/TOOLKIT.md`).
2. Extract your disc into `game_files\` (the released game's first-run setup does it and checks every file, or any
   extracted copy whose `default.xbe` has the SHA-256 in `twoc/data/disc_usa.tsv`).
3. `python -m venv .venv` and `.venv\Scripts\pip install capstone`.
4. Regenerate the translated code: `powershell -File twoc\analyze.ps1`, then `powershell -File twoc\lift.ps1`.
5. Build: `cmake -S twoc -B twoc\build -G "Visual Studio 17 2022" -A x64` and
   `cmake --build twoc\build --config Release --parallel`. The developer build is `twoc\build\Release\crash_twoc.exe`
   (it keeps its saves in `game_files\UDATA` and the developer names; copied as `ProjectWrath.exe` it behaves as the
   released game).
6. Run it from the repository's root (it finds `game_files\` in its working folder): `twoc\run_twoc.ps1`.

`docs/DEVELOPING.md` says how the port works and where things are.
