"""lrbal.py CAP.wav T0 T1 [SRC.wav] [--win=0.1] [--series=N] [--csv=OUT] (notes 18.59)

Left-right placement in a stereo capture or RECOMP_APU_OUT_WAV dump. Per window with sound: the balance
(L against R, dB, + = left) and the side ratio (energy of L-R against L+R, dB; -inf when L == R). With SRC
(the music, e.g. ArcticAL.wav) the music is fitted per channel first (musicfit.py's lag search, at a 2 s
window) and removed, so what is measured is the rest: effects and ambience. A ratio, so it survives the
playback chain's compressor (notes 18.49). --csv writes t, music source time, balance, side, level per window
(the music time lines up two captures of the same demo)."""
import sys, wave
import numpy as np

sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")

args = [a for a in sys.argv[1:] if not a.startswith("--")]
opt = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--") and "=" in a)
w = wave.open(args[0])
r, nch = w.getframerate(), w.getnchannels()
cap = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, nch).astype(np.float64) / 32768
t0, t1 = float(args[1]), float(args[2])
cap = cap[int(t0 * r):int(t1 * r), :2]
win = int(float(opt.get("win", "0.1")) * r)
db = lambda e: 10 * np.log10(np.maximum(e, 1e-20))

srct = np.full(len(cap), np.nan)
if len(args) > 3:
    from srcmatch import decode
    x, sr = decode(args[3])
    x = x.astype(np.float64)
    if sr != r:
        pos = np.arange(0, (len(x) - 1) * r / sr) * sr / r
        x = np.stack([np.interp(pos, np.arange(len(x)), x[:, c]) for c in range(x.shape[1])], 1)
    if x.max() > 2:
        x /= 32768
    xm = x.mean(1)
    n = 2 * r
    off = None
    for a in range(0, len(cap) - n + 1, n):
        seg = cap[a:a + n]
        sm = seg.mean(1)
        if off is None:
            L = 1 << int(np.ceil(np.log2(len(xm) + n)))
            c = np.fft.irfft(np.fft.rfft(xm, L) * np.conj(np.fft.rfft(sm, L)), L)[:len(xm) - n]
            cand = int(np.argmax(c))
        else:
            lo = max(0, a - off - int(0.05 * r)); hi = min(len(xm) - n, a - off + int(0.05 * r))
            if hi <= lo:
                break
            c = np.array([np.dot(xm[k:k + n], sm) for k in range(lo, hi, 2)])
            cand = lo + 2 * int(np.argmax(c))
        ref = x[cand:cand + n]
        corr = np.dot(sm, ref.mean(1)) / (np.linalg.norm(sm) * np.linalg.norm(ref.mean(1)) + 1e-12)
        if corr > 0.2:
            off = a - cand
        for ch in range(2):
            rc = ref[:, ch % ref.shape[1]]
            g = np.dot(seg[:, ch], rc) / max(np.dot(rc, rc), 1e-12)
            cap[a:a + n, ch] -= g * rc
        srct[a:a + n] = (cand + np.arange(n)) / r
    cap = cap[:(len(cap) // n) * n]

nw = len(cap) // win
seg = cap[:nw * win].reshape(nw, win, 2)
eL, eR = (seg[:, :, 0] ** 2).mean(1), (seg[:, :, 1] ** 2).mean(1)
eS = ((seg[:, :, 0] - seg[:, :, 1]) ** 2).mean(1)
eM = ((seg[:, :, 0] + seg[:, :, 1]) ** 2).mean(1)
lvl = db((eL + eR) / 2)
live = lvl > -60
bal = db(eL) - db(eR)
side = db(eS) - db(eM)
print("%s %.1f-%.1f s, %d windows of %.2f s, %d with sound%s" % (args[0], t0, t1, nw, win / r, live.sum(),
      ", music removed" if len(args) > 3 else ""))
if live.sum():
    print("  level  L %.1f dB  R %.1f dB" % (db(eL[live].mean()), db(eR[live].mean())))
    print("  balance (dB, + = left): p5 %+6.1f  p25 %+6.1f  median %+6.1f  p75 %+6.1f  p95 %+6.1f   |bal|>3 dB: %d%%" %
          (tuple(np.percentile(bal[live], [5, 25, 50, 75, 95])) + (round(100 * (np.abs(bal[live]) > 3).mean()),)))
    print("  side/mid (dB):          p5 %6.1f  p25 %6.1f  median %6.1f  p75 %6.1f  p95 %6.1f   overall %.1f" %
          (tuple(np.percentile(side[live], [5, 25, 50, 75, 95])) + (db(eS[live].sum()) - db(eM[live].sum()),)))
ns = int(opt.get("series", "0"))
if ns:
    idx = np.flatnonzero(live)
    for i in idx[::max(1, len(idx) // ns)][:ns]:
        print("   %7.2f s  level %6.1f  bal %+6.1f  side %6.1f" % (t0 + i * win / r, lvl[i], bal[i], side[i]))
if "csv" in opt:
    with open(opt["csv"], "w") as f:
        f.write("t,src,bal,side,level\n")
        for i in range(nw):
            f.write("%.3f,%.3f,%.2f,%.2f,%.2f\n" % (t0 + i * win / r, srct[i * win + win // 2] if i * win < len(srct) else np.nan,
                                                    bal[i], side[i], lvl[i]))
