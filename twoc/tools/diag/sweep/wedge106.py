"""wedge.py DIR [THR] [MINPX] -- step 106: short-lived regions that camera motion doesn't explain, light or dark.
For each picture i and span k (KS, default 1): the pictures i-k and i+k are warped onto i by dense optical flow
(OpenCV DIS); a pixel is flagged when it differs from BOTH warped neighbours by more than THR (grey levels, default
24) with the same sign (a region there only around i: what a frame or two of a stray triangle gives; a region
uncovered by motion matches one side). The largest flagged cluster per picture (after an opening) of at least MINPX
pixels (default 150 at 480x270) is printed with its box, mean difference, colour and flatness (std of grey inside).
MASK=x0,y0,x1,y1;... rectangles ignored (default: the HUD's head and counters at 480x270). FROM/TO picture range."""
import sys, os, glob
import numpy as np
import cv2
from scipy import ndimage
from PIL import Image
d = sys.argv[1]
THR = float(sys.argv[2]) if len(sys.argv) > 2 else 24
MINPX = int(sys.argv[3]) if len(sys.argv) > 3 else 150
KS = [int(v) for v in os.environ.get("KS", "1").split(",")]
fs = sorted(glob.glob(os.path.join(d, "*.bmp")) + glob.glob(os.path.join(d, "*.png")))
fs = fs[int(os.environ.get("FROM", 0)):int(os.environ.get("TO", 10 ** 9))]
ims = [np.asarray(Image.open(f).convert("RGB")) for f in fs]
H, W = ims[0].shape[:2]
gr = [cv2.cvtColor(i, cv2.COLOR_RGB2GRAY) for i in ims]
mask = np.ones((H, W), bool)
sx, sy = W / 480.0, H / 270.0
for r in os.environ.get("MASK", "0,190,110,270;0,0,110,50").split(";"):
    if r:
        x0, y0, x1, y1 = map(float, r.split(","))
        mask[int(y0 * sy):int(y1 * sy), int(x0 * sx):int(x1 * sx)] = False
dis = cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_MEDIUM)
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)


def warp(i, j):
    """picture j seen from i's pixels (flow from i to j)"""
    fl = dis.calc(gr[i], gr[j], None)
    return cv2.remap(gr[j], xx + fl[..., 0], yy + fl[..., 1], cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)


hits = 0
for i in range(len(ims)):
    best = None
    for k in KS:
        if i - k < 0 or i + k >= len(ims):
            continue
        g = gr[i].astype(np.float32)
        a = g - warp(i, i - k).astype(np.float32)
        b = g - warp(i, i + k).astype(np.float32)
        m = (((a > THR) & (b > THR)) | ((a < -THR) & (b < -THR))) & mask
        m = ndimage.binary_opening(m, iterations=2)
        lab, n = ndimage.label(m)
        if n == 0:
            continue
        sz = ndimage.sum(m, lab, range(1, n + 1))
        j = int(np.argmax(sz)) + 1
        if sz[j - 1] < MINPX * sx * sy:
            continue
        sel = lab == j
        if best is None or sz[j - 1] > best[0]:
            best = (sz[j - 1], k, sel, (0.5 * (a + b))[sel].mean())
    if best is None:
        continue
    hits += 1
    sz, k, sel, dv = best
    ys, xs = np.where(sel)
    col = ims[i][sel].mean(axis=0)
    flat = gr[i][sel].std()
    print("%s k%d px %6d box x %3d-%3d y %3d-%3d %s %+5.0f rgb %3.0f %3.0f %3.0f std %4.1f" % (
        os.path.basename(fs[i])[:24], k, sz, xs.min(), xs.max(), ys.min(), ys.max(), "LIGHT" if dv > 0 else "dark ",
        dv, col[0], col[1], col[2], flat), flush=True)
print("pictures %d, flagged %d (THR %g, MINPX %d, KS %s)" % (len(ims), hits, THR, MINPX, KS))
