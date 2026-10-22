"""findpos.py TAG [TOL] -- step 100: every float triple in guest RAM (0x10000-0x4000000) within TOL (default 0.5) of
Crash's position ([0x17B0DC] + 0x6C), to find what else holds where he is (a ball he rides). Appends to
runs/TAG.find.txt."""
import os, struct, sys
import numpy as np
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
HERE = os.path.dirname(os.path.abspath(__file__))
tag = sys.argv[1]
tol = float(sys.argv[2]) if len(sys.argv) > 2 else 0.5
h = memsnap.attach(memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe')))
me = memsnap.u32(h, 0x17B0DC) + 0x6C
p = np.array(struct.unpack("<3f", memsnap.read(h, me, 12)), np.float32)
out = open(os.path.join(HERE, "runs", tag + ".find.txt"), "a")
out.write("crash at %08X %s\n" % (me, p))
CH = 1 << 20
for base in range(0x10000, 0x4000000, CH):
    b = memsnap.read(h, base, CH + 8)
    if not b:
        continue
    a = np.frombuffer(b[:len(b) // 4 * 4], np.float32)
    with np.errstate(invalid="ignore"):
        m = (np.abs(a[:-2] - p[0]) < tol) & (np.abs(a[1:-1] - p[1]) < tol) & (np.abs(a[2:] - p[2]) < tol)
    for i in np.nonzero(m)[0]:
        va = base + 4 * int(i)
        if va != me:
            out.write("  %08X %s\n" % (va, a[i:i + 3]))
out.close()
