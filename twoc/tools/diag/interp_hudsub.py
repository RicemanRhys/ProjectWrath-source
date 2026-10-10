"""interp_hudsub.py DIR X0 Y0 X1 Y1 [OUT.png] -- step 65: for each dumped swap, how far each sub-frame's HUD text (bright,
saturated pixels of the swap's last sub-frame in the box) is from the last sub-frame: drawn as recorded, 0. Also a
strip of the box per sub-frame (OUT)."""
import glob, os, re, sys
import numpy as np
from PIL import Image
d, x0, y0, x1, y1 = sys.argv[1], *map(int, sys.argv[2:6])
fs = sorted(glob.glob(os.path.join(d, 'hostsub*_*of*.bmp')))
sw = {}
for f in fs:
    m = re.search(r'hostsub(\d+)_(\d)of(\d)', f)
    sw.setdefault(int(m.group(1)), {})[int(m.group(2))] = f
rows = []
for s in sorted(sw):
    k = max(sw[s])
    last = np.asarray(Image.open(sw[s][k]).convert('RGB'), np.int32)
    H, W = last.shape[:2]
    sx, sy = W / 640.0, H / 480.0
    bx = (int(x0 * sx), int(y0 * sy), int(x1 * sx), int(y1 * sy))
    L = last[bx[1]:bx[3], bx[0]:bx[2]]
    mx, mn = L.max(2), L.min(2)
    mask = (mx > 170) & (mx - mn > 90)
    out = []
    crops = []
    for j in sorted(sw[s]):
        a = np.asarray(Image.open(sw[s][j]).convert('RGB'), np.int32)[bx[1]:bx[3], bx[0]:bx[2]]
        crops.append(a)
        out.append(np.abs(a - L).sum(2)[mask].mean() if mask.sum() else 0.0)
    print('swap %d  text px %5d  sub-frame distance from the last: %s' % (s, mask.sum(), ' '.join('%6.1f' % v for v in out)))
    rows.append(np.concatenate(crops, 1))
if len(sys.argv) > 6 and rows:
    Image.fromarray(np.concatenate(rows[:12], 0).astype(np.uint8)).save(sys.argv[6])
