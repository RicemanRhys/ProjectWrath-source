"""pgsw2.py NAME -- step 95 (notes 18.98): pgosweep the running PGO-instrumented crash_twoc.exe's counts into
$TWOC_PGC_DIR/crash_twoc!NAME.pgc (beside its .pgd), for make_package.ps1 -Pgo's /USEPROFILE link. The game ends
with TerminateProcess, so its counts are swept while it runs. $TWOC_PGOSWEEP: pgosweep.exe's path."""
import os, subprocess, sys
out = os.path.join(os.environ["TWOC_PGC_DIR"], "crash_twoc!%s.pgc" % sys.argv[1])
r = subprocess.run([os.environ["TWOC_PGOSWEEP"], "crash_twoc.exe", out], capture_output=True, text=True)
print("pgsw2: %s exit %d, %s %s" % (out, r.returncode, "written" if os.path.exists(out) else "NOT written",
                                    (r.stdout.strip() + " " + r.stderr.strip())[-300:]), flush=True)
