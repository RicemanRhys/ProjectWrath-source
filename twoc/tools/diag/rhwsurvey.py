"""rhwsurvey.py LOG... -- every pre-transformed (XYZRHW) draw in the draw logs, grouped by swap, caller and target:
count, x/y box union, full-width count, z/rhw range (small draws), and the z test."""
import re, sys
from collections import defaultdict
HDR = re.compile(r"\[D3DB\] drawlog swap (\d+) #(\d+) (\S+) prim (\d+) n (\d+) fvf (\S+) (.*?); tex0 (\S+) .*?target (\S+); from (\S+); box \(([-\d.e+]+), ([-\d.e+]+)\)-\(([-\d.e+]+), ([-\d.e+]+)\)")
for path in sys.argv[1:]:
    g = defaultdict(lambda: dict(n=0, full=0, box=[1e9, 1e9, -1e9, -1e9], zr=[1e9, -1e9], wr=[1e9, -1e9], z=set(), tex=set()))
    cur = None
    for line in open(path, encoding="latin1"):
        m = HDR.search(line)
        if m:
            fvf = int(m.group(6), 16)
            cur = None
            if fvf & 0xE == 4 and not (fvf & 1):
                k = (m.group(1), m.group(10), m.group(9))
                e = g[k]
                e["n"] += 1
                x0, y0, x1, y1 = map(float, m.group(11, 12, 13, 14))
                e["box"] = [min(e["box"][0], x0), min(e["box"][1], y0), max(e["box"][2], x1), max(e["box"][3], y1)]
                e["full"] += x0 <= 1 and x1 >= 639
                e["tex"].add(m.group(8))
                cur = e
            continue
        if cur is not None and "drawlog   blend" in line:
            mm = re.search(r" z (\d+)/(\d+);", line)
            if mm:
                cur["z"].add(mm.group(1) + "/" + mm.group(2))
        if cur is not None and "pre-transformed (" in line:
            for v in re.findall(r"\(([-\d.e+]+) ([-\d.e+]+) ([-\d.e+]+) ([-\d.e+]+)", line):
                z, w = float(v[2]), float(v[3])
                cur["zr"] = [min(cur["zr"][0], z), max(cur["zr"][1], z)]
                cur["wr"] = [min(cur["wr"][0], w), max(cur["wr"][1], w)]
    print("==", path)
    for k in sorted(g):
        e = g[k]
        b = e["box"]
        print("swap %s from %s target %-5s draws %4d full-width %4d box (%.0f,%.0f)-(%.0f,%.0f) z %s rhw %s ztest %s tex %d" % (
            k[0], k[1], k[2], e["n"], e["full"], b[0], b[1], b[2], b[3],
            "%.3g..%.3g" % tuple(e["zr"]) if e["zr"][0] < 1e9 else "-", "%.3g..%.3g" % tuple(e["wr"]) if e["wr"][0] < 1e9 else "-",
            ",".join(sorted(e["z"])), len(e["tex"])))
