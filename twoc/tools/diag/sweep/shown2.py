"""shown2.py LOG... -- step 79: per run, the frames shown 1 refresh after the one before, of all measured, over its
last K (2, env) "frames shown this interval" lines (10 s each: the measured stretch, not the loads), with the
unmeasured count beside."""
import os, re, sys

K = int(os.environ.get("K", "2"))
for p in sys.argv[1:]:
    rows = []
    for l in open(p, errors='replace'):
        m = re.search(r'frames shown this interval, by refreshes since the last shown: 1 (\d+), 2 (\d+), 3 (\d+), '
                      r'4 (\d+), 5 (\d+), 6 (\d+), 7 (\d+), more (\d+); unmeasured (\d+); at another gap than presented (\d+)', l)
        if m:
            rows.append(list(map(int, m.groups())))
    rows = rows[-K:]
    tot = sum(sum(r[:8]) for r in rows)
    print("%-22s at 1 refresh %5d of %5d (%5.1f%%), at 2 %4d, more %3d; unmeasured %d; not at the gap presented %d (%.2f%%)" %
          (os.path.basename(p).replace(".err.log", ""), sum(r[0] for r in rows), tot, 100.0 * sum(r[0] for r in rows) / max(tot, 1),
           sum(r[1] for r in rows), sum(sum(r[2:8]) for r in rows), sum(r[8] for r in rows), sum(r[9] for r in rows),
           100.0 * sum(r[9] for r in rows) / max(tot, 1)))
