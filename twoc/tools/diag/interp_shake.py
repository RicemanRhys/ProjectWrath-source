"""interp_shake.py DIR [FIRST LAST] -- step 67 (notes 18.70): per 32x32 tile, over sub-frame dumps: path (sum of |sub k - sub k-1| over a swap's
sub-frames, from the swap before's last) against direct (|last - last before|). Shaking: path >> direct.
Prints the tiles with the most excess path (path - direct, summed), and the frame's total."""
import glob, os, re, sys
import numpy as np
from PIL import Image
d = sys.argv[1]
fs = sorted(glob.glob(os.path.join(d, "hostsub*_*of4.bmp")))
sw = sorted(set(int(re.search(r"hostsub(\d+)_", f).group(1)) for f in fs))
if len(sys.argv) > 3: sw = [s for s in sw if int(sys.argv[2]) <= s <= int(sys.argv[3])]
L = lambda s, k: np.asarray(Image.open(os.path.join(d, "hostsub%05d_%dof4.bmp" % (s, k))).convert("L"), np.float32)
T = 32; acc = None; tot = []
for s in sw:
    if s - 1 not in sw: continue
    ims = [L(s - 1, 4)] + [L(s, k) for k in (1, 2, 3, 4)]
    H, W = ims[0].shape; h, w = H // T, W // T
    tile = lambda a: np.abs(a)[:h * T, :w * T].reshape(h, T, w, T).mean((1, 3))
    path = sum(tile(ims[k] - ims[k - 1]) for k in range(1, 5)); direct = tile(ims[4] - ims[0])
    ex = path - direct
    acc = ex if acc is None else acc + ex
    tot.append((s, ex.sum()))
print("swaps", len(tot), "total excess %.0f" % sum(t for _, t in tot))
idx = np.dstack(np.unravel_index(np.argsort(-acc.ravel())[:8], acc.shape))[0]
for y, x in idx: print("  tile x %d-%d y %d-%d excess %.0f" % (x * T, x * T + T, y * T, y * T + T, acc[y, x]))
print("  worst swaps:", sorted(tot, key=lambda t: -t[1])[:6])
