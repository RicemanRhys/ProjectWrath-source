"""fxgain.py BINS XEMU.wav SRC.wav X_T0 X_T1 [--win=2] (notes 18.49; use --win=0.5) -- the gain the real GP gives the 3D effects bins
(6/7) relative to the speaker bins (0/1), from ratios that a compressor in the playback chain leaves alone.

xemu capture, per window: music (least-squares fit of SRC at its best lag) vs the rest.
Bin dump, per window at the same music position: music (fit of SRC in b0+b1), ambience (b0+b1 minus it),
effects E = energy of b6+b7. Solve G^2 = (M * 10^(-X/10) - A) / E per window; print the median."""
import sys, wave
import numpy as np
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
from srcmatch import decode

args = [a for a in sys.argv[1:] if not a.startswith("--")]
opt = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--") and "=" in a)
bins_p, cap_p, src_p, t0, t1 = args[0], args[1], args[2], float(args[3]), float(args[4])
win = float(opt.get("win", 2.0))
R = 48000
B = np.fromfile(bins_p, dtype=np.float32).reshape(-1, 8).astype(np.float64)
w = wave.open(cap_p); r = w.getframerate()
cap = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, w.getnchannels()).astype(np.float64) / 32768
if r != R:
    pos = np.arange(0, (len(cap) - 1) * R / r) * r / R
    cap = np.stack([np.interp(pos, np.arange(len(cap)), cap[:, c]) for c in range(2)], 1)
x, sr = decode(src_p)
x = x.astype(np.float64)
if sr != R:
    pos = np.arange(0, (len(x) - 1) * R / sr) * sr / R
    x = np.stack([np.interp(pos, np.arange(len(x)), x[:, c]) for c in range(x.shape[1])], 1)
xm = x.mean(1)
n = int(win * R)
L = 1 << int(np.ceil(np.log2(len(xm) + n)))
FX = np.fft.rfft(xm, L)


def lag(seg):
    c = np.fft.irfft(FX * np.conj(np.fft.rfft(seg.mean(1), L)), L)[:len(xm) - n]
    k = int(np.argmax(c))
    ref = xm[k:k + n]
    return k, np.dot(seg.mean(1), ref) / (np.linalg.norm(seg.mean(1)) * np.linalg.norm(ref) + 1e-12)


def split(seg, k):
    """music energy, rest energy (both channels averaged)"""
    ref = x[k:k + n]
    M = Rr = 0.0
    for ch in range(2):
        rc = ref[:, ch % ref.shape[1]]
        g = np.dot(seg[:, ch], rc) / max(np.dot(rc, rc), 1e-12)
        M += np.mean((g * rc) ** 2) / 2; Rr += np.mean((seg[:, ch] - g * rc) ** 2) / 2
    return M, Rr


# xemu windows: source position -> music-rest ratio
xw = {}
t = t0
while t + win <= t1:
    a = int(t * R); seg = cap[a:a + n]
    k, c = lag(seg)
    if c > 0.3:
        M, Rr = split(seg, k)
        xw[round(k / R * 2) / 2] = (10 * np.log10(M / Rr), c)
    t += win
# dump: find the same source positions (scan the dump in windows, keep those whose lag matches)
res = []
t = 0.0
while (t + win) * R <= len(B):
    a = int(t * R); seg = B[a:a + n, 0:2]
    if np.mean(seg ** 2) > 1e-7:
        k, c = lag(seg)
        key = round(k / R * 2) / 2
        if c > 0.6 and key in xw:
            M, A = split(seg, k)
            E = np.mean(B[a:a + n, 2:4] ** 2)   # b6+b7 energy, per channel
            X, cx = xw[key]
            need = M * 10 ** (-X / 10) - A
            G2 = need / E if E > 0 else np.nan
            res.append((key, X, 10 * np.log10(M / (A + E)), 10 * np.log10(M / A), G2, cx, c))
    t += win
print("src s | xemu music-rest | stub-like (A+E) | music-ambience only | G (dB) needed on b6/b7")
for key, X, ours, mo, G2, cx, c in sorted(res):
    need = ("%+.1f" % (10 * np.log10(G2))) if G2 > 0 else "<none>"
    print("%5.1f | %+6.1f dB | %+6.1f dB | %+6.1f dB | %s" % (key, X, ours, mo, need))
g = [10 * np.log10(r_[4]) for r_ in res if r_[4] > 0]
print("windows %d, with a solution %d; median G %+.1f dB (p25 %+.1f, p75 %+.1f)" % (
    len(res), len(g), np.median(g), np.percentile(g, 25), np.percentile(g, 75)))
