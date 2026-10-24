"""drv.py MODE X0 Y Z0 [X1 Z1 SPEED] [LOG_S] -- step 106: put Smokey's vehicle on the road and drive it.
Run while it moves (A held by the step before), as ballmove.py: every float triple in guest RAM within 3 of Crash's
position ([0x17B0DC] + 0x6C) that moves with him over 0.3 s is kept (the vehicle's copies).
  once: write (X0, Y, Z0) to him and the copies for 0.25 s, then let go (the game drives on with A held);
  path: write a point moving from (X0, Y, Z0) to (X1, Y, Z1) at SPEED units a second, every 5 ms, then let go.
Then prints his position every 0.1 s for LOG_S seconds (default 10) as 'pos T X Y Z'."""
import ctypes, os, struct, sys, time
import numpy as np
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
mode = sys.argv[1]
x0, y, z0 = map(float, sys.argv[2:5])
if mode == "path":
    x1, z1, spd = map(float, sys.argv[5:8]); rest = sys.argv[8:]
else:
    rest = sys.argv[5:]
log_s = float(rest[0]) if rest else 10.0
tol = 3.0
pid = memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe'))
h = memsnap.attach(pid)
me = memsnap.u32(h, 0x17B0DC) + 0x6C


def pos(va):
    return np.array(struct.unpack("<3f", memsnap.read(h, va, 12)), np.float64)


p0 = pos(me)
cand = []
CH = 1 << 20
for base in range(0x10000, 0x4000000, CH):
    b = memsnap.read(h, base, CH + 8)
    if not b:
        continue
    a = np.frombuffer(b[:len(b) // 4 * 4], np.float32)
    with np.errstate(invalid="ignore"):
        m = (np.abs(a[:-2] - p0[0]) < tol) & (np.abs(a[1:-1] - p0[1]) < tol) & (np.abs(a[2:] - p0[2]) < tol)
    cand += [base + 4 * int(i) for i in np.nonzero(m)[0]]
v0 = {va: pos(va) for va in cand}
pA = pos(me)
time.sleep(0.3)
v1 = {va: pos(va) for va in cand}
p1 = pos(me)
dme = p1 - pA
keep = []
for va in cand:
    if va == me:
        continue
    d = v1[va] - v0[va]
    if np.linalg.norm(d) > 0.05 and np.linalg.norm(d - dme) < 0.1:
        keep.append((va, v1[va] - p1))
print("crash at %08X %s moved %s; %d of %d triples kept" % (me, p1, dme, len(keep), len(cand)), flush=True)
hw = memsnap.K32.OpenProcess(0x0438, False, pid)
got = ctypes.c_size_t()


def put(t):
    memsnap.K32.WriteProcessMemory(hw, ctypes.c_void_p(me + memsnap.OFFSET), struct.pack("<3f", *t), 12, ctypes.byref(got))
    for va, o in keep:
        memsnap.K32.WriteProcessMemory(hw, ctypes.c_void_p(va + memsnap.OFFSET), struct.pack("<3f", *(t + o)), 12,
                                       ctypes.byref(got))


t0 = time.time()
if mode == "once":
    while time.time() - t0 < 0.25:
        put(np.array([x0, y, z0])); time.sleep(0.005)
else:
    a, b = np.array([x0, y, z0]), np.array([x1, y, z1])
    L = np.linalg.norm(b - a)
    while True:
        s = (time.time() - t0) * spd
        if s >= L:
            break
        put(a + (b - a) * s / L); time.sleep(0.005)
print("let go at +%.2f s; crash %s" % (time.time() - t0, pos(me)), flush=True)
t1 = time.time()
while time.time() - t1 < log_s:
    print("pos %.2f %.3f %.3f %.3f" % ((time.time() - t1,) + tuple(pos(me))), flush=True)
    time.sleep(0.1)
