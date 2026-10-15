"""(--noinput: no input file, the keyboard alone.) Launch the game host-only with the fast title and no autostart; port 0 is
driven from TAG.pad.txt (ctl.py). Writes TAG.err.log etc. to runs/.

  python launch.py TAG SECONDS [--exec] [--exe=PATH] [--root=DIR] [--nofast] [ENV=VAL ...]

Step 73: the working directory is scratch root/ (game_files with Crashdat as a junction and its own UDATA/TDATA),
so saves never touch the repo's game_files. The exe defaults to scratch exe/crash_twoc.exe (no ini next to it).
"""
import os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "root")
EXE = os.path.join(HERE, "exe", "crash_twoc.exe")
tag, secs = sys.argv[1], int(sys.argv[2])
extra = {}
for a in sys.argv[3:]:
    if a.startswith("--exe="):
        EXE = a[6:]
    elif a.startswith("--root="):
        ROOT = a[7:]
    elif "=" in a and not a.startswith("--"):
        k, v = a.split("=", 1)
        extra[k] = v
out = os.path.join(HERE, "runs")
pad = os.path.join(out, tag + ".pad.txt")
open(pad, "w").write("\n")
env = {k: v for k, v in os.environ.items() if not k.startswith("RECOMP_")}
env.update(RECOMP_VBLANK="1", RECOMP_AC97_READY="1", RECOMP_WATCHDOG_SECS=str(secs),
           RECOMP_DEV_FAST_TITLE="1", RECOMP_DEV_INPUT_FILE=pad, RECOMP_D3D_BRIDGE="1")
if "--nofast" in sys.argv:
    env.pop("RECOMP_DEV_FAST_TITLE")
if "--exec" in sys.argv:
    env["RECOMP_PB_EXEC"] = "1"
if "--noinput" in sys.argv:          # step 57: the keyboard alone, no input file
    env.pop("RECOMP_DEV_INPUT_FILE")
env.update(extra)
p = subprocess.Popen([EXE], cwd=ROOT, env=env, stdout=open(os.path.join(out, tag + ".out.log"), "wb"),
                     stderr=open(os.path.join(out, tag + ".err.log"), "wb"))
print("pid", p.pid, flush=True)
p.wait()
print("exit", p.returncode, "0x%08X" % (p.returncode & 0xFFFFFFFF), flush=True)
