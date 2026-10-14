"""Step 70 (notes 18.73): a run driven by SDL virtual pads (RECOMP_DEV_PAD_FILE).

  python vpads.py TAG SECONDS SCRIPT [--exe=PATH] [--keyboard=0|1] [--mode=sdl|xinput] [--bridge-off]
                  [--autostart] [--input] [--ini=PATH] [--env=K=V ...]

Run an exe copy (--exe) in a folder of its own: the repo's exe reads the player's crash_twoc.ini, and --ini
copies one next to the exe. The pad file (TAG.vpad.txt) can also be written by another script while it runs.
Pad lines: a kind (none xbox ps4 ps5 switch steam deck generic) then input=value pairs (a b x y lb rb lt rt
start back ls rs up down left right guide paddle1-4 touchpad misc1-6; lx ly rx ry, y up); see pads.c.

SCRIPT lines (one step each, run in order):
  pad <line>      write <line> to the pad file (e.g. "ps5 a=1", "none")
  input <line>    write <line> to RECOMP_DEV_INPUT_FILE (drive.py's port-0 dev pad; only with --input)
  sleep <s>
  wait <text>     until <text> appears in the err log (bytes, case-sensitive)
  mark <label>    a line in the events file
Host-only (RECOMP_D3D_BRIDGE=1, no executor) unless --bridge-off. Fast title, no autostart, no input file
unless asked. Logs in the current folder: TAG.err.log, TAG.out.log, TAG.events.txt.
"""
import os
import shutil
import subprocess
import sys
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
EXE = os.path.join(REPO, "twoc", "build", "Release", "crash_twoc.exe")

tag, secs, script = sys.argv[1], int(sys.argv[2]), sys.argv[3]
opts = {}
envx = {}
flags = set()
for a in sys.argv[4:]:
    if a.startswith("--env="):
        k, v = a[6:].split("=", 1)
        envx[k] = v
    elif "=" in a:
        k, v = a[2:].split("=", 1)
        opts[k] = v
    else:
        flags.add(a)
exe = os.path.abspath(opts.get("exe", EXE))
out = os.path.abspath(".")
padf = os.path.join(out, tag + ".vpad.txt")
inpf = os.path.join(out, tag + ".input.txt")
open(padf, "w").write("none\n")
open(inpf, "w").write("\n")
if "ini" in opts:
    shutil.copy(opts["ini"], os.path.join(os.path.dirname(exe), "crash_twoc.ini"))

env = {k: v for k, v in os.environ.items() if not k.startswith("RECOMP_")}
env.update(RECOMP_VBLANK="1", RECOMP_AC97_READY="1", RECOMP_WATCHDOG_SECS=str(secs), RECOMP_DEV_FAST_TITLE="1",
           RECOMP_DEV_PAD_FILE=padf)
if "--bridge-off" not in flags:
    env["RECOMP_D3D_BRIDGE"] = "1"
else:
    env["RECOMP_PB_EXEC"] = "1"
if "keyboard" in opts:
    env["RECOMP_KEYBOARD"] = opts["keyboard"]
if "mode" in opts:
    env["RECOMP_CONTROLLER_INPUT"] = opts["mode"]
if "--autostart" in flags:
    env["RECOMP_DEV_AUTOSTART"] = "1"
if "--input" in flags:
    env["RECOMP_DEV_INPUT_FILE"] = inpf
env.update(envx)

steps = [l.strip() for l in open(script) if l.strip() and not l.strip().startswith("#")]
errp = os.path.join(out, tag + ".err.log")
evf = open(os.path.join(out, tag + ".events.txt"), "w")
t0 = time.time()


def ev(s):
    evf.write("%7.1f %s\n" % (time.time() - t0, s))
    evf.flush()


p = subprocess.Popen([exe], cwd=REPO, env=env, stdout=open(os.path.join(out, tag + ".out.log"), "wb"),
                     stderr=open(errp, "wb"))
ev("start pid %d" % p.pid)
log = b""
i = 0
until = 0.0
while p.poll() is None:
    time.sleep(0.05)
    with open(errp, "rb") as f:
        log = f.read()
    now = time.time()
    while i < len(steps) and now >= until:
        cmd, _, arg = steps[i].partition(" ")
        if cmd == "wait":
            if arg.encode() not in log:
                break
            ev("seen: " + arg)
        elif cmd == "sleep":
            until = now + float(arg)
            i += 1
            ev("sleep " + arg)
            break
        elif cmd == "pad":
            open(padf, "w").write(arg + "\n")
            ev("pad: " + arg)
        elif cmd == "input":
            open(inpf, "w").write(arg + "\n")
            ev("input: " + arg)
        elif cmd == "mark":
            ev("mark: " + arg)
        i += 1
ev("exit %d" % p.returncode)
print(tag, "exit", p.returncode, "after %.0f s;" % (time.time() - t0), "steps done %d/%d" % (i, len(steps)))
