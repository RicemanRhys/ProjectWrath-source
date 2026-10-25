# Step 109's tests (notes 18.112): the Project Wrath names, migration, problem reporting

Run from a scratch folder holding `tools\` (step 107's `setup107/*.py` copied there, plus these) with the repo's
`.venv` Python. Test folders go in scratch; their `game_files\Crashdat` is a junction to the repo's (read only by the
game): **remove junctions as links only** (`[IO.Directory]::Delete(path)`), never with `rm -rf`.

| File | What it does |
|---|---|
| `mk_t109.py T107 OUT`, `mk_t109b.py OUT` | Makes `t109.py` from step 107's `t107.py`: the names follow the exe (`ProjectWrath.exe`: the setup window "Project Wrath - Setup", the game "Project Wrath", `ProjectWrath.err.log`; `crash_twoc.exe` the old ones), plus the steps `gclose` (WM_CLOSE to the game window), `titles` (the process's windows and `tasklist /V`) and `clip` (the clipboard's text to `ROOT/TAG.clip.txt`). |
| `prep109.py DIR EXE [--name N] [--gf] [--oldini F] [--ini F]` | A test folder: the exe (as `ProjectWrath.exe` unless `--name`) and its DLL; `--gf` a game_files stand-in (default.xbe copied, Crashdat a junction, an empty UDATA); `--oldini` a `crash_twoc.ini` beside it (the migration). `{NA}` in DIR: "Test é Ω". |
| `prepla.py LA [--tree OUT]` | A stand-in `%LOCALAPPDATA%` (for the exe's `RECOMP_DEV_LOCALAPPDATA`) with an old `CrashTWOC` data folder: the disc files copied, a stand-in save, a stand-in ini, logs and a mute marker; `--tree` lists every file with its SHA-256 (before/after: the old folder must not change). |
| `clip.py save\|restore\|check FILE` | Keeps the user's clipboard text around a test and puts it back; `check`: a Copy build info paste holds no user name, PC name or path. |
| `explorer_win.py NAME [--close]` | The Explorer windows "Open logs folder" opened (closed with `--close`). |
| `wavlevel.py WAV...` | RMS, peak and the loudest second of a loopcap recording. |
| `kittest.py DIR` | A replica of the capture kit (the kit's `kit\run.cmd` and exe, a stand-in ini and game files) run like a numbered launcher to the main menu, closed; prints the capture folder and the `[KIT]` line. The user's kit itself is never run. |
| `dxvk/` | The DXVK test folder's renamed launchers (outside the repo: `Desktop\Crash\DXVK-test`) and `ed_readme.py`, which updated its README. |

The rest of step 109's checks used the committed tools: `tools/diag/sweep/id.sh` + `idcmp.sh` (identity, `M` edited),
`tools/package/smoke_test.ps1` (Legion Go, Nitro 5, DXVK copies; its `-Quit` sequence fixed for step 104's tabs) and
`tools/diag/loopcap.exe`.
