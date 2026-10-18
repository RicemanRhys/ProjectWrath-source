"""flkan.py TAG X0 Y0 X1 Y1 -- step 92: a flk.sh run's sub-frames in order: a strip of the crop (runs/TAG/strip.png) and,
per consecutive pair, how much the picture moved (phase correlation, px) and the mean absolute change after undoing
that move, overall and in the crop; with the AO-alone debug view that change is the occlusion flickering."""
import os, re, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
tag = sys.argv[1]
x0, y0, x1, y1 = map(int, sys.argv[2:6])
d = os.path.join(HERE, 'runs', tag)
fs = sorted(f for f in os.listdir(d) if re.match(r'hostsub\d+_\dof\d\.bmp$', f))
ims = [np.asarray(Image.open(os.path.join(d, f)).convert('L'), dtype=np.float32) for f in fs]


def shift(a, b):
    fa, fb = np.fft.fft2(a), np.fft.fft2(b)
    r = fa * np.conj(fb)
    r /= np.abs(r) + 1e-9
    c = np.abs(np.fft.ifft2(r))
    dy, dx = np.unravel_index(np.argmax(c), c.shape)
    if dy > a.shape[0] // 2: dy -= a.shape[0]
    if dx > a.shape[1] // 2: dx -= a.shape[1]
    return int(dy), int(dx)


for i in range(1, len(ims)):
    a, b = ims[i - 1], ims[i]
    dy, dx = shift(b[y0:y1, x0:x1], a[y0:y1, x0:x1])
    bb = np.roll(b, (-dy, -dx), axis=(0, 1))
    diff = np.abs(bb - a)[y0 + 8:y1 - 8, x0 + 8:x1 - 8]
    print('%s -> %s: moved (%d, %d) px, mean change %.2f, pixels changed > 16: %.2f%%' % (
        fs[i - 1][7:-4], fs[i][7:-4], dx, dy, diff.mean(), 100.0 * (diff > 16).mean()))
k = 0.5
w, h = int((x1 - x0) * k), int((y1 - y0) * k)
strip = Image.new('L', (w * min(len(ims), 6), h * ((len(ims) + 5) // 6)))
for i, f in enumerate(fs):
    im = Image.open(os.path.join(d, f)).convert('L').crop((x0, y0, x1, y1)).resize((w, h))
    strip.paste(im, ((i % 6) * w, (i // 6) * h))
strip.save(os.path.join(d, 'strip.png'))
