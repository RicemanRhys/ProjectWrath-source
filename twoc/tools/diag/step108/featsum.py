"""featsum.py WAV... [--from S] [--to S] -- step 108: per file, each channel's RMS (dBFS) over the stretch, the side
signal (L-R) against the mid (L+R) in dB (very low = centred, as with the HRTF off), the share of samples with
L == R exactly, and the length."""
import sys, wave, argparse
import numpy as np
ap = argparse.ArgumentParser(); ap.add_argument("wav", nargs="+")
ap.add_argument("--from", dest="t0", type=float, default=0.0); ap.add_argument("--to", dest="t1", type=float, default=1e9)
a = ap.parse_args()
for p in a.wav:
    w = wave.open(p); ch, sr, n = w.getnchannels(), w.getframerate(), w.getnframes()
    x = np.frombuffer(w.readframes(n), np.int16).reshape(-1, ch).astype(np.float64) / 32768.0
    x = x[int(a.t0 * sr):int(min(a.t1 * sr, len(x)))]
    db = lambda v: 20 * np.log10(max(np.sqrt((v ** 2).mean()), 1e-12))
    rms = " ".join("%6.1f" % db(x[:, c]) for c in range(ch))
    side = db(x[:, 0] - x[:, 1]) - db(x[:, 0] + x[:, 1])
    same = (x[:, 0] == x[:, 1]).mean() * 100
    print("%-34s %d ch %6.1f s  rms dBFS [%s]  side/mid %6.1f dB  L==R %5.1f%%" % (
        p.replace("\\", "/").split("/")[-1], ch, len(x) / sr, rms, side, same))
