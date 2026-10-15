"""glide.py TAG SECS Z0 Z1 SPEED -- step 76: move Crash back and forth along z between Z0 and Z1 at SPEED units/s
for SECS seconds by writing his position (0x8F5120 + 0x6C: x, y, z), no pad input, so the camera follows as in
play while nobody plays (deterministic from run to run). x and y stay as they were at the start. Logs the
frame counter and z every 0.5 s to runs/TAG.glide.txt."""
import ctypes, ctypes.wintypes as wt, os, struct, sys, time
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap

K32 = memsnap.K32
K32.WriteProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                   ctypes.POINTER(ctypes.c_size_t)]
tag, secs, z0, z1, speed = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4]), float(sys.argv[5])
HERE = os.path.dirname(os.path.abspath(__file__))
pid = memsnap.find_pid(os.environ.get("TWOC_EXE", "crash_twoc.exe"))
h = K32.OpenProcess(0x0438, False, pid)
POS = 0x8F5120 + 0x6C
x, y, z = struct.unpack("<3f", memsnap.read(h, POS, 12))
log = open(os.path.join(HERE, "runs", tag + ".glide.txt"), "w")
log.write("start %.3f %.3f %.3f\n" % (x, y, z))
span = abs(z1 - z0)
t0 = time.perf_counter()
nxt = 0.0
got = ctypes.c_size_t()
while True:
    t = time.perf_counter() - t0
    if t >= secs:
        break
    d = max(0.0, min(span, abs(z - z0))) + speed * t    # along the back-and-forth path, from where he stood
    ph = d % (2 * span)
    zz = z0 + (ph if ph < span else 2 * span - ph) * (1 if z1 > z0 else -1)
    b = struct.pack("<3f", x, y, zz)
    K32.WriteProcessMemory(h, ctypes.c_void_p(POS + memsnap.OFFSET), b, 12, ctypes.byref(got))
    if t >= nxt:
        rx, ry, rz = struct.unpack("<3f", memsnap.read(h, POS, 12))
        log.write("%.2f %d %.3f %.3f %.3f\n" % (t, memsnap.u32(h, 0x25A660), rx, ry, rz))
        nxt += 0.5
    time.sleep(0.004)
log.close()
print("glide %s: %.1f s, z %.1f..%.1f at %.1f u/s" % (tag, secs, z0, z1, speed))
