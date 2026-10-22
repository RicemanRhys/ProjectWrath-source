"""patch.py DIR [X0 Y0 X1 Y1] -- step 100 item 5: the brick decal on Gold Rush's wall (default box 880-1020 x 150-300 at
1920x1080) against the plain wall left of it (760-860): mean grey of each dump in order, and the dumps where the decal
is missing (the patch as bright as the wall, within 3 levels)."""
import glob, re, sys
import numpy as np
from PIL import Image
d = sys.argv[1]
x0, y0, x1, y1 = map(int, sys.argv[2:6]) if len(sys.argv) > 5 else (880, 150, 1020, 300)
fs = sorted(glob.glob(d + "/hostsub*.bmp")) or sorted(glob.glob(d + "/host[0-9]*.bmp"))
miss = []
for f in fs:
    a = np.asarray(Image.open(f).convert("L"), np.float32)
    p, w = a[y0:y1, x0:x1].mean(), a[y0:y1, x0 - 120:x0 - 20].mean()
    tag = re.sub(r".*host", "", f)
    if w - p < 3:
        miss.append(tag)
    print("%s patch %.1f wall %.1f diff %.1f" % (tag, p, w, w - p))
print("dumps %d, decal missing in %d: %s" % (len(fs), len(miss), " ".join(miss[:40])))
