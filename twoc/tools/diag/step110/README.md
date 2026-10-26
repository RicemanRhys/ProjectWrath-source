# Step 110 harness: saves outside the package (notes 18.113)

Every test uses SYNTHETIC saves (`t110.synth()`, or a save the game makes in a scratch data folder) and its own
scratch data folder through `RECOMP_DATA_DIR`: nothing here touches the user's `%LOCALAPPDATA%\ProjectWrath`, the
repo's `game_files` (read only: default.xbe copied, Crashdat as a junction) or the user's ini.

| File | What it does |
|---|---|
| `run110.py TAG EXE ROOT SECS [--self] [--menu] [--kill=REGEX] [--keys=...] [--out=DIR] [VAR=VAL...]` | One run of an exe copy, launcher-style (working folder ROOT) or `--self` (like a double-click: working folder C:\, no RECOMP_* but the VAR=VAL given); WM_CLOSE after SECS; `--kill` a hard kill when the log matches. |
| `mkpkg.ps1 -Dir DIR -From EXEDIR [-Name ProjectWrath.exe]` | A package-like folder: the exe copy under that name, the DLL, game_files (default.xbe, Crashdat junction). |
| `rmpkg.ps1 -Dir DIR` | Deletes such a folder under %TEMP%: junctions removed as links first, refuses anything else. |
| `t110.py SCRATCH all\|TEST...` | The suite (18 tests, 75 checks): new install (launcher and double-click), migration (copy, SHA-256, the message once), both layouts, the package replaced and the old folder deleted, portable.txt, RECOMP_SAVES_IN_PACKAGE, the developer exe (and RECOMP_SAVES_OUTSIDE), a read-only data folder, a full disk, a copy / hash / move failure (taken back), kills during the copy and the commit, a non-ASCII data folder, restore at the next start, the capture kit's and the DXVK folder's launchers (replicas, with a control without portable.txt). Results in `SCRATCH\s\t\results.txt`. |
| `t110b.sh SCRATCH [TEST...]` | The tests that drive the game (the developer exe with RECOMP_SAVES_OUTSIDE=1, the harnesses' window title): `choose` (F1 > About > Choose another disc image... > Restart, the user's disc image dropped read-only, the swap, the next start), `verify`, `save` (a new game, the hub's console, SAVE GAME, then LOAD GAME), `savekill` (a hard kill while the written save waits to be put in place, `RECOMP_DEV_SAVE_WRITE_HOLD_MS`), `bakkill` (a kill during the backup after a save), `savepkg` (the package's layout). Needs `SCRATCH\tools\t107.py` (a copy of `../setup107/t107.py`), `SCRATCH\s\sw` (a copy of `../sweep` with `exe\` and `root\`) and `SCRATCH\s\gfcopy` (the disc files). |
| `id110.sh SCRATCH V...`, `idcmp110.sh SCRATCH A B` | The default run's identity (drive110.py 150 s, bridge off and host-only) in scratch roots with the saves reset before each run; compare: 231 CdRom opens in order, `[PATH]` lines, host device lines, the first 8 host dumps. |
| `drive110.py` | `../drive.py` with `--root=DIR` and VAR=VAL extras (drive.py runs in the repo root, with the real game_files). |
| `gamescan.py REPO [--toolkit=DIR --base=COMMIT]` | Part 1: per tracked file, its x86 instruction lines, hex-byte runs, `sub_` names, image addresses and SHA-256 values. |

Test aids added in step 110 (inert unless set): `RECOMP_DEV_SAVES_FAIL=write|copy@N|hash@N|move@N`,
`RECOMP_DEV_SAVES_HOLD=copy@N:MS|commit@N:MS` (userdata.c), `RECOMP_DEV_SAVE_WRITE_HOLD_MS`.
Traps: the session's scratch path is ~154 characters, so a data folder in it is near the 190-character limit (the
non-ASCII test uses one level only); `t110.py` must not be imported with side effects (t110b imports `synth`);
editing a `.sh` while bash runs it breaks the run.
