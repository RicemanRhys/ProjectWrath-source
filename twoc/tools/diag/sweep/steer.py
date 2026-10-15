"""steer.py TAG X Z [MAXSECONDS] -- walk Crash toward world (X, Z).

The stick is camera-relative, so the mapping stick -> world is estimated from
each short step (the angle between the stick vector and the displacement it
produced) and the next stick vector is rotated by it. Stops when within 0.4 of
the target, when stuck, or at MAXSECONDS. Prints the track and new level files.
"""
import math, os, struct, sys, time
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
tag, tx, tz = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
maxs = float(sys.argv[4]) if len(sys.argv) > 4 else 15
out = os.path.join(HERE, "runs")
pad = os.path.join(out, tag + ".pad.txt")
errp = os.path.join(out, tag + ".err.log")
h = memsnap.attach(memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe')))
pos0 = os.path.getsize(errp)


def st():
    b = memsnap.read(h, 0x8F5120, 0x180)
    x, y, z = struct.unpack_from("<3f", b, 0x6C)
    return x, y, z, b[0x172]


theta = None          # world angle = stick angle + theta
t0 = time.time()
stuck = 0
x, y, z, s = st()
while time.time() - t0 < maxs:
    dx, dz = tx - x, tz - z
    dist = math.hypot(dx, dz)
    if dist < 0.4:
        break
    want = math.atan2(dz, dx)                  # world angle to target
    sa = want - theta if theta is not None else math.pi / 2
    lx, ly = int(32767 * math.cos(sa)), int(32767 * math.sin(sa))
    open(pad, "w").write("lx=%d ly=%d\n" % (lx, ly))
    time.sleep(0.25)
    nx, ny, nz, ns = st()
    mdx, mdz = nx - x, nz - z
    moved = math.hypot(mdx, mdz)
    if moved > 0.15:
        est = math.atan2(mdz, mdx) - sa
        theta = est if theta is None else math.atan2(math.sin(theta) + math.sin(est), math.cos(theta) + math.cos(est))
        stuck = 0
    else:
        stuck += 1
    print("%5.2f pos (%7.2f %6.2f %7.2f) st %d dist %6.2f moved %.2f" % (time.time() - t0, nx, ny, nz, ns, dist, moved), flush=True)
    x, y, z, s = nx, ny, nz, ns
    if stuck >= 6:
        print("stuck")
        break
open(pad, "w").write("\n")
with open(errp, "rb") as fh:
    fh.seek(pos0)
    for l in fh.read().decode("latin1").splitlines():
        if ("[PATH]" in l and "levels" in l) or "CRASH" in l or "[GAME]" in l:
            print(l.strip())
