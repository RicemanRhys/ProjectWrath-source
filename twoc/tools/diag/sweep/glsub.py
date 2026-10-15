"""glsub.py TAG SECS Z0 Z1 SPEED AT N -- step 77: glide.py TAG SECS Z0 Z1 SPEED, and AT s into it write N to
runs/TAG/sub.trig (the RECOMP_D3D_BRIDGE_SUBDUMP=@ trigger: the next N game frames' sub-frames dumped)."""
import os, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
tag, secs, z0, z1, sp, at, n = sys.argv[1:8]
p = subprocess.Popen([sys.executable, os.path.join(HERE, "glide.py"), tag, secs, z0, z1, sp])
time.sleep(float(at))
os.makedirs(os.path.join(HERE, "runs", tag), exist_ok=True)
open(os.path.join(HERE, "runs", tag, "sub.trig"), "w").write(n)
print("trigger", n, flush=True)
p.wait()
