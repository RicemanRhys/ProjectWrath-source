"""down.py RUNDIR OUTDIR -- each hostsub*.bmp of RUNDIR as a 480x270 PNG in OUTDIR (box filter)."""
import glob, os, sys
from PIL import Image
for f in sorted(glob.glob(os.path.join(sys.argv[1], "hostsub*.bmp"))):
    Image.open(f).convert("RGB").resize((480, 270), Image.BOX).save(os.path.join(sys.argv[2], os.path.basename(f)[:-4] + ".png"))
