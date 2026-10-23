"""rollspikes.py DIR [N] -- step 104: for a rolling capture's frames folder (or any folder of .bmp/.png pictures in
order): frames (or sub-frames) that differ from both neighbours more than the neighbours differ from
each other: a short-lived region. Per frame: score = min(d(i-1,i), d(i,i+1)) - d(i-1,i+1) over 16x16 tiles (the worst
tile), and the dark share of that tile's change."""
import sys, os, glob
import numpy as np
from PIL import Image
fs = sorted(glob.glob(os.path.join(sys.argv[1], "*.png")) + glob.glob(os.path.join(sys.argv[1], "*.bmp")))
N = int(sys.argv[2]) if len(sys.argv) > 2 else 15
T = 16
ims = [np.asarray(Image.open(f).convert("RGB"), dtype=np.float32) for f in fs]
def tiles(d):
    h, w = d.shape[:2]
    d = d[:h // T * T, :w // T * T].mean(axis=2)
    return d.reshape(h // T, T, w // T, T).mean(axis=(1, 3))
rows = []
for i in range(1, len(ims) - 1):
    a, b, c = ims[i - 1], ims[i], ims[i + 1]
    dab, dbc, dac = tiles(abs(a - b)), tiles(abs(b - c)), tiles(abs(a - c))
    s = np.minimum(dab, dbc) - dac
    k = np.unravel_index(np.argmax(s), s.shape)
    # darker than both neighbours there?
    y, x = k[0] * T, k[1] * T
    lum = lambda im: im[y:y + T, x:x + T].mean()
    rows.append((s[k], i, k, lum(a), lum(b), lum(c), int((s > 12).sum())))
rows.sort(reverse=True)
for r in rows[:N]:
    print("%s  score %.1f tile %s lum %.0f -> %.0f -> %.0f  tiles>12: %d" % (os.path.basename(fs[r[1]]), r[0], r[2], r[3], r[4], r[5], r[6]))
