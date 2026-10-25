"""Makes tools/t109.py: step 107's t107.py with the names following the exe (Project Wrath for ProjectWrath.exe)."""
import os, sys
src, dst = sys.argv[1], sys.argv[2]
s = open(src, encoding='utf-8').read()
reps = [
    ("tag, root, exe = sys.argv[1], os.path.abspath(na(sys.argv[2])), os.path.abspath(na(sys.argv[3]))\n",
     "tag, root, exe = sys.argv[1], os.path.abspath(na(sys.argv[2])), os.path.abspath(na(sys.argv[3]))\n"
     "# step 109: the names follow the exe (names.h): the developer's crash_twoc.exe keeps the old ones\n"
     "IMAGE = os.path.basename(exe)\n"
     "DEV = IMAGE.lower() == 'crash_twoc.exe'\n"
     "LOGNAME = 'run_twoc.err.log' if DEV else 'ProjectWrath.err.log'\n"
     "SETUP_T = 'Crash TWOC - Setup' if DEV else 'Project Wrath - Setup'\n"
     "GAME_T = 'Crash TWOC - D3D bridge (host)' if DEV else 'Project Wrath'\n"),
    ("old_ino = os.stat(os.path.join(datadir, 'run_twoc.err.log')).st_ino", "old_ino = os.stat(os.path.join(datadir, LOGNAME)).st_ino"),
    ("log_src = os.path.join(datadir, 'run_twoc.err.log')", "log_src = os.path.join(datadir, LOGNAME)"),
    ("w = windows(r'^Crash TWOC - Setup$')", "w = windows('^' + re.escape(SETUP_T) + '$')"),
    ("w = windows(r'^Crash TWOC - D3D bridge')", "w = windows('^' + re.escape(GAME_T) + '$')"),
    ("title = 'Crash TWOC - Setup' if kind == 'grab' else 'Crash TWOC - D3D bridge (host)'", "title = SETUP_T if kind == 'grab' else GAME_T"),
    ("'IMAGENAME eq crash_twoc.exe'", "'IMAGENAME eq ' + IMAGE"),
]
for a, b in reps:
    n = s.count(a)
    assert n >= 1, a
    s = s.replace(a, b)
pass
open(dst, 'w', encoding='utf-8').write(s)
print('ok', [l for l in s.splitlines() if 'crash_twoc' in l])
