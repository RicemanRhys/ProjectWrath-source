"""flat106.py DIR... -- step 106: d3d_host.c roll_detect offline (KS 1,2; SAT: tiles of mean 8-245 only), on rolling-capture pictures (480x270): per picture i and
K in 1,2,4,8: 8x8 tiles flat in i (grey std < 2) and textured (> 5) in i-K and i+K, of one colour (within 8 of their
median); TILES (env, default 12) or more flag it. Prints flagged pictures and a count per folder."""
import sys, os, glob
import numpy as np
from PIL import Image
from scipy import ndimage
CUT = float(os.environ.get('CUT', 12))
KS = [int(v) for v in os.environ.get('KS', '1,2').split(',')]
SAT = os.environ.get('SAT', '1') == '1'
TILES = int(os.environ.get("TILES", 12))
for d in sys.argv[1:]:
    fs = sorted(glob.glob(os.path.join(d, "*.bmp")))
    st = []
    for f in fs:
        g = np.asarray(Image.open(f).convert("RGB"), dtype=np.float64).sum(axis=2)
        h, w = g.shape
        t = g[:h // 8 * 8, :w // 8 * 8].reshape(h // 8, 8, w // 8, 8)
        m = t.mean(axis=(1, 3)); v = (t ** 2).mean(axis=(1, 3)) - m ** 2
        st.append((m / 3, np.sqrt(np.maximum(v, 0)) / 3))
    flagged = []
    for i in range(len(st)):
        kk = int(os.path.basename(fs[i]).split('-of-')[1].split('.')[0])
        for K in KS:
            if i - K < 0 or i + K >= len(st) or (K > 1 and K > kk):
                continue
            if np.abs(st[i - K][0] - st[i + K][0]).mean() > CUT:
                continue
            (ma, sa), (mc, sc), (mb, sb) = st[i - K], st[i], st[i + K]
            sel = (sc < 2) & (sa > 5) & (sb > 5)
            if SAT:
                sel &= (mc >= 8) & (mc <= 245)
            if sel.sum() < TILES:
                continue
            med = np.median(mc[sel])
            good = sel & (np.abs(mc - med) <= 8)
            lab, n = ndimage.label(good)
            if n == 0:
                continue
            sz = ndimage.sum(good, lab, range(1, n + 1))
            good = lab == (int(np.argmax(sz)) + 1)
            ys, xs = np.nonzero(good)
            if good.sum() >= TILES and xs.max() > xs.min() and ys.max() > ys.min():
                flagged.append(i)
                print("%s K%d tiles %d grey %.0f box x %d-%d y %d-%d" % (os.path.basename(fs[i]), K, good.sum(), med, xs.min() * 8, xs.max() * 8 + 7, ys.min() * 8, ys.max() * 8 + 7))
                break
    print("%s: %d of %d flagged" % (d, len(flagged), len(st)))
