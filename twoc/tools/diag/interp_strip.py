"""interp_strip.py DIR SWAP [SWAP2 ...] [--out=PNG] -- each swap's sub-frames side by side (rows = swaps), half size,
plus the mean absolute difference between neighbouring sub-frames (how far each step moves the picture)."""
import glob, os, sys
import numpy as np
from PIL import Image

d = sys.argv[1]
swaps = [int(a) for a in sys.argv[2:] if not a.startswith("--")]
out = next((a[6:] for a in sys.argv if a.startswith("--out=")), os.path.join(d, "strip_%d.png" % swaps[0]))
rows = []
prev_last = None
for s in swaps:
    fs = sorted(glob.glob(os.path.join(d, "hostsub%05u_*of*.bmp" % s)))
    ims = [np.asarray(Image.open(f).convert("RGB")).astype(np.float32) for f in fs]
    steps = []
    for i, im in enumerate(ims):
        ref = ims[i - 1] if i else prev_last
        if ref is not None and ref.shape == im.shape:
            steps.append(np.abs(im - ref).mean())
    prev_last = ims[-1] if ims else prev_last
    print("swap %d: %d sub-frames; mean abs step from the one before: %s" % (s, len(ims), " ".join("%.2f" % x for x in steps)))
    rows.append(np.concatenate([im[::2, ::2] for im in ims], axis=1))
w = max(r.shape[1] for r in rows)
rows = [np.pad(r, ((0, 0), (0, w - r.shape[1]), (0, 0))) for r in rows]
Image.fromarray(np.concatenate(rows, axis=0).astype(np.uint8)).save(out)
print(out)
