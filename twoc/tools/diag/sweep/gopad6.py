"""gopad6.py TAG PAD [LEVEL] -- step 73: from the hub spawn, ride the central lift upstairs (warp room 6, the bonus
room), walk to the room's gate and onto pad PAD (its own level unless LEVEL is given), wait for the level load.
Prints the path points of room 6 and where Crash went."""
import os, re, struct, subprocess, sys, time
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
HERE = os.path.dirname(os.path.abspath(__file__))
tag, pk = sys.argv[1], int(sys.argv[2])
errp = os.path.join(HERE, "runs", tag + ".err.log")
h = memsnap.attach(memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe')))


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


if len(sys.argv) > 3:
    print(py("poke.py", "%X" % (0x19A7F0 + 12 * 5 + (pk - 1)), "1", sys.argv[3]).strip())
print("start", pos(), flush=True)
print("lift", steer(0, 0, 12), flush=True)
t0 = time.time()
while time.time() - t0 < 30 and pos()[1] < 30:
    time.sleep(0.5)
time.sleep(2)
print("upstairs", pos(), "after %.0f s" % (time.time() - t0), flush=True)
pts = room_pts(6)
for i, p in enumerate(pts):
    print("  pt", i, "(%.2f %.2f %.2f)" % p)
print("  gate", steer((pts[0][0] + pts[1][0]) / 2, (pts[0][2] + pts[1][2]) / 2), flush=True)
px, _, pz = pts[3 + 2 * (pk - 1)]
p0 = os.path.getsize(errp)
print("  pad", steer(px, pz), flush=True)
x, y, z = pos()
if (x - px) ** 2 + (z - pz) ** 2 > 0.5:
    fx = struct.unpack("<I", struct.pack("<f", px))[0]
    fz = struct.unpack("<I", struct.pack("<f", pz))[0]
    print("  placed", py("poke.py", "%X" % (0x8F5120 + 0x6C), "4", str(fx), "%X" % (0x8F5120 + 0x74), "4", str(fz)).strip(), flush=True)
t0 = time.time()
txt = ""
while time.time() - t0 < 40:
    txt = open(errp, "rb").read()[p0:].decode("latin1")
    if re.search(r"\.nux", txt):
        break
    time.sleep(0.5)
m = re.findall(r"levels\\([a-z])\\(\w+)\\\w+\.nux", txt)
print("  loading", m[-1] if m else "?", "after %.0f s" % (time.time() - t0), flush=True)
