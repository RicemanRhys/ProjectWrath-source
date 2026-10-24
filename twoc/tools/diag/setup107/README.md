# Step 107's tests of the first-run setup (notes 18.110)

Copy this folder's files to a scratch folder (`tools\` for the .py, the .sh beside it) and edit `S`/`W` in the
.sh files (the scratch folder) first. Nothing here writes outside that scratch folder, and the disc images are only
read. Python: the repo's `.venv` (Pillow for `png.py`).

| File | What it does |
|---|---|
| `t107.py TAG ROOT EXE [--self] [--copyexe=] [--cwd=] [--datadir=] [--env=K=V...] --steps="..."` | Starts an exe copy launcher-style (Play Crash.bat's switches, working folder ROOT, logs to `ROOT\TAG.err.log`) or like a double-click (`--self`: ShellExecute from `C:\`, only RECOMP_DEV_* variables; the exe's own `run_twoc.err.log`, copied to `TAG.err.log`), then runs steps: `wait:REGEX@S`, `sleep:S`, `key:` (posted to the setup window; `f1` etc.), `type:TEXT`, `tap:BUTTON` (an SDL virtual Xbox pad through `RECOMP_DEV_PAD_FILE`, plugged in from the start), `grab:`/`grabg:` (wgcgrab of the setup or the game window), `dialog:PATH` (fills the Windows file or folder dialog and presses its button, posted), `close`, `kill` (taskkill /F), `gkey:` (keys to the game window), `lock:PATH@S` (holds a file open without delete sharing), `max`, `mem`, `follow:S` (Restart now's new copy), `waitexit:S`. `{NA}` in any argument is a folder name with a space and letters outside ASCII ("Test é Ω"); `{DEEP:x}` the folder `prep107.py --deep` made. |
| `prep107.py ROOT [--exe P] [--exedir SUB] [--gf] [--request] [--image NAME] [--ro SUB / --unro SUB] [--deep N] [--copyiso NAME]` | A test's folder: the exe, game_files from `fx\gf copy` with a stand-in save and a file of the player's, the F1 menu's request file, a synthetic Redump-style image (sparse), a read-only folder (deny WD, AD), a folder over N characters of long non-ASCII names. |
| `check107.py ROOT [--hash] [--save NAME]` | game_files against the manifest (present, size, `--hash` SHA-256), leftovers (`game_files.new`, `.old`, the journal, the request), where the save NAME is. |
| `fixtures.py OUT` | The synthetic Redump-style image, a truncated one (600 MB), a random file, a damaged copy (one byte of chars.dat), another game's folder (Twinsanity's default.xbe), doctored default.xbe folders (PAL, version 2; Crashdat a junction: remove with rmdir). |
| `isoscan.py ISO [FOLDER] [--manifest OUT]`, `mkmanifest.py` | Hashes every file of an image read-only, its default.xbe certificate, compares with a folder; the repo's manifest from it. |
| `emptydirs.py ISO`, `exestr.py EXE [TEXT]`, `dlgtree.py`, `zipscan.py ZIP`, `png.py` | The image's empty folders; strings in an exe holding the home folder; a dialog's controls; a Collect logs zip's traces of this PC; a grab as PNG. |
| `t6a.sh` ... `t6e.sh` | The groups: A the five ways to set up, B the failures, C the hard kills and recovery, D the F1 replace / rollback / locked file, E read-only folders, long and non-ASCII paths, 1280x800 and 4K by pad. `ONLY="tag ..."` runs some. |

Exe test hooks (RECOMP_DEV_*, which don't count as a launcher): `SETUP_DROP=PATH`, `SETUP_HOLD=copy@N:MS | verify@N:MS | carry@0:MS | renames@0:MS`, `SETUP_FAIL=diskfull@N`, `SETUP_FREE_MB=N`, `SETUP_MBPS=N`, `SETUP_SIZE=WxH`, `SETUP_NAVLOG=1`, `LOCALAPPDATA=DIR`.
