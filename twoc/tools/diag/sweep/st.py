"""st.py TAG LINES -- step 78: write live settings to runs/TAG/sub.trig (the RECOMP_D3D_BRIDGE_SUBDUMP=@
trigger file), as "set KEY VALUE" lines: LINES like "aa_msaa4x;render_1920x1080;aspect_21/9" ('_' for
spaces, ';' between lines, '/' for ':', since act.py's x: splits on ':')."""
import os, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
d = os.path.join(HERE, "runs", sys.argv[1])
os.makedirs(d, exist_ok=True)
lines = ["set " + l.replace("_", " ").replace("/", ":") for l in sys.argv[2].split(";") if l]
tmp = os.path.join(d, "sub.tmp")
open(tmp, "w").write("\n".join(lines) + "\n")
os.replace(tmp, os.path.join(d, "sub.trig"))
print(time.strftime("%H:%M:%S"), "trigger:", "; ".join(lines), flush=True)
