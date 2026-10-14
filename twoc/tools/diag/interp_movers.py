"""interp_movers.py ERRLOG [MIN MAX] -- in the pair log, 'new' objects lying MIN-MAX units (default 0.2-4) from an object of the
frame before (any key) that was also 'new' or moved: candidates for an object whose key changes every frame. Prints
chains swap by swap with the key, the step and whether the keys differ."""
import sys, re, collections
lo, hi = (float(sys.argv[2]), float(sys.argv[3])) if len(sys.argv) > 3 else (0.2, 4.0)
rows = collections.defaultdict(list)
for l in open(sys.argv[1], encoding="latin-1"):
    m = re.search(r"pairlog swap (\d+) obj (\d+) key (\w+)(?: \(kind \w+ mesh \w+\))? pos (\S+) (\S+) (\S+).* (\S+)$", l.strip())
    if m:
        rows[int(m.group(1))].append((int(m.group(2)), m.group(3), tuple(float(m.group(i)) for i in (4, 5, 6)), m.group(7)))
ss = sorted(rows)
for a, b in zip(ss, ss[1:]):
    if b != a + 1:
        continue
    out = []
    for o, k, p, w in rows[b]:
        if w != "new":
            continue
        best = None
        for o2, k2, p2, w2 in rows[a]:
            d = sum((x - y) ** 2 for x, y in zip(p, p2)) ** 0.5
            if best is None or d < best[0]:
                best = (d, o2, k2, p2, w2)
        if best and lo <= best[0] <= hi:
            v = tuple(x - y for x, y in zip(p, best[3]))
            out.append("obj %d key %s pos %.2f %.2f %.2f <- obj %d key %s (%s) step %.2f v %.2f %.2f %.2f" % (
                o, k, *p, best[1], best[2], best[4], best[0], *v))
    if out:
        print("swap %d: %d" % (b, len(out)))
        for x in out[:8]:
            print("   ", x)
