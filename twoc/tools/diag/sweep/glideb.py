"""glideb.py TAG SECS Z0 Z1 SPEED -- glide.py started in the background (act.py goes on to its next step, e.g. the
hs2.py sampler while Crash glides)."""
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
subprocess.Popen([sys.executable, os.path.join(HERE, "glide.py")] + sys.argv[1:], cwd=HERE,
                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL)
print("glide started")
