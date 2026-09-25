"""One unattended run through splash, title, menu, intro and hub (notes 18.32).

  python drive.py TAG SECONDS [--bridge] [--tint] [--census] [--no-pbexec] [--no-input] [--intro=S]
                  [--dump=DIR] [--outdir=DIR]

Environment as run_twoc.ps1 -FastTitle -AutoStart, plus RECOMP_DEV_INPUT_FILE
(TAG.pad.txt). Input is keyed off the stderr log: Start S seconds (default 6)
after INTRO1.wav opens, which skips the intro; then, once THEMEL.wav opens in
the hub, A once, stick up 5 s, stick right 5 s.

  --bridge   RECOMP_D3D_BRIDGE=1, the host mirror (S2, step 31)
  --tint     RECOMP_D3D_BRIDGE_TINT=1, mirrored clears show the frame index
  --census   RECOMP_D3D_CENSUS=1, the S1 census (step 30's --bridge)
  --dump     RECOMP_FB_DUMP=DIR/shot, as run_twoc.ps1 -DumpDir does
  --no-pbexec leaves RECOMP_PB_EXEC unset.

Every other RECOMP_* variable from the calling shell is cleared.

Writes TAG.out.log, TAG.err.log and TAG.events.txt (the driver's own
timeline) to --outdir (default: the current directory). compare_runs.py
compares two err logs; census.py summarises a --census one.
"""
import os
import subprocess
import sys
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
EXE = os.path.join(REPO, "twoc", "build", "Release", "crash_twoc.exe")

tag, secs = sys.argv[1], int(sys.argv[2])
opts = dict(a[2:].split("=", 1) for a in sys.argv[3:] if "=" in a)
flags = {a for a in sys.argv[3:] if "=" not in a}
intro_s = float(opts.get("intro", "6"))
outdir = os.path.abspath(opts.get("outdir", "."))

pad = os.path.join(outdir, tag + ".pad.txt")
open(pad, "w").write("\n")
env = {k: v for k, v in os.environ.items() if not k.startswith("RECOMP_")}
env.update(RECOMP_VBLANK="1", RECOMP_AC97_READY="1", RECOMP_WATCHDOG_SECS=str(secs),
           RECOMP_DEV_FAST_TITLE="1", RECOMP_DEV_AUTOSTART="1")
if "--no-pbexec" not in flags:
    env["RECOMP_PB_EXEC"] = "1"
if "--no-input" not in flags:
    env["RECOMP_DEV_INPUT_FILE"] = pad
if "--bridge" in flags:
    env["RECOMP_D3D_BRIDGE"] = "1"
if "--tint" in flags:
    env["RECOMP_D3D_BRIDGE_TINT"] = "1"
if "--census" in flags:
    env["RECOMP_D3D_CENSUS"] = "1"
if "dump" in opts:
    os.makedirs(opts["dump"], exist_ok=True)
    env["RECOMP_FB_DUMP"] = os.path.join(os.path.abspath(opts["dump"]), "shot")

errp = os.path.join(outdir, tag + ".err.log")
evf = open(os.path.join(outdir, tag + ".events.txt"), "w")
t0 = time.time()


def ev(s):
    evf.write("%7.1f %s\n" % (time.time() - t0, s))
    evf.flush()


p = subprocess.Popen([EXE], cwd=REPO, env=env, stdout=open(os.path.join(outdir, tag + ".out.log"), "wb"),
                     stderr=open(errp, "wb"))
ev("start pid %d" % p.pid)
plan = []                  # (time, pad line, label)
state = 0                  # 0 wait for INTRO1, 1 wait for the hub music, 2 done
start_done = False
pos = 0
while p.poll() is None:
    time.sleep(0.25)
    if "--no-input" in flags:
        continue
    with open(errp, "rb") as f:
        f.seek(pos)
        chunk = f.read()
    pos += len(chunk)
    now = time.time()
    if state == 0 and b"INTRO1.wav" in chunk:
        ev("INTRO1.wav seen")
        state = 1
        plan += [(now + intro_s, "buttons=0x10", "Start down"), (now + intro_s + 0.6, "", "Start up")]
    elif state == 1 and start_done and b"THEMEL.wav" in chunk:
        ev("hub music seen")
        state = 2
        plan += [(now + 3, "a=255", "A down"), (now + 3.5, "", "A up"),
                 (now + 6, "ly=32767", "stick up"), (now + 11, "lx=32767", "stick right"),
                 (now + 16, "", "stick idle")]
    while plan and plan[0][0] <= now:
        _, line, label = plan.pop(0)
        open(pad, "w").write(line + "\n")
        ev(label)
        start_done = start_done or label == "Start up"
ev("exit %d" % p.returncode)
print(tag, "exit", p.returncode, "after %.0f s" % (time.time() - t0))
