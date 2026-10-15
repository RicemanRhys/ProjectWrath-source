"""gopad2.py TAG LEVEL [ROOM:PAD] -- from the hub spawn: set that pad's level to LEVEL (table index), walk onto it,
wait until the level has loaded. Default pad 1:1."""
import math, os, re, subprocess, sys, time
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
HERE = os.path.dirname(os.path.abspath(__file__))
tag, level = sys.argv[1], int(sys.argv[2])
room, pk = map(int, (sys.argv[3] if len(sys.argv) > 3 else "1:1").split(":"))
errp = os.path.join(HERE, "runs", tag + ".err.log")
h = memsnap.attach(memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe')))
import struct
RING = 4.4
ANG = {5: 150, 4: -150, 3: -90, 2: -30, 1: 30}
ORDER = [5, 4, 3, 2, 1]
def py(*a):
    return subprocess.run([sys.executable] + list(a), cwd=HERE, capture_output=True, text=True).stdout
def pos():
    return struct.unpack_from("<3f", memsnap.read(h, 0x8F5120, 0x180), 0x6C)
def room_pts(r):
    p = memsnap.u32(h, 0x19C118 + 24 * (r + 1))
    hdr = memsnap.read(h, p, 16)
    n = struct.unpack_from("<h", hdr, 2)[0]
    arr = struct.unpack_from("<I", hdr, 8)[0]
    d = memsnap.read(h, arr, 12 * n)
    return [struct.unpack_from("<3f", d, 12 * i) for i in range(n)]
def steer(x, z, secs=15):
    r = py("steer.py", tag, "%.2f" % x, "%.2f" % z, str(secs))
    last = [l for l in r.splitlines() if "pos (" in l]
    return last[-1] if last else r.strip()[-200:]
def ring(a):
    return RING * math.cos(math.radians(a)), RING * math.sin(math.radians(a))
for _ in range(60):
    try:
        if len(room_pts(1)) == 12:
            break
    except Exception:
        pass
    time.sleep(1)
print(py("poke.py", "%X" % (0x19A7F0 + 12 * (room - 1) + (pk - 1)), "1", str(level)).strip())
x, y, z = pos()
for k in range(0, ORDER.index(room) + 1):
    print("  ring", ORDER[k], steer(*ring(ANG[ORDER[k]])))
pts = room_pts(room)
print("  gate", steer((pts[0][0] + pts[1][0]) / 2, (pts[0][2] + pts[1][2]) / 2))
px, _, pz = pts[3 + 2 * (pk - 1)]
p0 = os.path.getsize(errp)
print("  pad", steer(px, pz))
x, y, z = pos()
if (x - px) ** 2 + (z - pz) ** 2 > 0.5:     # step 65: the walk stalled: put Crash on the pad
    fx = struct.unpack("<I", struct.pack("<f", px))[0]
    fz = struct.unpack("<I", struct.pack("<f", pz))[0]
    print("  placed", py("poke.py", "%X" % (0x8F5120 + 0x6C), "4", str(fx), "%X" % (0x8F5120 + 0x74), "4", str(fz)).strip(), flush=True)
t0 = time.time()
while time.time() - t0 < 40:
    txt = open(errp, "rb").read()[p0:].decode("latin1")
    if re.search(r"\.nux", txt):
        break
    time.sleep(0.5)
m = re.findall(r"levels\\([a-z])\\(\w+)\\\w+\.nux", txt)
print("  loading", m[-1] if m else "?", "after %.0f s" % (time.time() - t0), flush=True)
