"""hubnear.py TAG LEVEL ROOM:PAD -- step 101: like gopad2.py, but stops short of the pad (NEAR_F of the way from the gate)."""
import math, os, re, subprocess, sys, time
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
HERE = os.path.dirname(os.path.abspath(__file__))
tag, level = sys.argv[1], int(sys.argv[2])
room, pk = (int(sys.argv[3]), int(sys.argv[4])) if len(sys.argv) > 4 else map(int, (sys.argv[3] if len(sys.argv) > 3 else "1:1").split(":"))
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
# step 101: walk into the room and stop short of pad PK (fraction F of the way from the gate), no level byte poked
F = float(os.environ.get("NEAR_F", "0.6"))
x, y, z = pos()
for k in range(0, ORDER.index(room) + 1):
    print("  ring", ORDER[k], steer(*ring(ANG[ORDER[k]])))
pts = room_pts(room)
gx, gz = (pts[0][0] + pts[1][0]) / 2, (pts[0][2] + pts[1][2]) / 2
print("  gate", steer(gx, gz))
px, _, pz = pts[3 + 2 * (pk - 1)]
print("  near pad", steer(gx + F * (px - gx), gz + F * (pz - gz)))
print("  pos", pos(), flush=True)
