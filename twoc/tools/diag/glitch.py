"""glitch.py CAP.wav [T0 T1] [--list]: audible faults in a loopcap.exe capture.

Per second (or for [T0, T1] s), for a 16-bit stereo capture:
  - clip: samples at full scale (|x| >= 32700), and the runs of them;
  - drop: dropouts, 2 ms or more under -70 dBFS inside sound (the 20 ms
    either side louder than -40 dBFS): an output underrun or a gap in a
    stream;
  - click: steps between neighbouring samples over 8x the local mean step
    (the 5 ms around it) and over -30 dBFS: a discontinuity the source
    doesn't have (a resampler restarting a voice mid-sample, a buffer seam).
--list prints each drop and click with its time.
"""
import sys
import wave

import numpy as np

args = [a for a in sys.argv[1:] if not a.startswith("--")]
w = wave.open(args[0])
sr, ch = w.getframerate(), w.getnchannels()
x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, ch).astype(np.float64)
m = x.mean(axis=1) if ch > 1 else x[:, 0]
t0 = float(args[1]) if len(args) > 2 else 0.0
t1 = float(args[2]) if len(args) > 2 else len(m) / sr

a = np.abs(x).max(axis=1)
clip = a >= 32700
# dropouts
env = np.sqrt(np.convolve(m * m, np.ones(int(sr * 0.002)) / int(sr * 0.002), mode="same"))
quiet = env < 32768 * 10 ** (-70 / 20)
drops = []
i = 0
n = len(m)
while i < n:
    if quiet[i]:
        j = i
        while j < n and quiet[j]:
            j += 1
        if j - i >= int(sr * 0.002):
            pre = m[max(0, i - int(sr * 0.02)):i]
            post = m[j:j + int(sr * 0.02)]
            loud = 32768 * 10 ** (-40 / 20)
            if len(pre) and len(post) and np.sqrt((pre ** 2).mean()) > loud and np.sqrt((post ** 2).mean()) > loud:
                drops.append((i / sr, (j - i) / sr * 1000))
        i = j
    else:
        i += 1
# clicks
d = np.abs(np.diff(m))
k = int(sr * 0.005)
loc = np.convolve(d, np.ones(k) / k, mode="same")
cand = np.nonzero((d > 8 * loc + 1) & (d > 32768 * 10 ** (-30 / 20)))[0]
clicks = []
last = -1
for c in cand:
    if c - last > sr * 0.002:
        clicks.append(c / sr)
    last = c

for s in range(int(t0), int(np.ceil(t1))):
    lo, hi = s * sr, (s + 1) * sr
    c = clip[lo:hi]
    runs = int(np.count_nonzero(c[1:] & ~c[:-1])) + int(c[:1].sum()) if len(c) else 0
    nd = sum(1 for tt, _ in drops if s <= tt < s + 1)
    nc = sum(1 for tt in clicks if s <= tt < s + 1)
    rms = 20 * np.log10(np.sqrt((m[lo:hi] ** 2).mean()) / 32768 + 1e-9) if hi <= len(m) else -120
    print("%4d s: rms %6.1f dB  clip %5d samples in %3d runs  drop %2d  click %2d" % (s, rms, int(c.sum()), runs, nd, nc))
print("total [%g, %g] s: clipped samples %d, dropouts %d, clicks %d" % (
    t0, t1, int(clip[int(t0 * sr):int(t1 * sr)].sum()), sum(1 for tt, _ in drops if t0 <= tt < t1),
    sum(1 for tt in clicks if t0 <= tt < t1)))
if "--list" in sys.argv:
    print("drops:", " ".join("%.3f(%.0fms)" % dd for dd in drops if t0 <= dd[0] < t1))
    print("clicks:", " ".join("%.3f" % cc for cc in clicks if t0 <= cc < t1))
