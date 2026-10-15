"""rmw.py ADDR AND OR [ADDR AND OR ...] -- 32-bit read-modify-write of guest memory: v = (v & AND) | OR (hex or int)."""
import os, sys
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
h = memsnap.attach(memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe')))
HERE = os.path.dirname(os.path.abspath(__file__))
a = sys.argv[1:]
for i in range(0, len(a), 3):
    va, an, o = int(a[i], 16), int(a[i + 1], 0), int(a[i + 2], 0)
    v = memsnap.u32(h, va)
    nv = (v & an) | o
    __import__("subprocess").run([sys.executable, os.path.join(HERE, "poke.py"), "%X" % va, "4", str(nv)])
    print("%08X: %08X -> %08X" % (va, v, nv))
