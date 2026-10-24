"""darkmap.py DIR [W] [THR] -- per frame: pixels darker than the median of frames i-W..i+W by more than THR
(sum of RGB/3), with the bounding box of the largest such cluster. Prints one line per frame with any."""
import sys, os, glob
import numpy as np
from PIL import Image
from scipy import ndimage
d = sys.argv[1]
W = int(sys.argv[2]) if len(sys.argv) > 2 else 6
THR = float(sys.argv[3]) if len(sys.argv) > 3 else 18
MIN = int(sys.argv[4]) if len(sys.argv) > 4 else 12
fs = sorted(glob.glob(os.path.join(d, "*.bmp")) + glob.glob(os.path.join(d, "*.png")))
REG = [int(v) for v in os.environ['REGION'].split(',')] if os.environ.get('REGION') else None
ims = np.stack([np.asarray(Image.open(f).convert("RGB"), dtype=np.float32).mean(axis=2) for f in fs])
n = len(ims)
hits = []
for i in range(n):
    a, b = max(0, i - W), min(n, i + W + 1)
    med = np.median(ims[a:b], axis=0)
    m = (med - ims[i]) > THR
    if REG:
        z = np.zeros_like(m); z[REG[1]:REG[3], REG[0]:REG[2]] = m[REG[1]:REG[3], REG[0]:REG[2]]; m = z
    m = ndimage.binary_opening(m)
    lab, k = ndimage.label(m)
    if k == 0: continue
    sz = ndimage.sum(m, lab, range(1, k + 1))
    j = int(np.argmax(sz)) + 1
    if sz[j - 1] < MIN: continue
    ys, xs = np.where(lab == j)
    dk = (med - ims[i])[lab == j].mean()
    bright = med[lab == j].mean()
    hits.append(i)
    print("%s  px %4d  box x %3d-%3d y %3d-%3d  darker by %.0f of %.0f (%.0f%%)" % (os.path.basename(fs[i])[:22], sz[j - 1], xs.min(), xs.max(), ys.min(), ys.max(), dk, bright, 100 * dk / max(bright, 1)))
print("frames with a dark cluster: %d of %d" % (len(hits), n))
