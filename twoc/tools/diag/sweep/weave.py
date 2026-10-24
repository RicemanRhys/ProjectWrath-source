"""weave.py TAG [SECS] -- step 104: hold A and steer left and right in turn (1.2 s each) through the pad file."""
import os, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
pad = os.path.join(HERE, "runs", sys.argv[1] + ".pad.txt")
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 16
t0 = time.time(); k = 0
while time.time() - t0 < secs:
    lx = (12000, 0, -12000, 0)[k % 4]
    open(pad, "w").write("a=255 ly=32767 lx=%d\n" % lx)
    time.sleep(1.2); k += 1
open(pad, "w").write("\n")
