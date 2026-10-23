"""decal4.py DIR -- step 101: Gold Rush's brick decal, per swap against its own t = 1 frame (4of4: the game's frame,
where the decal shows). Each sub-frame is aligned to the t = 1 frame (phase correlation over the wall band, 500-1500 x
0-420 at 1920x1080) and the largest mean |difference| over a 140x150 box (the decal's size) found: a decal there in
one and not the other gives ~10 (step 100: the decal ~10.6 darker than its wall); the camera's own motion between
them leaves ~1-2. Over 5 is a sub-frame that lost the decal. Prints each swap and the counts."""
import glob, re, sys
import numpy as np
from PIL import Image

d = sys.argv[1]
BX0, BY0, BX1, BY1 = 500, 0, 1500, 420
W, H = 140, 150


def load(f):
    return np.asarray(Image.open(f).convert("L"), np.float32)[BY0:BY1, BX0:BX1]


def shift(a, b):
    fa, fb = np.fft.fft2(a - a.mean()), np.fft.fft2(b - b.mean())
    r = fa * np.conj(fb)
    c = np.fft.ifft2(r / (np.abs(r) + 1e-9)).real
    y, x = np.unravel_index(np.argmax(c), c.shape)
    if y > a.shape[0] // 2:
        y -= a.shape[0]
    if x > a.shape[1] // 2:
        x -= a.shape[1]
    return int(y), int(x)


def boxmax(diff):
    ii = np.pad(diff, ((1, 0), (1, 0))).cumsum(0).cumsum(1)
    s = ii[H:, W:] - ii[:-H, W:] - ii[H:, :-W] + ii[:-H, :-W]
    y, x = np.unravel_index(np.argmax(s), s.shape)
    return float(s[y, x] / (W * H)), int(x) + BX0, int(y) + BY0


swaps = {}
for f in glob.glob(d + "/hostsub*.bmp"):
    m = re.search(r"hostsub(\d+)_(\d)of(\d)", f)
    swaps.setdefault(int(m.group(1)), {})[int(m.group(2))] = f
lost_subs = lost_swaps = n = 0
worst = 0.0
for s in sorted(swaps):
    fs = swaps[s]
    k = max(fs)
    if len(fs) < 2:
        continue
    ref = load(fs[k])
    out, bad = [], 0
    for i in sorted(fs):
        if i == k:
            continue
        a = load(fs[i])
        dy, dx = shift(a, ref)
        a2 = np.roll(np.roll(a, -dy, 0), -dx, 1)        # a's content moved onto ref's
        m = 40
        diff = np.abs(a2 - ref)[m:-m, m:-m]
        v, bx, by = boxmax(diff)
        worst = max(worst, v)
        out.append("%.1f" % v)
        if v > 5:
            bad += 1
    n += 1
    lost_subs += bad
    lost_swaps += bad > 0
    print("swap %d  sub-frames' largest box difference %s%s" % (s, " ".join(out), "  LOST" if bad else ""))
print("swaps %d, with the decal lost in a sub-frame %d; sub-frames lost %d (largest box difference %.1f)" %
      (n, lost_swaps, lost_subs, worst))
