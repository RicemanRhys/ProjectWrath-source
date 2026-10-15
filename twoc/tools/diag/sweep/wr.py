"""wr.py TAG TEXT -- step 75: write TEXT ('_' for spaces) to runs/TAG.trig (the SUBDUMP=@ trigger file)."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
open(os.path.join(HERE, "runs", sys.argv[1] + ".trig"), "w").write(sys.argv[2].replace("_", " ") + "\n")
