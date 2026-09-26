"""levelheap.py ERRLOG -- heap figures per level segment (a segment starts at a level's first .nux open).

Per segment: bump used max, heap live max, and (fixed builds) zone live max, zone largest free min,
'live less zone free' max, pool live max; (old builds) largest free block min."""
import re, sys

segs = []
cur = None
for l in open(sys.argv[1], encoding="latin1"):
    m = re.search(r"levels\\([abc])\\(\w+)\\\w+\.nux", l)
    if m and m.group(2) not in ("pause",) and (cur is None or cur["name"] != m.group(2)):
        cur = {"name": m.group(2), "used": 0, "live": 0, "lg": None, "zlive": 0, "zlg": None, "real": 0, "tohe": 0}
        segs.append(cur)
    if cur is None:
        continue
    if "[HEAPSTAT]" in l:
        cur["used"] = max(cur["used"], int(re.search(r"used (\d+)/", l).group(1)))
        cur["live"] = max(cur["live"], int(re.search(r"live (\d+) KiB", l).group(1)))
        lg = int(re.search(r"largest (\d+) KiB", l).group(1))
        cur["lg"] = lg if cur["lg"] is None else min(cur["lg"], lg)
    if "[VMSTAT] zone" in l:
        cur["zlive"] = max(cur["zlive"], int(re.search(r"live (\d+) KiB", l).group(1)))
        zl = int(re.search(r"\(largest (\d+) KiB\)", l).group(1))
        cur["zlg"] = zl if cur["zlg"] is None else min(cur["zlg"], zl)
        cur["real"] = max(cur["real"], int(re.search(r"less zone free (\d+) KiB", l).group(1)))
        cur["tohe"] = int(re.search(r"to the heap (\d+)", l).group(1))
print("%-10s %9s %9s %9s %10s %11s %10s %6s" % ("segment", "used", "live", "lgfree", "zone live", "zone lgfree", "real live", "toheap"))
for s in segs:
    print("%-10s %9d %9d %9s %10d %11s %10d %6d" % (s["name"], s["used"], s["live"], s["lg"], s["zlive"], s["zlg"], s["real"], s["tohe"]))
