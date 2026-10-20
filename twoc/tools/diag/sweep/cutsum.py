"""cutsum.py LOG [LEVELMARK...] -- step 96: a D3 run's per-frame "[DIAG] match" lines (RECOMP_DIAG_MATCHLOG=1):
frames judged; the old rule's decision (the exact key, strict) against the new one's (by texture shape); every
frame the old rule held for matches and the new one interpolates, in runs of consecutive swaps, with the camera's
step and turn; frames the new rule holds, by reason; near-empty matches standing still (the render-thread race)."""
import re, sys

pat = re.compile(r'\[DIAG\] match swap (\d+) draws (\d+) \(before (\d+)\): strict ([\d.]+)%, shape ([\d.]+)%.*?; '
                 r'step (-?[\d.]+) turn ([\d.]+) (.*)$')
lev = re.compile(r'Crashdat\\levels\\[ab]\\([a-z_0-9]+)\\')
rows, where = [], '?'
for l in open(sys.argv[1], 'rb').read().decode('latin-1').splitlines():
    m = lev.search(l)
    if m and m.group(1) not in ('pause',):
        where = m.group(1)
    m = pat.search(l)
    if m:
        sw, n, nb = int(m.group(1)), int(m.group(2)), int(m.group(3))
        st, sh, step, turn, why = float(m.group(4)), float(m.group(5)), float(m.group(6)), float(m.group(7)), m.group(8)
        rows.append((sw, n, nb, st, sh, step, turn, why.strip(), where))
print('%d frames judged' % len(rows))
by = {}
for r in rows:
    by.setdefault(r[8], []).append(r)
for w, rs in by.items():
    old_hold = sum(1 for r in rs if r[7] == 'interpolated' and r[3] < 50) + sum(1 for r in rs if r[7] != 'interpolated')
    new_hold = sum(1 for r in rs if r[7] != 'interpolated')
    reasons = {}
    for r in rs:
        if r[7] != 'interpolated':
            reasons[r[7]] = reasons.get(r[7], 0) + 1
    print('  %-10s frames %5d, strict mean %5.1f%% (<50%%: %d), shape mean %5.1f%%; held old rule %d, new rule %d %s' % (
        w, len(rs), sum(r[3] for r in rs) / len(rs), sum(1 for r in rs if r[3] < 50), sum(r[4] for r in rs) / len(rs),
        old_hold, new_hold, reasons))
# frames the old rule held for matches only and the new one interpolates, as runs of consecutive swaps
flip = [r for r in rows if r[7] == 'interpolated' and r[3] < 50]
runs = []
for r in flip:
    if runs and r[0] - runs[-1][-1][0] <= 2 and r[8] == runs[-1][-1][8]:
        runs[-1].append(r)
    else:
        runs.append([r])
print('old rule held, new interpolates: %d frames in %d runs' % (len(flip), len(runs)))
for rr in runs[:40]:
    print('  %-10s swaps %d-%d (%d): strict %.0f-%.0f%%, shape %.0f-%.0f%%, step max %.3f, turn max %.2f' % (
        rr[0][8], rr[0][0], rr[-1][0], len(rr), min(r[3] for r in rr), max(r[3] for r in rr),
        min(r[4] for r in rr), max(r[4] for r in rr), max(r[5] for r in rr), max(r[6] for r in rr)))
# held by the new rule for matches
hm = [r for r in rows if r[7] == 'too few draws matched']
print('new rule held for matches: %d' % len(hm))
for r in hm[:40]:
    print('  %-10s swap %d draws %d (before %d) strict %.1f shape %.1f step %.3f turn %.2f' % (r[8], r[0], r[1], r[2], r[3], r[4], r[5], r[6]))
odd = [r for r in rows if r[4] < 10 and 0 <= r[5] < 0.1 and r[6] < 1 and r[1] == r[2]]
print('near-empty matches standing still with the same draw count (the race): %d' % len(odd))
