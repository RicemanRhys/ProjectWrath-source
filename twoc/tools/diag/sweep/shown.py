"""shown.py GAP LOG...: per run, the frames shown at GAP refreshes after the one before, of all measured
(the bridge's "frames shown this interval" lines), skipping the first interval (the load)."""
import re, sys
gap = int(sys.argv[1])
for p in sys.argv[2:]:
    rows = []
    for l in open(p, errors='replace'):
        m = re.search(r'frames shown this interval, by refreshes since the last shown: 1 (\d+), 2 (\d+), 3 (\d+), '
                      r'4 (\d+), 5 (\d+), 6 (\d+), 7 (\d+), more (\d+)', l)
        if m:
            rows.append(list(map(int, m.groups())))
    rows = rows[1:]
    tot = sum(sum(r) for r in rows)
    n = sum(r[gap - 1] for r in rows)
    print("%s: at %d refreshes %d of %d (%.1f%%)" % (p.replace('\\', '/').split('/')[-1], gap, n, tot,
                                                       100.0 * n / max(tot, 1)))
