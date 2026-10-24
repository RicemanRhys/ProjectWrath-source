"""audcmp.py A.wav B.wav [--win 0.5] [--from S] [--to S] [--quiet -50] -- step 108: how alike two recordings of
the same scenario are. B is aligned to A as a whole (cross-correlation of the two 100 Hz envelopes, then the
samples), then each WIN-second window of A with sound in it (RMS over QUIET dBFS) is matched in B within
+-0.25 s of the running lag: the best normalised correlation (both channels together), the lag, and whether the
window's samples are IDENTICAL at that lag. Prints the distribution and the worst windows.

Two runs of one build differ where the game's timing moves a sound against the audio frames (the APU's frame
thread runs on the clock, the game on its own): the floor to compare a change against is two runs of the same
exe (A vs A'), not 1.0."""
import sys, wave, argparse
import numpy as np
from scipy.signal import fftconvolve

ap = argparse.ArgumentParser()
ap.add_argument("a"); ap.add_argument("b")
ap.add_argument("--win", type=float, default=0.5)
ap.add_argument("--from", dest="t0", type=float, default=0.0)
ap.add_argument("--to", dest="t1", type=float, default=1e9)
ap.add_argument("--quiet", type=float, default=-50.0)
ap.add_argument("--worst", type=int, default=5)
ap.add_argument("--maxlag", type=float, default=30.0, help="seconds either way for the whole-file alignment")
ap.add_argument("--wide", type=float, default=12.0, help="seconds either way of the running lag a window is looked for")
a = ap.parse_args()


def load(p):
    w = wave.open(p)
    ch, sr, n = w.getnchannels(), w.getframerate(), w.getnframes()
    x = np.frombuffer(w.readframes(n), np.int16).reshape(-1, ch)
    return x, sr


A, sr = load(a.a)
B, srb = load(a.b)
assert sr == srb, (sr, srb)
if A.shape[1] != B.shape[1]:
    sys.exit("channel counts differ: %d vs %d" % (A.shape[1], B.shape[1]))
Af = A.astype(np.float64) / 32768.0
Bf = B.astype(np.float64) / 32768.0
am, bm = Af.mean(axis=1), Bf.mean(axis=1)

# whole-file lag from 10 ms envelopes, then refined on the samples around it
hop = sr // 100
def env(x):
    n = len(x) // hop
    e = np.sqrt((x[:n * hop].reshape(n, hop) ** 2).mean(axis=1))
    return e - e.mean()
ea, eb = env(am), env(bm)
c = fftconvolve(ea, eb[::-1], mode="full")
lags = np.arange(-len(eb) + 1, len(ea))
keep = np.abs(lags) <= a.maxlag * 100
lag0 = int(lags[keep][np.argmax(c[keep])]) * hop      # A[t] ~ B[t - lag0]


def best(i0, n, guess, span):
    """the lag (A[i] ~ B[i - lag]) in guess +- span maximising the correlation of A[i0:i0+n] with B, and that value"""
    lo, hi = guess - span, guess + span
    b0, b1 = i0 - hi, i0 + n - lo
    if b0 < 0 or b1 > len(Bf):
        return None
    seg_a = Af[i0:i0 + n]
    seg_b = Bf[b0:b1]
    num = np.zeros(b1 - b0 - n + 1)
    for ch in range(Af.shape[1]):
        num += fftconvolve(seg_b[:, ch], seg_a[::-1, ch], mode="valid")
    ea2 = (seg_a ** 2).sum()
    cs = np.concatenate([[0.0], np.cumsum((seg_b ** 2).sum(axis=1))])
    eb2 = cs[n:] - cs[:-n]
    r = num / np.sqrt(ea2 * np.maximum(eb2, 1e-30))
    k = int(np.argmax(r))
    lag = hi - k
    return lag, float(r[k])


W = int(a.win * sr)
span = int(0.25 * sr)
wide = int(a.wide * 100)           # envelope frames either way of the running lag
lag = lag0
rows = []
i0 = int(a.t0 * sr)
end = min(len(Af), int(a.t1 * sr))


def candidates(i0, guess):
    """lags worth refining for the window at i0: the running lag, and the 3 best envelope matches within +-wide"""
    out = [guess]
    ka, n = i0 // hop, W // hop
    sa = ea[ka:ka + n]
    g = guess // hop
    lo, hi = max(0, ka - g - wide), min(len(eb), ka - g + wide + n)
    sb = eb[lo:hi]
    if len(sb) > n and sa.std() > 0:
        c = fftconvolve(sb, sa[::-1], mode="valid")             # c[k] ~ A[ka..] vs B[lo+k..]
        for k in np.argsort(c)[::-1][:3]:
            out.append((ka - (lo + int(k))) * hop)
    return out


while i0 + W <= end:
    seg = Af[i0:i0 + W]
    rms = np.sqrt((seg ** 2).mean())
    db = 20 * np.log10(max(rms, 1e-12))
    if db > a.quiet:
        r = None
        for c0 in candidates(i0, lag):
            rr = best(i0, W, c0, span if c0 == lag else hop * 3)
            if rr and (r is None or rr[1] > r[1]):
                r = rr
        if r:
            lag, cor = r
            same = np.array_equal(A[i0:i0 + W], B[i0 - lag:i0 - lag + W])
            nd = int(np.count_nonzero(A[i0:i0 + W] != B[i0 - lag:i0 - lag + W]))
            rows.append((i0 / sr, db, lag, cor, same, nd))
    i0 += W

if not rows:
    sys.exit("no window with sound")
cor = np.array([r[3] for r in rows])
same = sum(1 for r in rows if r[4])
lagv = np.array([r[2] for r in rows])
print("%s vs %s: %d ch, whole-file lag %+d samples (%.3f s); %d windows of %.2f s with sound (over %.0f dBFS)" %
      (a.a.split("\\")[-1], a.b.split("\\")[-1], A.shape[1], lag0, lag0 / sr, len(rows), a.win, a.quiet))
print("  correlation: median %.6f  p10 %.6f  min %.6f;  windows >= 0.999: %d (%.0f%%), >= 0.99: %d (%.0f%%)" %
      (np.median(cor), np.percentile(cor, 10), cor.min(), (cor >= 0.999).sum(), 100.0 * (cor >= 0.999).mean(),
       (cor >= 0.99).sum(), 100.0 * (cor >= 0.99).mean()))
print("  samples identical at the best lag: %d of %d windows; lag drift %+d .. %+d samples" %
      (same, len(rows), lagv.min() - lag0, lagv.max() - lag0))
for t, db, lg, cr, sm, nd in sorted(rows, key=lambda r: r[3])[:a.worst]:
    print("    worst: %7.2f s  %6.1f dBFS  lag %+7d  r %.5f  %s" % (t, db, lg, cr, "identical" if sm else "%d samples differ" % nd))
