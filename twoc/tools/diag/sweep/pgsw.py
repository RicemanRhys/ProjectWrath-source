"""pgsw.py NAME -- step 84 (d): pgosweep the running PGO-instrumented crash_twoc.exe's counts into
../d84/pg/Release/crash_twoc!NAME.pgc (beside its .pgd), for the /USEPROFILE link."""
import os, subprocess, sys
PGOSWEEP = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\pgosweep.exe"
OUT = r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\d84\pg\Release"
r = subprocess.run([PGOSWEEP, "crash_twoc.exe", os.path.join(OUT, "crash_twoc!%s.pgc" % sys.argv[1])],
                   capture_output=True, text=True)
print(r.returncode, r.stdout.strip()[-300:], r.stderr.strip()[-300:])
