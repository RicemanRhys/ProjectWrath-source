"""play.py TAG SECS [GRABEVERY] -- step 73: scripted play on a launch.py run for SECS seconds.

Runs, turns, jumps, spins and crouches on port 0 (TAG.pad.txt), samples the game's frame counter (0x25A660) and
Crash's position every 0.5 s, grabs the bridge window every GRABEVERY s (default 10) as TAG_pNN, and prints a
summary: frames advanced, the longest stall of the frame counter, distance moved, CRASH lines in the log, whether
the process is still alive.
"""
import math, os, struct, subprocess, sys, threading, time
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
WGC = os.environ.get("WGCGRAB", r"C:\Users\User\AppData\Local\Temp\claude\C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp\34018395-629f-40f9-9d4d-f55117e9c429\scratchpad\bin\wgcgrab.exe")  # wgcgrab.exe (../wgcgrab_build.bat)
tag, secs = sys.argv[1], float(sys.argv[2])
every = float(sys.argv[3]) if len(sys.argv) > 3 else 10
out = os.path.join(HERE, "runs")
pad = os.path.join(out, tag + ".pad.txt")
errp = os.path.join(out, tag + ".err.log")
EXE = os.environ.get("TWOC_EXE", "crash_twoc.exe")
pid = memsnap.find_pid(EXE)
h = memsnap.attach(pid)
F = 32767
# (duration, pad line); a cycle of ~16 s
CYCLE = [
    (2.5, "ly=%d" % F), (0.2, "ly=%d a=255" % F), (1.0, "ly=%d" % F), (0.15, "ly=%d x=255" % F), (0.6, "ly=%d" % F),
    (1.5, "lx=%d" % -F), (0.2, "lx=%d a=255" % -F), (0.8, "lx=%d" % -F), (0.15, "x=255"), (0.4, ""),
    (2.0, "ly=%d" % -F), (0.2, "ly=%d a=255" % -F), (0.6, "ly=%d" % -F),
    (1.5, "lx=%d" % F), (0.15, "lx=%d x=255" % F), (0.5, "lx=%d" % F),
    (1.0, "lx=%d ly=%d" % (F // 2, F)), (0.2, "lx=%d ly=%d rt=255" % (F // 2, F)), (0.6, "lx=%d ly=%d" % (F // 2, F)),
    (0.2, "a=255"), (0.3, ""), (0.2, "a=255"), (0.3, ""),
]


def alive():
    try:
        return memsnap.find_pid(EXE) == pid
    except Exception:
        return False


def grab(name):
    def run():
        subprocess.run([WGC, "Crash TWOC - D3D bridge (host)", os.path.join(out, name), "0"], capture_output=True, timeout=60)
    threading.Thread(target=run, daemon=True).start()


def sample():
    fc = memsnap.u32(h, 0x25A660)
    x, y, z = struct.unpack_from("<3f", memsnap.read(h, 0x8F5120 + 0x6C, 12))
    return fc, x, y, z


stop = threading.Event()
samples = []


def monitor():
    t0 = time.time()
    while not stop.is_set():
        try:
            samples.append((time.time() - t0,) + sample())
        except Exception as e:
            samples.append((time.time() - t0, None, 0, 0, 0))
        time.sleep(0.5)


p0 = os.path.getsize(errp)
mt = threading.Thread(target=monitor, daemon=True)
mt.start()
t0 = time.time()
ng = 0
i = 0
while time.time() - t0 < secs:
    if time.time() - t0 >= ng * every:
        grab("%s_p%02d" % (tag, ng))
        ng += 1
    d, line = CYCLE[i % len(CYCLE)]
    open(pad, "w").write(line + "\n")
    time.sleep(d)
    i += 1
open(pad, "w").write("\n")
time.sleep(1)
stop.set()
mt.join()
fcs = [s for s in samples if s[1] is not None]
stall = 0.0
run = 0.0
last = None
for k in range(1, len(fcs)):
    if fcs[k][1] == fcs[k - 1][1]:
        run += fcs[k][0] - fcs[k - 1][0]
        stall = max(stall, run)
    else:
        run = 0.0
dist = sum(math.dist(fcs[k][2:], fcs[k - 1][2:]) for k in range(1, len(fcs)) if
           math.dist(fcs[k][2:], fcs[k - 1][2:]) < 20)
txt = open(errp, "rb").read()[p0:].decode("latin1")
crash = [l for l in txt.splitlines() if "CRASH" in l or "out of memory" in l.lower()]
fr = (fcs[-1][1] - fcs[0][1]) if fcs else 0
el = (fcs[-1][0] - fcs[0][0]) if fcs else 1
print("summary %s: frames %d in %.1f s (%.1f/s), longest stall %.1f s, moved %.1f units, read failures %d, crash lines %d, alive %s"
      % (tag, fr, el, fr / max(el, 1e-6), stall, dist, len(samples) - len(fcs), len(crash), alive()))
for l in crash[:5]:
    print("  ", l)
with open(os.path.join(out, tag + ".samples.txt"), "w") as f:
    for s in samples:
        f.write(" ".join(str(v) for v in s) + "\n")
