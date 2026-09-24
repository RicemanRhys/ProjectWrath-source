"""Aggregate prof_sampler.log (step 26 temp sampler) with the /MAP linker map.

Usage: python prof.py crash_twoc.map prof_sampler.log [t_from_ms] [t_to_ms]
"""
import bisect
import collections
import re
import sys

mp, log = sys.argv[1], sys.argv[2]
t_from = int(sys.argv[3]) if len(sys.argv) > 3 else 0
t_to = int(sys.argv[4]) if len(sys.argv) > 4 else 1 << 62

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
    if not (base <= a < base + 0x10000000):
        return "<sys>"
    p = a - base + pref
    k = bisect.bisect_right(addrs, p) - 1
    if k < 0 or p - addrs[k] > 0x100000:
        return "<exe?>"
    return syms[k][1]


main_tid = None
snaps = []          # (t, hdr dict, {tid: (cpu, [names])})
cur = None
base = None
for l in open(log, encoding="utf-8", errors="replace"):
    m = re.match(r"main tid (\d+)", l)
    if m:
        main_tid = m.group(1)
        continue
    m = re.match(r"=== t=(\d+) ms exe base ([0-9A-F]{16}) (.*)", l)
    if m:
        base = int(m.group(2), 16)
        hdr = dict(kv.split("=") for kv in m.group(3).split())
        cur = (int(m.group(1)), hdr, {})
        snaps.append(cur)
        continue
    m = re.match(r"tid (\d+) \[cpu (\d+) ms\]:(.*)", l)
    if m and cur:
        names = [name(int(x, 16), base) for x in m.group(3).split()]
        cur[2][m.group(1)] = (int(m.group(2)), names)

snaps = [s for s in snaps if t_from <= s[0] <= t_to]
print(f"{len(snaps)} snapshots, t={snaps[0][0]}..{snaps[-1][0]} ms, main tid {main_tid}")


def label(names):
    for n in names:
        if n in ("nv2a_ack_thread", "xbox_watchdog_thread", "tmp_sampler"):
            return n
    for n in reversed(names):
        if n.startswith(("sub_", "WinMain", "worker", "kernel_", "xbox_", "apu", "dpc", "vblank")):
            return n
    return names[-1] if names else "?"


threads = collections.defaultdict(list)
for t, hdr, th in snaps:
    for tid, (cpu, names) in th.items():
        threads[tid].append((t, cpu, names))

wall = snaps[-1][0] - snaps[0][0]
print(f"\n== threads (CPU use over {wall} ms of wall time)")
rows = []
for tid, ss in threads.items():
    lab = collections.Counter(label(n) for _, _, n in ss).most_common(1)[0][0]
    cpu = ss[-1][1] - ss[0][1]
    rows.append((cpu, tid, lab, len(ss)))
for cpu, tid, lab, n in sorted(rows, reverse=True)[:12]:
    tag = " (main)" if tid == main_tid else ""
    print(f"  tid {tid}{tag}: {100.0 * cpu / max(wall, 1):5.1f}% CPU, {n} samples, {lab}")


def profile(tid, title, marks):
    ss = threads.get(tid, [])
    if not ss:
        return
    print(f"\n== {title}: tid {tid}, {len(ss)} samples")
    leaf = collections.Counter()
    for _, _, names in ss:
        ours = [n for n in names if n not in ("<sys>", "<exe?>")]
        leaf[(names[0] if names else "?") + ("  [in " + ours[0] + "]" if ours and ours[0] != names[0] else "")] += 1
    for k, v in leaf.most_common(12):
        print(f"  {100.0 * v / len(ss):5.1f}%  {k}")
    for mk in marks:
        n = sum(1 for _, _, names in ss if mk in names)
        print(f"  on stack {mk}: {100.0 * n / len(ss):5.1f}%")


profile(main_tid, "main thread", ["sub_00103330", "sub_001034D0", "sub_00103300", "sub_001035C0",
                                  "sub_00101BC0", "sub_0003B010", "sub_00106720"])
ack = [tid for tid, ss in threads.items() if any("nv2a_ack_thread" in n for _, _, n in ss)]
for tid in ack:
    profile(tid, "NV2A ack thread", ["nv2a_pb_scan", "nv2a_pb_exec", "framebuffer_probe_tick"])

print("\n== GPU state per second")
last = None
for t, hdr, _ in snaps:
    sec = t // 1000
    if sec == last:
        continue
    last = sec
    print(f"  t={t:6d} put={hdr['put']} get={hdr['get']} fence={hdr['fence']} gpu={hdr['gpu']} "
          f"pfb={hdr['pfb']} vbl={hdr['vbl']}")
