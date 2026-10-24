"""msg.py -- step 104: the startup message's place: grabs at 1.5, 3.5, 5.5 and 7.3 s after the window opens.

  python msg.py TAG EXEDIR RS [ENV=VAL ...]
"""
import os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable
WGC = os.environ.get("WGCGRAB", r"C:\Users\User\AppData\Local\Temp\claude\C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp\34018395-629f-40f9-9d4d-f55117e9c429\scratchpad\bin\wgcgrab.exe")
TITLE = "Crash TWOC - D3D bridge (host)"
tag, exedir, rs = sys.argv[1], sys.argv[2], sys.argv[3]
extra = dict(a.split("=", 1) for a in sys.argv[4:])
runs = os.path.join(HERE, "runs")
pad = os.path.join(runs, tag + ".pad.txt")
open(pad, "w").write("\n")
env = {k: v for k, v in os.environ.items() if not k.startswith("RECOMP_")}
env.update(RECOMP_VBLANK="1", RECOMP_AC97_READY="1", RECOMP_WATCHDOG_SECS="30", RECOMP_DEV_INPUT_FILE=pad,
           RECOMP_D3D_BRIDGE="1", RECOMP_RENDER_SIZE=rs)
env.update(extra)
g = subprocess.Popen([WGC, TITLE, os.path.join(runs, tag), "1.5", "3.5", "5.5", "7.3"], stdout=subprocess.DEVNULL)
p = subprocess.Popen([os.path.join(exedir, "crash_twoc.exe")], cwd=os.path.join(HERE, "root"), env=env,
                     stdout=open(os.path.join(runs, tag + ".out.log"), "wb"),
                     stderr=open(os.path.join(runs, tag + ".err.log"), "wb"))
g.wait(timeout=120)
p.kill()
p.wait()
for f in sorted(os.listdir(runs)):
    if f.startswith(tag + "_") and f.endswith(".bmp"):
        subprocess.run([PY, os.path.join(HERE, "topng.py"), os.path.join(runs, f)], capture_output=True)
        os.remove(os.path.join(runs, f))
        print(f[:-4] + ".png")
