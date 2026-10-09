"""interp_blendscan.py DIR X0 Y0 X1 Y1 -- per interpolated sub-frame, the crop's distance from the blend of the two game
frames around it (the last sub-frames); a torn or misplaced model stands out from in-between motion."""
import glob, re, sys, numpy as np
from PIL import Image
d = sys.argv[1]; x0, y0, x1, y1 = map(int, sys.argv[2:6])
fs = sorted(glob.glob(d + "/hostsub*_*of*.bmp"))
by = {}
for f in fs:
    m = re.search(r"hostsub(\d+)_(\d)of(\d)", f)
    by.setdefault(int(m.group(1)), {})[int(m.group(2))] = f
ld = lambda f: np.asarray(Image.open(f).convert("RGB"))[y0:y1, x0:x1].astype(np.float32)
sw = sorted(by)
worst = []
for a, b in zip(sw, sw[1:]):
    if b != a + 1: continue
    k = max(by[b]); A = ld(by[a][max(by[a])]); B = ld(by[b][k])
    row = []
    for s in range(1, k):
        t = s / k
        X = ld(by[b][s])
        e = np.abs(X - ((1 - t) * A + t * B)).mean()
        e0 = min(np.abs(X - A).mean(), np.abs(X - B).mean())
        row.append(e)
    worst.append((max(row), b))
    print("swap %d: %s  (endpoints apart %.2f)" % (b, " ".join("%.2f" % e for e in row), np.abs(A - B).mean()))
print("worst:", sorted(worst)[-5:])
