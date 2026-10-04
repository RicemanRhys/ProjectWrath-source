"""sixch.py OUT.wav [T0 T1] [--win=0.1] [--series=N]

Reads a RECOMP_APU_OUT_WAV dump (6 channels: FL FR C LFE BL BR, or 2) and
prints each channel's level over T0-T1, then, per window with sound, where
the sound sits: left-right balance (FL+BL against FR+BR) and front-back
balance (FL+FR against BL+BR), both in dB, and how often each speaker is the
loudest. Positional sounds that move show up as balances that change from
window to window."""
import sys, wave
import numpy as np

args = [a for a in sys.argv[1:] if not a.startswith('--')]
opts = dict(a[2:].split('=', 1) for a in sys.argv[1:] if a.startswith('--') and '=' in a)
w = wave.open(args[0])
ch, rate = w.getnchannels(), w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64).reshape(-1, ch) / 32768.0
t0 = float(args[1]) if len(args) > 1 else 0.0
t1 = float(args[2]) if len(args) > 2 else len(x) / rate
x = x[int(t0 * rate):int(t1 * rate)]
names = ['FL', 'FR', 'C', 'LFE', 'BL', 'BR'][:ch] if ch == 6 else ['L', 'R']
db = lambda e: 10 * np.log10(e + 1e-12)
print('%s: %d ch, %.1f s (%.1f-%.1f)' % (args[0], ch, len(x) / rate, t0, t1))
print('  level  ' + '  '.join('%s %6.1f' % (n, db((x[:, i] ** 2).mean())) for i, n in enumerate(names)))
if ch != 6:
    sys.exit()
win = int(float(opts.get('win', '0.1')) * rate)
n = len(x) // win
e = (x[:n * win] ** 2).reshape(n, win, ch).mean(1)
tot = e[:, [0, 1, 4, 5]].sum(1)
live = tot > 10 ** (-60 / 10)
L, R = e[:, 0] + e[:, 4], e[:, 1] + e[:, 5]
F, B = e[:, 0] + e[:, 1], e[:, 4] + e[:, 5]
lr = db(L) - db(R)
fb = db(F) - db(B)
print('  windows with sound: %d of %d (%.2f s each)' % (live.sum(), n, win / rate))
if live.sum():
    print('  left-right (dB, + = left):  p5 %6.1f  p25 %6.1f  median %6.1f  p75 %6.1f  p95 %6.1f' %
          tuple(np.percentile(lr[live], [5, 25, 50, 75, 95])))
    print('  front-back (dB, + = front): p5 %6.1f  p25 %6.1f  median %6.1f  p75 %6.1f  p95 %6.1f' %
          tuple(np.percentile(fb[live], [5, 25, 50, 75, 95])))
    loud = np.argmax(e[:, [0, 1, 4, 5]], 1)
    print('  loudest speaker: ' + '  '.join('%s %d%%' % (nm, round(100 * (loud[live] == k).mean()))
                                             for k, nm in enumerate(['FL', 'FR', 'BL', 'BR'])))
    print('  C %.1f dB, LFE %.1f dB of the four corners' % (db(e[live, 2].sum()) - db(tot[live].sum()),
                                                            db(e[live, 3].sum()) - db(tot[live].sum())))
ns = int(opts.get('series', '0'))
if ns:
    idx = np.flatnonzero(live)
    step = max(1, len(idx) // ns)
    for i in idx[::step][:ns]:
        print('   %7.2f s  ' % (t0 + i * win / rate) + '  '.join('%s %6.1f' % (nm, db(e[i, k])) for k, nm in
                                                           enumerate(names)) + '   LR %+5.1f  FB %+5.1f' % (lr[i], fb[i]))
