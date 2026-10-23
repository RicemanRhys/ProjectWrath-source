"""stsum.py DIR [MODE] -- step 103: a run's sub-frame dumps (hostsubSSSSS_kof4.bmp).
MODE st (default): each sub-frame against its own swap's t = 1 frame (a still camera: static scenery must match):
  mean |diff|, pixels over 24 levels, and tiles (256 px) shifted by phase correlation (|shift| >= 1 px), their
  largest shift.
MODE t0 (INTERP_T0 runs): sub-frame 1 (t = 0) against the swap before's t = 1 frame, the same numbers; the whole
  tick (this swap's t = 1 against the one before) beside it."""
import glob, os, re, sys
import numpy as np
from PIL import Image

d = sys.argv[1]
mode = sys.argv[2] if len(sys.argv) > 2 else "st"
files = glob.glob(os.path.join(d, "hostsub*_1of4.bmp"))
swaps = sorted(int(re.search(r"hostsub(\d+)_", f).group(1)) for f in files)


def ld(s, k):
    return np.asarray(Image.open(os.path.join(d, "hostsub%05d_%dof4.bmp" % (s, k))).convert("L"), np.float32)


def shift(a, b):
    A = np.fft.fft2(a - a.mean()); B = np.fft.fft2(b - b.mean())
    R = A * np.conj(B); R /= np.abs(R) + 1e-9
    r = np.fft.ifft2(R).real
    y, x = np.unravel_index(np.argmax(r), r.shape)
    if y > a.shape[0] // 2: y -= a.shape[0]
    if x > a.shape[1] // 2: x -= a.shape[1]
    return x, y


def cmp(img, ref):
    T = 256
    H, W = ref.shape
    dd = np.abs(img - ref)
    n = moved = 0; big = 0
    for ty in range(0, H - T + 1, T):
        for tx in range(0, W - T + 1, T):
            a = img[ty:ty + T, tx:tx + T]; b = ref[ty:ty + T, tx:tx + T]
            if b.std() < 4:
                continue
            n += 1
            if np.abs(a - b).mean() < 0.3:
                continue
            x, y = shift(a, b)
            if abs(x) + abs(y) >= 1 and abs(x) <= 40 and abs(y) <= 40:
                moved += 1; big = max(big, max(abs(x), abs(y)))
    return dd.mean(), 100.0 * (dd > 24).mean(), moved, n, big


for s in swaps:
    try:
        if mode == "st":
            ref = ld(s, 4)
            for k in (1, 2, 3):
                m, o, mv, n, big = cmp(ld(s, k), ref)
                print("swap %d sub %d vs t=1: mean %.2f, over 24 %.2f%%, tiles shifted %d of %d (largest %d px)" %
                      (s, k, m, o, mv, n, big))
        else:
            if s - 1 not in swaps:
                continue
            ref = ld(s - 1, 4)
            m, o, mv, n, big = cmp(ld(s, 1), ref)
            m1, o1, _, _, _ = cmp(ld(s, 4), ref) if False else (np.abs(ld(s, 4) - ref).mean(), 100.0 * (np.abs(ld(s, 4) - ref) > 24).mean(), 0, 0, 0)
            print("swap %d t=0 vs the swap before: mean %.2f, over 24 %.2f%%, tiles shifted %d of %d (largest %d px);"
                  "  whole tick: mean %.2f, over 24 %.2f%%" % (s, m, o, mv, n, big, m1, o1))
    except FileNotFoundError:
        pass
