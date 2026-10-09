"""interp_oddscan.py DIR X0 Y0 X1 Y1 [THRESH] -- per interpolated sub-frame, the pixels in the box that match neither game
frame around it (differ from both last sub-frames by over THRESH levels, default 60, in any channel)."""
import glob, re, sys, numpy as np
from PIL import Image
d = sys.argv[1]; x0, y0, x1, y1 = map(int, sys.argv[2:6]); th = int(sys.argv[6]) if len(sys.argv) > 6 else 60
by = {}
for f in glob.glob(d + "/hostsub*_*of*.bmp"):
    m = re.search(r"hostsub(\d+)_(\d)of(\d)", f)
    by.setdefault(int(m.group(1)), {})[int(m.group(2))] = f
ld = lambda f: np.asarray(Image.open(f).convert("RGB"))[y0:y1, x0:x1].astype(np.int16)
sw = sorted(by); tot = []
for a, b in zip(sw, sw[1:]):
    if b != a + 1: continue
    k = max(by[b]); A = ld(by[a][max(by[a])]); B = ld(by[b][k])
    row = []
    for s in range(1, k):
        X = ld(by[b][s])
        row.append(int(((np.abs(X - A).max(2) > th) & (np.abs(X - B).max(2) > th)).sum()))
    tot.append((max(row), b))
    if "-a" in sys.argv or max(row) > 0: print("swap %d: %s" % (b, row))
print("swaps %d, with odd pixels %d, worst %s" % (len(tot), sum(1 for t in tot if t[0]), sorted(tot)[-4:]))
