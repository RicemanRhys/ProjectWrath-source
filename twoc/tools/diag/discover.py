"""Find guest dwords that behave like frame counters in a fpsrun --discover dump
(guest 0x100000..0x946000 every 5 s, then 0x1000 bytes of the D3D device).
python discover.py TAG.disc.bin [t_from] [t_to] [min_rate] [max_rate]"""
import array, struct, sys

path = sys.argv[1]
t_from = float(sys.argv[2]) if len(sys.argv) > 2 else 60
t_to = float(sys.argv[3]) if len(sys.argv) > 3 else 178
lo = float(sys.argv[4]) if len(sys.argv) > 4 else 1
hi = float(sys.argv[5]) if len(sys.argv) > 5 else 40
BASE, GLEN = 0x100000, 0x946000 - 0x100000
rows = []
with open(path, "rb") as f:
    while True:
        h = f.read(16)
        if len(h) < 16: break
        t, lg, ld = struct.unpack("<dII", h)
        g = f.read(lg); d = f.read(ld)
        if lg != GLEN or not (t_from <= t <= t_to): continue
        a = array.array("I"); a.frombytes(g + d.ljust(0x1000, b"\0"))
        rows.append((t, a))
print(f"{len(rows)} samples in window {t_from}-{t_to}s")
span = rows[-1][0] - rows[0][0]
first, last = rows[0][1], rows[-1][1]
cands = [i for i in range(len(first)) if lo * span < last[i] - first[i] < hi * span]
print(f"{len(cands)} pass the end-to-end rate test")
for i in cands:
    col = [r[1][i] for r in rows]
    steps = [b - a for a, b in zip(col, col[1:])]
    if min(steps) <= 0: continue
    va = BASE + 4 * i
    where = f"0x{va:08X}" if 4 * i < GLEN else f"dev+0x{4*i - GLEN:03X}"
    print(f"  {where}  rate {(col[-1]-col[0])/span:7.2f}/s  steps {min(steps)}..{max(steps)}  {col[0]} -> {col[-1]}")
