"""ballmove.py X Y Z [REPEAT_S] [TOL] -- step 103: teleport Crash where something else drives his position (Medieval
Madness's ball). Run while he moves (a stick held by the step before): every float triple in guest RAM within TOL
(default 3) of his position ([0x17B0DC] + 0x6C) is noted, again 0.4 s later, and only the triples that moved with him
(the same step to 0.05, a step over 0.05) are kept; then his and theirs are written to X Y Z plus each one's offset
from him, every 5 ms for REPEAT_S seconds (default 3). Prints what it kept and where he is after."""
import ctypes, os, struct, sys, time
import numpy as np
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
x, y, z = map(float, sys.argv[1:4])
rep = float(sys.argv[4]) if len(sys.argv) > 4 else 3.0
tol = float(sys.argv[5]) if len(sys.argv) > 5 else 3.0
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
print("crash at %08X %s moved %s; %d of %d triples moved with him: %s" % (
    me, p1, dme, len(keep), len(cand), " ".join("%08X%+.2f,%+.2f,%+.2f" % (va, *o) for va, o in keep[:40])), flush=True)
hw = memsnap.K32.OpenProcess(0x0438, False, pid)
got = ctypes.c_size_t()
tgt = np.array([x, y, z])
t0 = time.time()
while time.time() - t0 < rep:
    memsnap.K32.WriteProcessMemory(hw, ctypes.c_void_p(me + memsnap.OFFSET), struct.pack("<3f", *tgt), 12, ctypes.byref(got))
    for va, o in keep:
        memsnap.K32.WriteProcessMemory(hw, ctypes.c_void_p(va + memsnap.OFFSET), struct.pack("<3f", *(tgt + o)), 12,
                                       ctypes.byref(got))
    time.sleep(0.005)
time.sleep(0.5)
print("ballmove done; crash now %s" % pos(me), flush=True)
