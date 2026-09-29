"""ddsum.py OUT.txt [T0 T1] [--period=MS] [--list]: what a window showed, from a ddtimes.exe log.

Per 10 s of the log (or for [T0, T1] s after its first desktop frame): how
many refreshes each picture of the window stayed on screen (time from one
change of the window's hash to the next, over the refresh period, rounded),
how many 5-then-3 pairs there were (PresentMon's 20.8 then 12.5 ms), and how
many desktop frames the duplication missed (accumulated > 1). At 60 fps on a
240 Hz display every picture should stay 4 refreshes. --list prints each
interval that isn't 4 with its time.
"""
import collections
import sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
period = float(next((a[9:] for a in sys.argv if a.startswith("--period=")), 1000.0 / 240))
rows = [l.split() for l in open(args[0])]
t = [float(r[0]) for r in rows]
acc = [int(r[1]) for r in rows]
hs = [r[2] for r in rows]
base = t[0]
changes = [i for i in range(1, len(rows)) if hs[i] != hs[i - 1]]
iv = [((t[changes[k]] - base) / 1000.0, (t[changes[k]] - t[changes[k - 1]]) / period, changes[k])
      for k in range(1, len(changes))]


def show(lo, hi):
    seg = [(a, round(b), i) for a, b, i in iv if lo <= a < hi]
    if not seg:
        return
    c = collections.Counter(n for _, n, _ in seg)
    pairs = sum(1 for k in range(len(seg) - 1) if seg[k][1] == 5 and seg[k + 1][1] == 3)
    odd = sum(v for k, v in c.items() if k != 4)
    missed = sum(1 for i in range(len(rows)) if lo <= (t[i] - base) / 1000.0 < hi and acc[i] > 1)
    print("%5.0f-%-5.0f s: %4d pictures, refreshes each stayed %s; not 4: %d (%.1f%%), 5-then-3 pairs %d;"
          " desktop frames missed by the capture %d"
          % (lo, hi, len(seg), dict(sorted(c.items())), odd, 100.0 * odd / len(seg), pairs, missed))
    if "--list" in sys.argv:
        print("   " + " ".join("%.2f:%d" % (a, n) for a, n, _ in seg if n != 4))


if len(args) >= 3:
    show(float(args[1]), float(args[2]))
else:
    for w in range(0, int(iv[-1][0]) + 10, 10):
        show(w, w + 10)
