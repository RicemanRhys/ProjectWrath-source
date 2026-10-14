"""interp_skelstep.py ERRLOG -- for paired skinned draws in skeletons (pair log), the first-bone step (|pos - partner's pos|,
step 62's measure) against the step the snap now uses (the skeleton's G); how many of each are over 1.5."""
import sys, re, collections
rows = collections.defaultdict(dict)
lines = []
for l in open(sys.argv[1], encoding="latin-1"):
    m = re.search(r"pairlog swap (\d+) obj (\d+) key \w+ \(kind \w+ mesh (\w+)\) pos (\S+) (\S+) (\S+) prev (-?\d+) step (\S+) off its motion (\S+) near \S+ size \S+ skeleton (-?\d+) (\S+)$", l.strip())
    if m:
        s, o = int(m.group(1)), int(m.group(2))
        pos = tuple(float(m.group(i)) for i in (4, 5, 6))
        rows[s][o] = pos
        lines.append((s, o, m.group(3), pos, int(m.group(7)), float(m.group(8)), float(m.group(9)), int(m.group(10)), m.group(11)))
n = fb = gs = 0
worst = []
for s, o, mesh, pos, prev, step, off, sk, what in lines:
    if sk < 0 or prev < 0 or s - 1 not in rows or prev not in rows[s - 1]:
        continue
    q = rows[s - 1][prev]
    d = sum((a - b) ** 2 for a, b in zip(pos, q)) ** 0.5
    n += 1
    fb += d > 1.5
    gs += step > 1.5
    worst.append((d, s, o, mesh, step, off, what))
worst.sort(reverse=True)
print("%d skeleton draws paired; first-bone step over 1.5: %d; skeleton step over 1.5: %d" % (n, fb, gs))
for w in worst[:8]:
    print("  first-bone step %.3f swap %d obj %d mesh %s | skeleton step %.3f off its motion %.3f -> %s" % w)
