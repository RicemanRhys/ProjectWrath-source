"""rs91.py TAG EXEDIR win|fs [ENV=VAL ...] -- step 91 part 2: every Render size entry (game, native, the 18 listed,
a custom 1000x700, 8192x8192) at 16:10, 16:9 and 21:9 through the live trigger file, a window grab after each;
then render scale with the largest sizes. Writes runs/TAG.steps.txt (one line per step: aspect, size, scale,
the picture expected) for rs91chk.py."""
import os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable
WGC = r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag\wgcgrab.exe"
TITLE = "Crash TWOC - D3D bridge (host)"
tag, exedir, mode = sys.argv[1], sys.argv[2], sys.argv[3]
runs = os.path.join(HERE, "runs")
trig = os.path.join(runs, tag + ".trig")
err = os.path.join(runs, tag + ".err.log")
for f in os.listdir(runs):
    if f.startswith(tag + "_") or f.startswith(tag + "."):
        os.remove(os.path.join(runs, f))
env = ["RECOMP_RENDER_SIZE=1280x800", "RECOMP_WIDESCREEN=16:10", "RECOMP_D3D_BRIDGE_SUBDUMP=@" + trig,
       "RECOMP_FRAMERATE=60", "RECOMP_STARTUP_MESSAGE=0", "RECOMP_FULLSCREEN=" + ("1" if mode == "fs" else "0")]
env += sys.argv[4:]
p = subprocess.Popen([PY, os.path.join(HERE, "launch.py"), tag, "900", "--exe=" + os.path.join(exedir, "crash_twoc.exe")]
                     + env, stdout=open(os.path.join(runs, tag + ".launch.txt"), "w"), stderr=subprocess.STDOUT)
t0 = time.time()
while time.time() - t0 < 60:
    time.sleep(1)
    if os.path.exists(err) and b"MAIN MENU OPEN" in open(err, "rb").read():
        break
time.sleep(3)

NATIVE = (3840, 2160)
LISTED = ["1280x960", "1600x1200", "2048x1536", "1280x800", "1440x900", "1680x1050", "1920x1200", "2560x1600",
          "3840x2400", "1280x720", "1600x900", "1920x1080", "2560x1440", "3840x2160", "2560x1080", "3440x1440",
          "3840x1600", "5120x2160"]
ENTRIES = ["0", "native"] + LISTED + ["1000x700", "8192x8192"]
ASPECTS = [("16:10", 1.6), ("16:9", 16 / 9), ("21:9", 21 / 9)]


def fit(size, r):
    """twoc_render_fit for the guest's 640x480."""
    if size == "0":
        return 2 * int(480 * r / 2 + 0.5), 480
    rw, rh = NATIVE if size == "native" else map(int, size.split("x"))
    if rw / rh > r:
        return 2 * int(rh * r / 2 + 0.5), rh
    return rw & ~1, int(rw / r + 0.5)


def scaled(w, h, pct):
    if pct == 100:
        return w, h
    k = pct / 100
    sw, sh = 2 * int(w * k / 2 + 0.5), int(h * k + 0.5)
    if sw > 16384 or sh > 16384:
        f = min(16384 / sw, 16384 / sh)
        sw, sh = 2 * int(sw * f / 2), int(sh * f)
    return max(sw, 16), max(sh, 16)


steps = open(os.path.join(runs, tag + ".steps.txt"), "w")


def step(name, lines, asp, size, pct):
    open(trig + ".tmp", "w").write("".join("set %s\n" % l for l in lines))
    os.replace(trig + ".tmp", trig)
    t = time.time()
    while os.path.exists(trig) and time.time() - t < 10:
        time.sleep(0.05)
    time.sleep(1.6)
    subprocess.run([WGC, TITLE, os.path.join(runs, "%s_%s" % (tag, name)), "0"], capture_output=True)
    w, h = fit(size, dict(ASPECTS)[asp])
    sw, sh = scaled(w, h, pct)
    steps.write("%s %s %s %d %dx%d %dx%d\n" % (name, asp, size, pct, w, h, sw, sh))
    steps.flush()
    print(time.strftime("%H:%M:%S"), name, asp, size, pct, "->", w, h, sw, sh, flush=True)


for asp, r in ASPECTS:
    for e in ENTRIES:
        step("%s_%s" % (asp.replace(":", "x"), e), ["aspect " + asp, "render " + e], asp, e, 100)
for asp, size, pct in [("16:9", "2560x1440", 50), ("16:9", "1280x720", 200), ("16:9", "native", 25),
                       ("16:10", "8192x8192", 200), ("16:9", "8192x8192", 200), ("21:9", "8192x8192", 200),
                       ("21:9", "5120x2160", 150), ("16:9", "3840x2160", 100)]:
    step("scale%d_%s_%s" % (pct, asp.replace(":", "x"), size), ["aspect " + asp, "render " + size, "scale %d" % pct],
         asp, size, pct)
time.sleep(1)
subprocess.run(["taskkill", "/IM", "crash_twoc.exe", "/F"], capture_output=True)
p.wait()
for f in sorted(os.listdir(runs)):
    if f.startswith(tag + "_") and f.endswith(".bmp"):
        subprocess.run([PY, os.path.join(HERE, "topng.py"), os.path.join(runs, f)], capture_output=True)
        os.remove(os.path.join(runs, f))
print("done", tag, flush=True)
