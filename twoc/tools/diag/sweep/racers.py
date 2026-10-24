"""racers.py OUT SECS [WAIT] -- step 106: Smokey's AI racers' paths. Two snapshots of guest RAM 0.3 s apart (after WAIT
s, default 4): float triples inside the course's box (|x| < 80, -3 < y < 10, -50 < z < 900) that moved 1.5-8 units
across with |dy| < 1 are kept (one per distinct position); then every 0.1 s for SECS each is read and written to OUT
as 't addr x y z' lines."""
import os, struct, sys, time
import numpy as np
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
out, secs = sys.argv[1], float(sys.argv[2])
time.sleep(float(sys.argv[3]) if len(sys.argv) > 3 else 4)
h = memsnap.attach(memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe')))
CH = 1 << 20


def snap():
    r = {}
    for base in range(0x10000, 0x4000000, CH):
        b = memsnap.read(h, base, CH + 8)
        if b:
            r[base] = np.frombuffer(b[:len(b) // 4 * 4], np.float32).copy()
    return r


A = snap(); time.sleep(0.3); B = snap()
keep = []
for base, a in A.items():
    b = B.get(base)
    if b is None or len(b) != len(a):
        continue
    with np.errstate(invalid="ignore", over="ignore"):
        ax, ay, az, bx, by, bz = a[:-2], a[1:-1], a[2:], b[:-2], b[1:-1], b[2:]
        inbox = (np.abs(ax) < 80) & (ay > -3) & (ay < 10) & (az > -50) & (az < 900)
        mv = np.hypot(bx - ax, bz - az)
        m = inbox & (mv > 1.5) & (mv < 8) & (np.abs(by - ay) < 1)
    for i in np.nonzero(m)[0]:
        keep.append((base + 4 * int(i), (float(bx[i]), float(by[i]), float(bz[i]))))
uniq = {}
for va, p in keep:
    key = (round(p[0], 1), round(p[2], 1))
    uniq.setdefault(key, va)
print("%d moving triples, %d distinct" % (len(keep), len(uniq)), flush=True)
vas = sorted(uniq.values())
t0 = time.time()
with open(out, "w") as f:
    while time.time() - t0 < secs:
        t = time.time() - t0
        for va in vas:
            x, y, z = struct.unpack("<3f", memsnap.read(h, va, 12))
            f.write("%.2f %08X %.3f %.3f %.3f\n" % (t, va, x, y, z))
        f.flush()
        time.sleep(0.1)
print("done", flush=True)
