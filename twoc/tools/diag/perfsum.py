"""perfsum.py PREFIX T0 T1 [--map=MAP]: frame-time stats and per-thread CPU for [T0,T1] s."""
import bisect
import re
import sys

pre, t0, t1 = sys.argv[1], float(sys.argv[2]) * 1000, float(sys.argv[3]) * 1000
mp = next((a[6:] for a in sys.argv if a.startswith("--map=")), None)
fr = [tuple(map(float, l.split())) for l in open(pre + ".frames.txt")]
fr = [(t, c) for t, c in fr if t0 <= t <= t1]
gaps = [(fr[i + 1][0] - fr[i][0], fr[i + 1][1] - fr[i][1]) for i in range(len(fr) - 1)]
if fr:
    dur = (fr[-1][0] - fr[0][0]) / 1000
    nfr = fr[-1][1] - fr[0][1]
    per = sorted(g / max(n, 1) for g, n in gaps for _ in range(int(n)))   # time per game frame
    big = sorted((g for g, n in gaps), reverse=True)
    print("%s %.0f-%.0f s: %d frames in %.1f s = %.1f fps" % (pre, t0 / 1000, t1 / 1000, nfr, dur, nfr / dur))
    q = lambda p: per[min(len(per) - 1, int(p * len(per)))]
    print("  frame time ms: median %.2f  p90 %.2f  p99 %.2f  max gap %.1f" % (q(0.5), q(0.9), q(0.99), big[0]))
    print("  gaps between counter changes: >20 ms %d, >25 ms %d, >33 ms %d, >50 ms %d, >100 ms %d (per minute: >25 ms %.1f)"
          % tuple([sum(1 for g in big if g > x) for x in (20, 25, 33.4, 50, 100)] + [sum(1 for g in big if g > 25) * 60 / dur]))
syms = []
if mp:
    for l in open(mp, encoding="utf-8", errors="replace"):
        m = re.match(r"\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s", l)
        if m:
            syms.append((int(m.group(2), 16), m.group(1)))
    syms.sort()
addrs = [a for a, _ in syms]
base = None
cpu = {}
n = 0
for l in open(pre + ".cpu.txt"):
    if l.startswith("base"):
        base = int(l.split()[1], 16)
        continue
    parts = l.split()
    t = float(parts[0]) * 1000
    if not (t0 < t <= t1):
        continue
    n += 1
    for p in parts[1:]:
        tid, st, d = p.split(":")
        key = (tid, int(st, 16))
        cpu[key] = cpu.get(key, 0) + float(d)


def nm(a):
    if base and base <= a < base + 0x10000000:
        if syms:
            k = bisect.bisect_right(addrs, a - base + 0x140000000) - 1
            return syms[k][1]
        return "exe+%x" % (a - base)
    return "sys:%x" % (a & 0xFFFFFF)


tot = sum(cpu.values())
print("  CPU over %d s: total %.0f%% of a core" % (n, tot / max(n, 1) / 10))
for (tid, st), v in sorted(cpu.items(), key=lambda x: -x[1])[:10]:
    print("    t%-6s %-40s %5.1f%%" % (tid, nm(st), v / max(n, 1) / 10))
