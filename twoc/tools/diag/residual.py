"""residual.py CAP.wav T0 T1 MASK.wav TARGET.wav: per window, remove the best
least-squares fit of MASK (at its best offset), then match the residual with
TARGET. Prints both correlations and the residual's level relative to the window."""
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from srcmatch import decode  # noqa: E402

cap_path, t0, t1, mask_p, targ_p = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), sys.argv[4], sys.argv[5]
win = 1.0
w = wave.open(cap_path)
r = w.getframerate()
cap = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, w.getnchannels()).astype(np.float64).mean(1) / 32768


def prep(p):
    x, sr = decode(p)
    m = x.mean(1).astype(np.float64)
    pos = np.arange(0, (len(m) - 1) * r / sr) * sr / r
    return np.interp(pos, np.arange(len(m)), m)


def best(ref, seg):
    L = len(ref) + len(seg)
    nf = 1 << (L - 1).bit_length()
    c = np.fft.irfft(np.fft.rfft(ref, nf) * np.conj(np.fft.rfft(seg, nf)), nf)[:len(ref) - len(seg) + 1]
    cs = np.concatenate([[0], np.cumsum(ref ** 2)])
    e = np.sqrt(np.maximum(cs[len(seg):len(seg) + len(c)] - cs[:len(c)], 0)) * np.sqrt((seg ** 2).sum()) + 1e-12
    sc = np.where(e > 0.01 * e.max(), np.abs(c) / e, 0)
    j = int(np.argmax(sc))
    return j, sc[j]


mask, targ = prep(mask_p), prep(targ_p)
a = t0
while a < t1:
    seg = cap[int(a * r):int((a + win) * r)]
    j, cm = best(mask, seg)
    m = mask[j:j + len(seg)]
    g = (seg @ m) / (m @ m + 1e-12)
    res = seg - g * m
    k, ct = best(targ, res)
    print("%6.1f s  mask corr %.3f  residual %5.1f dB  target at %8.3f s corr %.3f"
          % (a, cm, 10 * np.log10((res ** 2).sum() / (seg ** 2).sum() + 1e-12), k / r, ct))
    a += win
