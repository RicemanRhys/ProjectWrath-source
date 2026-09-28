"""timeline.py CAP.wav T0 T1 SRC.wav... [--win=S] [--step=S] [--speed=K] (notes 18.45)

For each window of the capture: its level, and the source (and the time in
it) that matches best, with the normalised correlation (mono). A source time
that advances one second per second of capture is continuous playback."""
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from srcmatch import decode  # noqa: E402

args = [a for a in sys.argv[1:] if not a.startswith("--")]
opt = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--") and "=" in a)
cap_path, t0, t1, srcs = args[0], float(args[1]), float(args[2]), args[3:]
win = float(opt.get("win", 0.5))
step = float(opt.get("step", 1.0))
k = float(eval(opt.get("speed", "1.0")))
w = wave.open(cap_path)
r = w.getframerate()
cap = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, w.getnchannels()).astype(np.float64).mean(1) / 32768
refs = []
for p in srcs:
    x, sr = decode(p)
    m = x.mean(1).astype(np.float64)
    pos = np.arange(0, (len(m) - 1) * r / (sr * k)) * sr * k / r
    ref = np.interp(pos, np.arange(len(m)), m)
    L = len(ref) + int(win * r)
    nf = 1 << (L - 1).bit_length()
    refs.append((os.path.basename(p), ref, np.fft.rfft(ref, nf), nf, np.concatenate([[0], np.cumsum(ref ** 2)])))
a = t0
rows = []
while a < t1:
    seg = cap[int(a * r):int((a + win) * r)]
    if len(seg) < int(win * r):
        break
    rms = 20 * np.log10(np.sqrt((seg ** 2).mean()) + 1e-12)
    best = (0, None, 0)
    if np.abs(seg).max() > 1e-4:
        for name, ref, F, nf, cs in refs:
            if len(ref) <= len(seg):
                continue
            c = np.fft.irfft(F * np.conj(np.fft.rfft(seg, nf)), nf)[:len(ref) - len(seg) + 1]
            e = np.sqrt(np.maximum(cs[len(seg):len(seg) + len(c)] - cs[:len(c)], 0)) * np.sqrt((seg ** 2).sum()) + 1e-12
            sc = np.where(e > 0.01 * e.max(), np.abs(c) / e, 0)
            j = int(np.argmax(sc))
            if sc[j] > best[0]:
                best = (sc[j], name, j * k / r)
    rows.append((a, rms, best))
    print("%6.1f s  %6.1f dB  %-12s %8.3f s  corr %.3f" % (a, rms, best[1] or "-", best[2], best[0]))
    a += step
