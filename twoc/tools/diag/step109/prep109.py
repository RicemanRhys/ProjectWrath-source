"""prep109.py DIR EXE [--name ProjectWrath.exe] [--gf] [--ini FILE] [--oldini FILE]

A test folder for step 109 (in scratch): EXE and the xemu_apu.dll beside it copied into DIR under --name (default
ProjectWrath.exe); --gf: DIR\\game_files with default.xbe copied from the repo's game_files, Crashdat as a junction
to the repo's (read only by the game) and an empty UDATA (no saves of the user's: the game makes its own);
--oldini FILE: FILE copied to DIR\\crash_twoc.ini (the migration test); --ini FILE: to DIR\\<name>.ini.
"{NA}" in DIR: a folder name with a space and letters outside ASCII.
"""
import os, shutil, subprocess, sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
NA = 'Test \u00e9 \u03a9'
REPO = r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp"
a = sys.argv[1:]
d = a[0].replace('{NA}', NA)
exe = a[1]
name = a[a.index('--name') + 1] if '--name' in a else 'ProjectWrath.exe'
os.makedirs(d, exist_ok=True)
shutil.copy(exe, os.path.join(d, name))
shutil.copy(os.path.join(os.path.dirname(exe), 'xemu_apu.dll'), os.path.join(d, 'xemu_apu.dll'))
if '--gf' in a:
    gf = os.path.join(d, 'game_files')
    os.makedirs(os.path.join(gf, 'UDATA'), exist_ok=True)
    shutil.copy(os.path.join(REPO, 'game_files', 'default.xbe'), gf)
    j = os.path.join(gf, 'Crashdat')
    if not os.path.exists(j):
        r = subprocess.run(['powershell', '-NoProfile', '-Command',
                            'New-Item -ItemType Junction -Path "%s" -Target "%s" | Out-Null' %
                            (j, os.path.join(REPO, 'game_files', 'Crashdat'))], capture_output=True, text=True)
        assert os.path.isdir(j), r.stderr
if '--oldini' in a:
    shutil.copy(a[a.index('--oldini') + 1], os.path.join(d, 'crash_twoc.ini'))
if '--ini' in a:
    shutil.copy(a[a.index('--ini') + 1], os.path.join(d, os.path.splitext(name)[0] + '.ini'))
print('made', d, sorted(os.listdir(d)))
