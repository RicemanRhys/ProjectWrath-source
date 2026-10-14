"""hud_share.py ERRLOG [--passes] -- per HUD call site, the share of the edge shift each logged pass gave it.
Old builds: recomputed from the element ids and boxes (step 65's ramp). Step 68 on: the logged share (-1 left edge, 1 right, 0 kept)."""
import sys, re, collections
passes = collections.OrderedDict()
for l in open(sys.argv[1], encoding="latin-1"):
    m = re.search(r"hudlog pass (\d+) draw (\d+) \S+ \S+ site (\w+) box (\S+) (\S+) (\S+) (\S+) element (-?\d+)(?: share (\S+))?", l)
    if m:
        p = int(m.group(1))
        passes.setdefault(p, []).append((m.group(3), [float(m.group(i)) for i in range(4, 8)], int(m.group(8)),
                                         float(m.group(9)) if m.group(9) else None))
per = collections.defaultdict(list)
for p, ds in passes.items():
    el = {}
    for site, b, e, sh in ds:
        x = el.setdefault(e, [1e9, 1e9, -1e9, -1e9])
        x[0] = min(x[0], b[0]); x[1] = min(x[1], b[1]); x[2] = max(x[2], b[2]); x[3] = max(x[3], b[3])
    seen = {}
    for site, b, e, sh in ds:
        if sh is None:
            c = 0.5 * (el[e][0] + el[e][2])
            a = max(0.0, min(1.0, (abs(c) - 0.2) / 0.25))
            sh = a * (-1 if c < 0 else 1)
        seen.setdefault(site, set()).add(round(sh, 3))
    for s, v in seen.items():
        per[s].append((p, sorted(v)))
for s in sorted(per):
    vals = collections.Counter(tuple(v) for p, v in per[s])
    print(s, len(per[s]), "passes;", ", ".join("%s x%d" % (list(k), n) for k, n in vals.most_common(8)))
if "--passes" in sys.argv:
    for p, ds in passes.items():
        print(p, " ".join(sorted(set("%s:%d" % (d[0], d[2]) for d in ds))))
