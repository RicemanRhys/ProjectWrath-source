"""Map guest addresses to the lifted function that contains them.

Usage: python fnof.py ADDR [ADDR ...]         (hex, e.g. 1241B3 or 0x2C032)
Reads the "sub_XXXXXXXX / Original: 0xA - 0xB" headers of the generated
twoc/src/recomp/gen/recomp_*.c (override with RECOMP_GEN_DIR).
"""
import bisect
import glob
import os
import re
import sys

gen = os.environ.get("RECOMP_GEN_DIR") or os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "src", "recomp", "gen")
rng = []
for f in sorted(glob.glob(os.path.join(gen, "recomp_*.c"))):
    name = None
    for i, l in enumerate(open(f, encoding="utf-8", errors="replace")):
        m = re.match(r" \* (sub_[0-9A-F]{8})$", l)
        if m:
            name = m.group(1)
            continue
        m = re.match(r" \* Original: 0x([0-9A-F]+) - 0x([0-9A-F]+)", l)
        if m and name:
            rng.append((int(m.group(1), 16), int(m.group(2), 16), name, os.path.basename(f), i + 1))
            name = None
if not rng:
    sys.exit(f"no lifted function headers found under {gen}")
rng.sort()
starts = [r[0] for r in rng]
for a in sys.argv[1:]:
    v = int(a, 16)
    k = bisect.bisect_right(starts, v) - 1
    if k < 0:
        print(f"{v:#010x} -> below the first lifted function")
        continue
    r = rng[k]
    ok = r[0] <= v <= r[1]
    print(f"{v:#010x} -> {r[2]} [{r[0]:#x}..{r[1]:#x}] {r[3]}:{r[4]}" + ("" if ok else " (OUTSIDE)"))
