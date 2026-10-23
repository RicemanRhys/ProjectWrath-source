"""dsum.py TAG MUSIC TARGET [T0 T1] -- step 102 part 3: residual.py's per-second lines over a run's APU capture,
summarised over the seconds where the music fits (mask corr > 0.6): their count, the music fit's median, the median
and 90th percentile of the residual's correlation with TARGET (the drone), and the residual's median level."""
import os, re, subprocess, sys
import numpy as np
D = r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag\residual.py"
G = r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\game_files\Crashdat\sfx"
tag, mus, tgt = sys.argv[1:4]
wav = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'runs', tag + '.wav')
import wave
w = wave.open(wav)
dur = w.getnframes() / w.getframerate()
t0 = float(sys.argv[4]) if len(sys.argv) > 4 else 30.0
t1 = float(sys.argv[5]) if len(sys.argv) > 5 else dur - 1
out = subprocess.run([sys.executable, D, wav, str(t0), str(t1), os.path.join(G, mus), os.path.join(G, tgt)],
                     capture_output=True, text=True).stdout
rows = []
for l in out.splitlines():
    m = re.match(r'\s*([\d.]+) s\s+mask corr ([\d.]+)\s+residual\s+(\S+) dB\s+target at\s+\S+ s corr ([\d.]+)', l)
    if m:
        rows.append(tuple(float(x) if x != 'nan' else np.nan for x in m.groups()))
a = np.array([r for r in rows if r[1] > 0.6])
if not len(a):
    print('%s: the music never fits (%d s looked at)' % (tag, len(rows)))
    sys.exit(0)
print('%-8s %s -> %s: %3d s with the music (fit median %.3f); drone corr median %.3f, p90 %.3f; residual median %.1f dB'
      % (tag, mus.split('/')[-1], tgt.split('/')[-1], len(a), np.median(a[:, 1]), np.median(a[:, 3]),
         np.percentile(a[:, 3], 90), np.nanmedian(a[:, 2])))
