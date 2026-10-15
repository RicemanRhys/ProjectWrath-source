"""subev.py DIR PREFIX -- step 77: how evenly the picture moves from one present to the next. Reads the sub-frame
dumps PREFIXsubSSSSS_JofK.bmp (RECOMP_D3D_BRIDGE_SUBDUMP) in present order and measures the shift of the picture
between consecutive presents by phase correlation (the middle half of the frame, grey). With the camera following
Crash at a constant speed every present should move the picture by the same amount when the presents are evenly
spaced in time; prints each shift, its game frame and sub-frame, and the spread."""
import glob, os, re, sys
import numpy as np
from PIL import Image

d, pfx = sys.argv[1], sys.argv[2]
files = []
for f in glob.glob(os.path.join(d, pfx + "sub*_*of*.bmp")):
    m = re.search(r"sub(\d+)_(\d+)of(\d+)\.bmp$", f)
    if m:
        files.append((int(m.group(1)), int(m.group(2)), int(m.group(3)), f))
files.sort()


def grey(f):
    a = np.asarray(Image.open(f).convert("L"), dtype=np.float64)
    h, w = a.shape
    a = a[: h // 4, w // 8: 7 * w // 8] if os.environ.get("SUBEV_TOP") else a[h // 4: 3 * h // 4, w // 4: 3 * w // 4]
    a = a - a.mean()
    wy = np.hanning(a.shape[0])[:, None]
    wx = np.hanning(a.shape[1])[None, :]
    return a * wy * wx


def shift(a, b):
    """b's offset from a (dy, dx) to a tenth of a pixel: phase correlation, then a parabola on the peak."""
    F = np.fft.fft2(a) * np.conj(np.fft.fft2(b))
    r = np.fft.ifft2(F / (np.abs(F) + 1e-9)).real
    iy, ix = np.unravel_index(np.argmax(r), r.shape)
    def sub(c, i, n, axis):
        l = r[(i - 1) % n, ix] if axis == 0 else r[iy, (i - 1) % n]
        rr = r[(i + 1) % n, ix] if axis == 0 else r[iy, (i + 1) % n]
        den = l - 2 * c + rr
        return i + (0.5 * (l - rr) / den if den else 0.0)
    c = r[iy, ix]
    y = sub(c, iy, r.shape[0], 0)
    x = sub(c, ix, r.shape[1], 1)
    if y > r.shape[0] / 2:
        y -= r.shape[0]
    if x > r.shape[1] / 2:
        x -= r.shape[1]
    return y, x


prev = None
mags = []
for sw, j, k, f in files:
    g = grey(f)
    if prev is not None:
        dy, dx = shift(prev[1], g)
        m = (dy * dy + dx * dx) ** 0.5
        mags.append(m)
        print("swap %5u %u of %u: shift %6.2f px (dy %6.2f dx %6.2f)" % (sw, j, k, m, dy, dx))
    prev = (f, g)
if mags:
    a = np.array(mags)
    print("presents %u, shift mean %.2f px, sd %.2f, min %.2f, max %.2f, sd/mean %.1f%%" %
          (len(a) + 1, a.mean(), a.std(), a.min(), a.max(), 100 * a.std() / a.mean() if a.mean() else 0))
