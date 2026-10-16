"""vp.py TAG WHICH LINE -- step 82: write a pad line for a chord run: WHICH v = runs/TAG.vpad.txt (SDL virtual pad,
RECOMP_DEV_PAD_FILE), x = runs/TAG.xi.txt (XInput user 0's stand-in, RECOMP_DEV_XINPUT_FILE). '_' for spaces."""
import os, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
tag, which, line = sys.argv[1], sys.argv[2], sys.argv[3].replace("_", " ")
p = os.path.join(HERE, "runs", tag + (".vpad.txt" if which == "v" else ".xi.txt"))
open(p + ".tmp", "w").write(line + "\n")
os.replace(p + ".tmp", p)
print("%.2f vp %s %s" % (time.time(), which, line), flush=True)
