"""musicfit.py CAP.wav T0 T1 SRC.wav [--win=2] [--full] (notes 18.49) -- per window, fit the source music (best lag near the
running offset, least-squares gain) and print the music level, the rest (capture minus the fitted music),
and the music-to-rest ratio. Stereo: fit per channel. Prints medians at the end.
--full searches the whole source for every window (needed when other sound buries the music). Use
--win=0.5 on a capture with playback gaps: a gap shifts the music inside a longer window."""
import os, sys, wave
import numpy as np
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
from srcmatch import decode

args = [a for a in sys.argv[1:] if not a.startswith("--")]
opt = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--") and "=" in a)
cap_path, t0, t1, src = args[0], float(args[1]), float(args[2]), args[3]
win = float(opt.get("win", 2.0))
w = wave.open(cap_path)
r = w.getframerate()
cap = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, w.getnchannels()).astype(np.float64) / 32768
x, sr = decode(src)
x = x.astype(np.float64)
if sr != r:
    pos = np.arange(0, (len(x) - 1) * r / sr) * sr / r
    x = np.stack([np.interp(pos, np.arange(len(x)), x[:, c]) for c in range(x.shape[1])], 1)
if x.max() > 2:
    x /= 32768
xm = x.mean(1)
n = int(win * r)
db = lambda e: 10 * np.log10(e + 1e-20)
rows = []
off = None
t = t0
while t + win <= t1:
    a = int(t * r)
    seg = cap[a:a + n]
    sm = seg.mean(1)
    # find the lag: full search the first time, then +-50 ms around the running offset
    if off is None or '--full' in sys.argv:
        L = 1 << int(np.ceil(np.log2(len(xm) + n)))
        c = np.fft.irfft(np.fft.rfft(xm, L) * np.conj(np.fft.rfft(sm, L)), L)[:len(xm) - n]
        cand = int(np.argmax(c))
    else:
        lo = max(0, a - off - int(0.05 * r)); hi = min(len(xm) - n, a - off + int(0.05 * r))
        if hi <= lo:
            break
        c = np.array([np.dot(xm[k:k + n], sm) for k in range(lo, hi, 4)])
        k0 = lo + 4 * int(np.argmax(c))
        c2 = np.array([np.dot(xm[k:k + n], sm) for k in range(max(0, k0 - 4), min(len(xm) - n, k0 + 5))])
        cand = max(0, k0 - 4) + int(np.argmax(c2))
    ref = x[cand:cand + n]
    mus, rest = 0.0, 0.0
    for ch in range(2):
        rc = ref[:, ch % ref.shape[1]]
        g = np.dot(seg[:, ch], rc) / max(np.dot(rc, rc), 1e-12)
        m = g * rc
        mus += np.mean(m * m); rest += np.mean((seg[:, ch] - m) ** 2)
    corr = np.dot(sm, ref.mean(1)) / (np.linalg.norm(sm) * np.linalg.norm(ref.mean(1)) + 1e-12)
    if corr > 0.2:
        off = a - cand
    rows.append((t, cand / r, corr, db(mus / 2), db(rest / 2), db(np.mean(seg ** 2))))
    print("%6.1f s  src %7.3f  corr %.2f  music %6.1f dB  rest %6.1f dB  total %6.1f dB  music-rest %+5.1f"
          % (t, cand / r, corr, db(mus / 2), db(rest / 2), db(np.mean(seg ** 2)), db(mus / 2) - db(rest / 2)))
    t += win
good = [rw for rw in rows if rw[2] > 0.1]
if good:
    print("median over %d windows: music %.1f dB, rest %.1f dB, total %.1f dB, music-rest %+.1f dB" % (
        len(good), np.median([g[3] for g in good]), np.median([g[4] for g in good]),
        np.median([g[5] for g in good]), np.median([g[3] - g[4] for g in good])))
