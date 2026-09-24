"""Compare two run_twoc.err.log files on behaviour milestones, ignoring timing noise.

Usage: python compare_runs.py a.err.log b.err.log
"""
import re
import sys


def milestones(path):
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    files = []
    for l in lines:
        m = re.search(r"\[PATH\] \\Device\\CdRom0\\Crashdat\\(.+)$", l)
        if m and (not files or files[-1] != m.group(1)):
            files.append(m.group(1))
    events = [l.strip() for l in lines
              if re.match(r"\s*\[(BOOT|DSP|XPP\] (open|close|sub_000EB|port)|DEV|ICALL|GAME|APU)", l)
              or "MAIN MENU" in l or "Exception" in l or "ACCESS_VIOLATION" in l]
    threads = len({m.group(1) for l in lines for m in [re.search(r"\[CS\] thread (\d+)", l)] if m})
    return files, events, threads, len(lines)


a, b = milestones(sys.argv[1]), milestones(sys.argv[2])
for name, (files, events, threads, n) in (("A", a), ("B", b)):
    print(f"{name}: {n} lines, {len(files)} distinct CdRom file opens (in order), "
          f"{len(events)} event lines, {threads} guest threads seen contending")
common = 0
for x, y in zip(a[0], b[0]):
    if x != y:
        break
    common += 1
print(f"file-open sequence: identical for the first {common} entries "
      f"(A has {len(a[0])}, B has {len(b[0])})")
if common < max(len(a[0]), len(b[0])):
    print("  A continues:", a[0][common:common + 5])
    print("  B continues:", b[0][common:common + 5])
print("last file A:", a[0][-1] if a[0] else None)
print("last file B:", b[0][-1] if b[0] else None)
ea = [re.sub(r"0x[0-9A-F]{8}", "X", e) for e in a[1]]
eb = [re.sub(r"0x[0-9A-F]{8}", "X", e) for e in b[1]]
print("event lines only in A:", sorted(set(ea) - set(eb)))
print("event lines only in B:", sorted(set(eb) - set(ea)))
