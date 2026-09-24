"""Symbolize the sampler's host frames with the linker map.

Usage: python symbolize.py crash_twoc.map hang_sampler.log [first_snapshot_index]
"""
import bisect
import re
import sys

mp, log = sys.argv[1], sys.argv[2]
first = int(sys.argv[3]) if len(sys.argv) > 3 else 0

pref = 0x140000000
syms = []
for l in open(mp, encoding="utf-8", errors="replace"):
    m = re.search(r"Preferred load address is ([0-9a-fA-F]+)", l)
    if m:
        pref = int(m.group(1), 16)
    m = re.match(r"\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s", l)
    if m:
        syms.append((int(m.group(2), 16), m.group(1)))
syms.sort()
addrs = [s[0] for s in syms]


def name(a, base):
    p = a - base + pref
    k = bisect.bisect_right(addrs, p) - 1
    if k < 0 or p - addrs[k] > 0x100000:
        return f"{a:016X}"
    return f"{syms[k][1]}+0x{p - addrs[k]:X}"


snap = -1
base = None
for l in open(log, encoding="utf-8", errors="replace"):
    m = re.match(r"=== t=(\d+) ms exe base ([0-9A-F]{16})", l)
    if m:
        snap += 1
        base = int(m.group(2), 16)
        if snap >= first:
            print(l.rstrip())
        continue
    if snap < first or not l.startswith("tid "):
        continue
    tid, rest = l.split(":", 1)
    out = []
    for tok in rest.split():
        if re.fullmatch(r"[0-9A-F]{16}", tok):
            v = int(tok, 16)
            out.append(name(v, base) if base <= v < base + 0x10000000 else tok)
        else:
            out.append(tok)
    if any(o.startswith(("sub_", "xbox_", "recomp", "kernel", "tmp_", "apu", "mcpx")) or "twoc" in o for o in out) \
            or any(not re.fullmatch(r"[0-9A-F]{16}", o) and "+0x" in o and not o.startswith(("Zw", "Nt", "Rtl", "Base", "Tp", "Wait", "Sleep", "CoreUI", "Set", "Create")) for o in out):
        print(f"  {tid}: " + " <- ".join(out))
