"""Repeated level loads with nobody at the controller (notes 18.40, S7).

  python cycle.py TAG SECONDS [--exec] [--bridge] [--hold=S] [--natural] [--outdir=DIR] [--exe=PATH]

Default: run_twoc.ps1 -HostOnly -FastTitle (the bridge alone, the executor off,
paced by the host limiter), no autostart. The menu's idle timeout (1800
updates, 30 s at 60 Hz, sub_0006E130) starts an attract demo; S seconds
(--hold, default 20) after a demo level's first file opens, Start and A are
pressed through RECOMP_DEV_INPUT_FILE, which ends the demo and goes back to
the menu. So every ~63 s is one menu -> demo -> menu cycle: two level loads.
--natural presses nothing (each demo plays to its end). --exec keeps the
executor on instead (with --bridge, -Bridge).

Writes TAG.err.log, TAG.out.log and TAG.events.txt to --outdir (default: the
current directory), and prints one line per demo load with the [HEAPSTAT]
live figure just before it.
"""
import os
import re
import subprocess
import sys
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
EXE = os.path.join(REPO, "twoc", "build", "Release", "crash_twoc.exe")

tag, secs = sys.argv[1], int(sys.argv[2])
opts = dict(a[2:].split("=", 1) for a in sys.argv[3:] if "=" in a)
flags = {a for a in sys.argv[3:] if "=" not in a}
hold = float(opts.get("hold", "20"))
outdir = os.path.abspath(opts.get("outdir", "."))
if "exe" in opts:
    EXE = os.path.abspath(opts["exe"])

pad = os.path.join(outdir, tag + ".pad.txt")
open(pad, "w").write("\n")
env = {k: v for k, v in os.environ.items() if not k.startswith("RECOMP_")}
env.update(RECOMP_VBLANK="1", RECOMP_AC97_READY="1", RECOMP_WATCHDOG_SECS=str(secs),
           RECOMP_DEV_FAST_TITLE="1", RECOMP_DEV_INPUT_FILE=pad)
if "--exec" in flags:
    env["RECOMP_PB_EXEC"] = "1"
    if "--bridge" in flags:
        env["RECOMP_D3D_BRIDGE"] = "1"
else:
    env["RECOMP_D3D_BRIDGE"] = "1"

errp = os.path.join(outdir, tag + ".err.log")
evf = open(os.path.join(outdir, tag + ".events.txt"), "w")
t0 = time.time()


def ev(s):
    evf.write("%7.1f %s\n" % (time.time() - t0, s))
    evf.flush()
    print("%7.1f %s" % (time.time() - t0, s), flush=True)


p = subprocess.Popen([EXE], cwd=REPO, env=env, stdout=open(os.path.join(outdir, tag + ".out.log"), "wb"),
                     stderr=open(errp, "wb"))
ev("start pid %d" % p.pid)
plan = []
pos = 0
live = "?"
in_demo = False
demos = 0
tail = b""
while p.poll() is None:
    time.sleep(0.25)
    with open(errp, "rb") as f:
        f.seek(pos)
        chunk = f.read()
    pos += len(chunk)
    text = tail + chunk
    tail = text[-200:]
    now = time.time()
    for m in re.finditer(rb"\[HEAPSTAT\] used \d+/\d+ KiB \([0-9.]+%\); live (\d+) KiB", chunk):
        live = m.group(1).decode()
    if b"MAIN MENU OPEN" in chunk:
        ev("menu open; heap live %s KiB" % live)
        in_demo = False
    m = re.search(rb"\[PATH\] \\Device\\CdRom0\\Crashdat\\levels\\([a-z])\\([a-z0-9_]+)\\", chunk)
    if m and not in_demo and m.group(1) != b"b":
        in_demo = True
        demos += 1
        ev("demo %d load: levels\\%s\\%s; heap live %s KiB" % (demos, m.group(1).decode(), m.group(2).decode(),
                                                               live))
        if "--natural" not in flags:
            plan += [(now + hold, "buttons=0x10 a=255", "Start+A down"), (now + hold + 0.6, "", "Start+A up")]
    if b"[CRASH]" in chunk:
        ev("CRASH seen")
    while plan and plan[0][0] <= now:
        _, line, label = plan.pop(0)
        open(pad, "w").write(line + "\n")
        ev(label)
ev("exit %d; heap live %s KiB, %d demo loads" % (p.returncode, live, demos))
