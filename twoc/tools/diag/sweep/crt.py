"""crt.py FILE -- step 100: crate positions in a level's .crt (x y z floats followed by a zero word, plausible
coordinates), in file order, deduplicated to 1 unit."""
import math, struct, sys
b = open(sys.argv[1], "rb").read()
seen, out = set(), []
for o in range(6, len(b) - 16):
    x, y, z = struct.unpack_from("<3f", b, o)
    if struct.unpack_from("<I", b, o + 12)[0] != 0:
        continue
    if not all(math.isfinite(v) and 0.05 < abs(v) < 2000 for v in (x, y, z)):
        continue
    k = (round(x), round(y), round(z))
    if k in seen:
        continue
    seen.add(k)
    out.append((o, x, y, z))
for i, (o, x, y, z) in enumerate(out):
    if i % max(1, len(out) // 30) == 0:
        print("%4d @%5d %8.2f %8.2f %8.2f" % (i, o, x, y, z))
print(len(out), "positions; x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f" % (
    min(p[1] for p in out), max(p[1] for p in out), min(p[2] for p in out), max(p[2] for p in out),
    min(p[3] for p in out), max(p[3] for p in out)))
