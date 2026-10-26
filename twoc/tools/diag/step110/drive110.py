"""Step 110 (notes 18.113): drive.py with --root=DIR (the working folder: game_files inside, never the repo's) and
VAR=VAL extras (RECOMP_DATA_DIR and the like). Otherwise drive.py as it is:

One unattended run through splash, title, menu, intro and hub (notes 18.32).

  python drive.py TAG SECONDS [--bridge] [--host-only] [--tint] [--point] [--census] [--no-pbexec] [--no-input]
                  [--no-autostart] [--no-fast-title] [--hz=HZ] [--intro=S]
                  [--dump=DIR] [--dump-every=N] [--dump-rt] [--rtlog] [--psprobe] [--texcheck] [--skiptex] [--vsdump] [--vsprobe] [--vp-copy] [--drawlog=A-B]
                  [--outdir=DIR] [--exe=PATH]

Environment as run_twoc.ps1 -FastTitle -AutoStart, plus RECOMP_DEV_INPUT_FILE
(TAG.pad.txt). Input is keyed off the stderr log: Start S seconds (default 6)
after INTRO1.wav opens, which skips the intro; then, once THEMEL.wav opens in
the hub, A once, stick up 5 s, stick right 5 s.

  --bridge   RECOMP_D3D_BRIDGE=1, the host mirror (S2, step 31)
  --tint     RECOMP_D3D_BRIDGE_TINT=1, mirrored clears show the frame index
  --point    RECOMP_D3D_BRIDGE_POINT=1, point sampling as the executor does (a check, S3)
  --census   RECOMP_D3D_CENSUS=1, the S1 census (step 30's --bridge)
  --dump     RECOMP_FB_DUMP=DIR/shot, as run_twoc.ps1 -DumpDir does
  --dump-every=N  RECOMP_D3D_BRIDGE_DUMP_EVERY=N: a host dump every Nth guest swap too (S3)
  --dump-rt  RECOMP_D3D_BRIDGE_DUMP_RT=1: each host dump also writes the host render targets (S4)
  --rtlog    RECOMP_D3D_BRIDGE_RTLOG=1: log render-target draws in full (S4; rtcheck.py re-renders them)
  --psprobe  RECOMP_D3D_BRIDGE_PSPROBE=1: probe each pixel shader once (S4; pscheck.py checks the log)
  --texcheck RECOMP_D3D_BRIDGE_TEXCHECK=1: write each P8 texture once, as uploaded, and each DXT cube map's
             six faces raw (S4, S5 part 2; needs --dump; skiptex.py decodes the faces)
  --skiptex  RECOMP_D3D_BRIDGE_SKIPTEX=1: write each texture a skipped draw binds, once per kind, and each
             new immediate draw's screen box (step 34; needs --dump; skiptex.py decodes the DXT ones)
  --vsdump   RECOMP_D3D_BRIDGE_VSDUMP=1: each vertex program in full, once (S5; vscheck.py decodes it)
  --vsprobe  RECOMP_D3D_BRIDGE_VSPROBE=1: draw each program's first vertices as points on the host and log
             where they land (S5; vscheck.py --probe compares with its CPU evaluation; use with --vsdump)
  --vp-copy  RECOMP_D3D_BRIDGE_VP_COPY=1: mirror program draws that sample a CopyRects copy even when their
             texture coordinates can't be scaled from texels (S5 part 2; none such from splash to hub)
  --drawlog=A-B  RECOMP_D3D_BRIDGE_DRAWLOG=A-B: log every draw in guest swaps A-B (path, texture, screen box,
             texture-coordinate range, pixel-shader texture modes and all four stage textures) and, with --dump, a
             host dump of each of those swaps (step 35)
  --no-pbexec leaves RECOMP_PB_EXEC unset.
  --host-only  --bridge --no-pbexec, as run_twoc.ps1 -HostOnly (S7): the mirror alone, paced by its limiter
  --no-autostart leaves RECOMP_DEV_AUTOSTART unset: the menu waits for its idle timeout (S7 timing check)
  --no-fast-title leaves RECOMP_DEV_FAST_TITLE unset: the title animation at its own speed
  --hz=HZ    RECOMP_D3D_BRIDGE_HZ=HZ: the host frame limiter's refresh rate (S7; 0 turns it off)
  --exe=PATH runs a copy of crash_twoc.exe (so the build can be relinked meanwhile);
             the game still runs in the repo root.

Every other RECOMP_* variable from the calling shell is cleared.

Writes TAG.out.log, TAG.err.log and TAG.events.txt (the driver's own
timeline) to --outdir (default: the current directory). compare_runs.py
compares two err logs; census.py summarises a --census one.
"""
import os
import subprocess
import sys
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", ".."))
EXE = os.path.join(REPO, "twoc", "build", "Release", "crash_twoc.exe")

tag, secs = sys.argv[1], int(sys.argv[2])
opts = dict(a[2:].split("=", 1) for a in sys.argv[3:] if "=" in a and a.startswith("--"))
extra = dict(a.split("=", 1) for a in sys.argv[3:] if "=" in a and not a.startswith("--"))
flags = {a for a in sys.argv[3:] if "=" not in a}
if "root" in opts:
    REPO = os.path.abspath(opts["root"])
if "--host-only" in flags:
    flags |= {"--bridge", "--no-pbexec"}
intro_s = float(opts.get("intro", "6"))
outdir = os.path.abspath(opts.get("outdir", "."))
if "exe" in opts:
    EXE = os.path.abspath(opts["exe"])

pad = os.path.join(outdir, tag + ".pad.txt")
open(pad, "w").write("\n")
env = {k: v for k, v in os.environ.items() if not k.startswith("RECOMP_")}
env.update(RECOMP_VBLANK="1", RECOMP_AC97_READY="1", RECOMP_WATCHDOG_SECS=str(secs))
if "--no-fast-title" not in flags:
    env["RECOMP_DEV_FAST_TITLE"] = "1"
if "--no-autostart" not in flags:
    env["RECOMP_DEV_AUTOSTART"] = "1"
if "--no-pbexec" not in flags:
    env["RECOMP_PB_EXEC"] = "1"
if "--no-input" not in flags:
    env["RECOMP_DEV_INPUT_FILE"] = pad
if "--bridge" in flags:
    env["RECOMP_D3D_BRIDGE"] = "1"
if "--tint" in flags:
    env["RECOMP_D3D_BRIDGE_TINT"] = "1"
if "--point" in flags:
    env["RECOMP_D3D_BRIDGE_POINT"] = "1"
if "--census" in flags:
    env["RECOMP_D3D_CENSUS"] = "1"
if "dump" in opts:
    os.makedirs(opts["dump"], exist_ok=True)
    env["RECOMP_FB_DUMP"] = os.path.join(os.path.abspath(opts["dump"]), "shot")
if "drawlog" in opts:
    env["RECOMP_D3D_BRIDGE_DRAWLOG"] = opts["drawlog"]
if "hz" in opts:
    env["RECOMP_D3D_BRIDGE_HZ"] = opts["hz"]
if "dump-every" in opts:
    env["RECOMP_D3D_BRIDGE_DUMP_EVERY"] = opts["dump-every"]
for flag, var in (("--dump-rt", "RECOMP_D3D_BRIDGE_DUMP_RT"), ("--rtlog", "RECOMP_D3D_BRIDGE_RTLOG"),
                  ("--psprobe", "RECOMP_D3D_BRIDGE_PSPROBE"),
                  ("--texcheck", "RECOMP_D3D_BRIDGE_TEXCHECK"), ("--skiptex", "RECOMP_D3D_BRIDGE_SKIPTEX"),
                  ("--vsdump", "RECOMP_D3D_BRIDGE_VSDUMP"), ("--vsprobe", "RECOMP_D3D_BRIDGE_VSPROBE"),
                  ("--vp-copy", "RECOMP_D3D_BRIDGE_VP_COPY")):
    if flag in flags:
        env[var] = "1"

env.update(extra)
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
