"""peek.py ADDR... -- print guest u32s (hex addrs)."""
import os, sys
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
h = memsnap.attach(memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe')))
print(" ".join("%s=%08X" % (a, memsnap.u32(h, int(a, 16))) for a in sys.argv[1:]), flush=True)
