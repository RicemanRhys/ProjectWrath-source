"""det2.py DIR [N] -- step 106: short-lived regions at several spans. For span k in KS and frame i:
per 16x16 tile s = min(|i-(i-k)|, |i-(i+k)|) - |(i-k)-(i+k)| (mean over the tile, grey); a region there for up to
~k frames and gone either side scores high, steady motion scores <= 0. Reports the best tile per (frame, k), the
count of tiles over 8, and lighter/darker."""
import sys, os, glob
import numpy as np
from PIL import Image
fs = sorted(glob.glob(os.path.join(sys.argv[1], "*.bmp")) + glob.glob(os.path.join(sys.argv[1], "*.png")))[int(os.environ.get("FROM", 0)):]
N = int(sys.argv[2]) if len(sys.argv) > 2 else 20
KS = [int(v) for v in os.environ.get("KS", "1,2,4,5,8").split(",")]
T = 16
g = np.stack([np.asarray(Image.open(f).convert("RGB"), dtype=np.float32).mean(axis=2) for f in fs])
n, h, w = g.shape
g = g[:, :h // T * T, :w // T * T]
def tiles(d): return d.reshape(h // T, T, w // T, T).mean(axis=(1, 3))
rows = []
for k in KS:
    for i in range(k, n - k):
        a, b, c = g[i - k], g[i], g[i + k]
        s = np.minimum(tiles(abs(a - b)), tiles(abs(b - c))) - tiles(abs(a - c))
        y, x = np.unravel_index(np.argmax(s), s.shape)
        sl = (slice(y * T, y * T + T), slice(x * T, x * T + T))
        sign = b[sl].mean() - 0.5 * (a[sl].mean() + c[sl].mean())
        rows.append((s[y, x], i, k, x * T, y * T, sign, int((s > 8).sum())))
rows.sort(reverse=True)
seen = set()
for r in rows:
    if len(seen) >= N: break
    key = (r[1] // 4, r[2])
    if key in seen: continue
    seen.add(key)
    print("%s k%d score %5.1f tile x%3d y%3d %s %+5.1f tiles>8: %d" % (os.path.basename(fs[r[1]])[:24], r[2], r[0], r[3], r[4], "LIGHT" if r[5] > 0 else "dark ", r[5], r[6]))
