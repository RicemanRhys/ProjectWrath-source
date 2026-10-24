"""trig.py TAG NAME [TEXT] -- step 105: write runs/TAG/NAME (a trigger file) with TEXT (default 1)."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
open(os.path.join(HERE, "runs", sys.argv[1], sys.argv[2]), "w").write(sys.argv[3] if len(sys.argv) > 3 else "1")
