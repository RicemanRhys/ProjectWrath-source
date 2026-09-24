"""Watch Crash in the hub (notes 18.30). Drives port 0 through
RECOMP_DEV_INPUT_FILE (5 s each: idle, right, down, left, up, idle) and prints
the player object 0x8F5120 once a second: position +0x6C (x, y, z) and the
movement state byte +0x172 (3 standing, 2 moving, 0 seen on a jump/slope).

  python crashpos.py INPUT_FILE

The game must already be running with RECOMP_DEV_INPUT_FILE=INPUT_FILE.
"""
import math, struct, sys, time
import memsnap

h = memsnap.attach()
P = sys.argv[1]

def st():
    b = memsnap.read(h, 0x8F5120, 0x180)
    return memsnap.u32(h, 0x25A660), struct.unpack_from("<3f", b, 0x6C), b[0x172]

prev = st()
for ph, line in [("idle", ""), ("right", "lx=32767"), ("down", "ly=-32767"), ("left", "lx=-32767"), ("up", "ly=32767"), ("idle", "")]:
    open(P, "w").write(line + "\n")
    for k in range(5):
        time.sleep(1.0)
        cur = st()
        d = math.dist(cur[1], prev[1]); df = max(1, cur[0] - prev[0])
        print("%-5s fr %5d pos (%8.3f %8.3f %8.3f) state %d  moved %.3f in %d frames (%.4f/frame)"
              % (ph, cur[0], *cur[1], cur[2], d, df, d / df), flush=True)
        prev = cur
open(P, "w").write("\n")
