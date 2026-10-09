"""interp_t0.py DIR A B -- with RECOMP_D3D_BRIDGE_INTERP_T0: swap s's first sub-frame (its draws under swap s-1's
camera) against swap s-1's last (as recorded): static scenery must match. Prints, per swap, the mean difference
and the share of pixels over 24 levels, against the same for the whole tick (s-1's last vs s's last); writes
DIR/t0diff_s.png (the two pictures and the t=0 difference, amplified)."""
import os, sys
import numpy as np
from PIL import Image

d, a, b = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
load = lambda s, i, k: np.asarray(Image.open(os.path.join(d, "hostsub%05u_%uof%u.bmp" % (s, i, k))).convert("RGB")).astype(np.float32)
for s in range(a + 1, b + 1):
    try:
        prev, t0, last = load(s - 1, 4, 4), load(s, 1, 4), load(s, 4, 4)
    except FileNotFoundError:
        continue
    d0 = np.abs(t0 - prev).max(axis=2)
    d1 = np.abs(last - prev).max(axis=2)
    print("swap %d: t=0 vs the frame before: mean %.2f, pixels over 24: %.2f%%;  whole tick: mean %.2f, over 24: %.2f%%"
          % (s, d0.mean(), 100 * (d0 > 24).mean(), d1.mean(), 100 * (d1 > 24).mean()))
    vis = np.clip(d0 * 4, 0, 255)[..., None].repeat(3, 2)
    Image.fromarray(np.concatenate([prev, t0, vis], axis=1)[::2, ::2].astype(np.uint8)).save(os.path.join(d, "t0diff_%d.png" % s))
