"""ap.py TAG SECS X,Z:X,Z:... -- step 106: an autopilot for Smokey's vehicle through the pad file (runs/TAG.pad.txt):
A held, the stick steered by pure pursuit: the point AP_LOOK (default 10) units ahead along the waypoints' polyline
from the nearest point on it (searched forward only), against the heading from the last 0.25 s of motion (+x is to
the right when facing +z). Prints 'ap T X Y Z SPEED LX S' every 0.05 s (S = distance along the path). Env AP_GAIN
(stick per radian, default 60000), AP_VMAX (throttle off above it, or with the error over 0.5 rad; default 11), AP_STOP=1 (throttle off at the path's end: it coasts to a stop). Waypoints may also be given as one argument with ':' between them."""
import math, os, struct, sys, time
import numpy as np
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
HERE = os.path.dirname(os.path.abspath(__file__))
tag, secs = sys.argv[1], float(sys.argv[2])
wps = np.array([tuple(map(float, w.split(","))) for a in sys.argv[3:] for w in a.split(":") if w])
pad = os.path.join(HERE, "runs", tag + ".pad.txt")
gain = float(os.environ.get("AP_GAIN", "60000"))
look = float(os.environ.get("AP_LOOK", "10"))
vmax = float(os.environ.get("AP_VMAX", "11"))
stop = os.environ.get("AP_STOP") == "1"
seg = np.diff(wps, axis=0)
sl = np.hypot(seg[:, 0], seg[:, 1])
cum = np.concatenate([[0], np.cumsum(sl)])
h = memsnap.attach(memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe')))
me = memsnap.u32(h, 0x17B0DC) + 0x6C
pos = lambda: np.array(struct.unpack("<3f", memsnap.read(h, me, 12)), np.float64)


def along(p, k0):
    """nearest point on segments k0.. (a few ahead): (segment, distance along the path)"""
    best = None
    for k in range(k0, min(k0 + 4, len(seg))):
        a = wps[k]
        u = np.clip(np.dot(p - a, seg[k]) / sl[k] ** 2, 0, 1)
        d = np.hypot(*(a + u * seg[k] - p))
        if best is None or d < best[0]:
            best = (d, k, cum[k] + u * sl[k])
    return best[1], best[2]


def at(s):
    s = min(s, cum[-1])
    k = min(int(np.searchsorted(cum, s, side="right")) - 1, len(seg) - 1)
    return wps[k] + seg[k] * (s - cum[k]) / sl[k]


hist = []
k = 0
t0 = time.time()
while time.time() - t0 < secs:
    t = time.time() - t0
    p = pos()
    hist.append((t, p))
    hist = [q for q in hist if t - q[0] <= 0.25]
    k, s = along(np.array([p[0], p[2]]), k)
    lx = 0
    sp = 0.0
    thr = 255
    if len(hist) > 1:
        d = hist[-1][1] - hist[0][1]
        dt = hist[-1][0] - hist[0][0]
        sp = math.hypot(d[0], d[2]) / dt if dt > 0 else 0
        if sp > 1.0:
            cur = math.atan2(d[0], d[2])
            tg = at(s + look) if s + look <= cum[-1] else wps[-1] + (wps[-1] - wps[-2]) * 10
            want = math.atan2(tg[0] - p[0], tg[1] - p[2])
            e = (want - cur + math.pi) % (2 * math.pi) - math.pi
            lx = int(max(-32767, min(32767, e * gain)))
            if sp > vmax or abs(e) > 0.5 or (stop and s >= cum[-1] - 0.5):
                thr = 0
    if stop and s >= cum[-1] - 0.5:
        thr = 0                         # AP_STOP: never again once at the end
    open(pad, "w").write("a=%d ly=32767 lx=%d\n" % (thr, lx))
    print("ap %.2f %.3f %.3f %.3f %.1f %d %.1f" % (t, p[0], p[1], p[2], sp, lx, s), flush=True)
    time.sleep(0.05)
open(pad, "w").write("\n")
