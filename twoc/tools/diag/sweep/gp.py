"""gp.py TAG LEVEL ROOM PAD -- gopad2.py TAG LEVEL ROOM:PAD (act.py's x: splits on ':')."""
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
subprocess.run([sys.executable, "-u", os.path.join(HERE, "gopad2.py"), sys.argv[1], sys.argv[2], sys.argv[3] + ":" + sys.argv[4]])
