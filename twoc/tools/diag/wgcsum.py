"""wgcsum.py OUT.txt [T0 T1] [--period=MS]: display changes from a wgctimes.exe log.

Prints, per 10 s of the log (or for [T0, T1] s after its first frame), how
many refreshes lay between consecutive new frames on screen (the interval
over DWM's refresh period, rounded) and how many 5-then-3 pairs (a frame
shown a refresh late, the next back on the grid) there were. At 60 fps on a
240 Hz display every interval should be 4.
"""
import collections
import sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
period = float(next((a[9:] for a in sys.argv if a.startswith("--period=")), 1000.0 / 240))
rows = [l.split() for l in open(args[0])]
t = [float(r[0]) for r in rows]
fc = [int(r[1]) for r in rows]
hs = [r[2] if len(r) > 2 else None for r in rows]
base = t[0]
iv = [((t[i] - base) / 1000.0, (t[i] - t[i - 1]) / period, i) for i in range(1, len(t))]


def show(lo, hi):
    seg = [(a, round(b), i) for a, b, i in iv if lo <= a < hi]
    if not seg:
        return
    c = collections.Counter(n for _, n, _ in seg)
    pairs = sum(1 for k in range(len(seg) - 1) if seg[k][1] == 5 and seg[k + 1][1] == 3)
    odd = sum(v for k, v in c.items() if k != 4)
    print("%5.0f-%-5.0f s: %4d frames, refreshes between them %s; not 4: %d (%.1f%%), 5-then-3 pairs %d"
          % (lo, hi, len(seg), dict(sorted(c.items())), odd, 100.0 * odd / len(seg), pairs))
    if hs[0] is not None and hs[0] != "0000000000000000":
        rep = sum(1 for _, _, i in seg if hs[i] == hs[i - 1])
        steps = collections.Counter(fc[i] - fc[i - 1] for _, _, i in seg)
        print("               same picture as the frame before: %d; game frame counter steps per shown frame %s"
              % (rep, dict(sorted(steps.items()))))


if len(args) >= 3:
    show(float(args[1]), float(args[2]))
else:
    for w in range(0, int(iv[-1][0]) + 10, 10):
        show(w, w + 10)
