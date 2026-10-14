"""pathlog.py ERRLOG [THR] -- step 69: for step69_pathlog.patch's [TMP] lines (RECOMP_TMP_PATHLOG=1): swaps where a draw
path's count (rigid A5ED2, skins A8233/A614F) jumps by over THR (15), with the objects (item+0x1C) that came and went and their distances."""
import sys, re, collections
NAMES = {"A5ED2": "rigid", "A5EF1": "rigid8", "A5F0B": "rigid7", "A614F": "skinB", "A8233": "skinA"}
thr = int(sys.argv[2]) if len(sys.argv) > 2 else 15
rows = []
for l in open(sys.argv[1], "rb"):
    l = l.decode("latin1")
    if not l.startswith("[TMP] swap"): continue
    f = l.split()
    sw = int(f[2])
    d = []
    for t in f[3:]:
        if t.count(":") != 4: continue
        p, it, ob, ty, dist = t.split(":")
        d.append((NAMES.get(p, p), it, ob, int(ty), float(dist)))
    rows.append((sw, d))
prev = None
for sw, d in rows:
    c = collections.Counter(x[0] for x in d)
    if prev:
        pc = prev[1]
        if any(abs(c[k] - pc[k]) > thr for k in set(c) | set(pc)):
            print("swap %d: %s  (before %s)" % (sw, dict(c), dict(pc)))
            po = {(x[0], x[2]) for x in prev[2]}; no = {(x[0], x[2]) for x in d}
            gone = collections.Counter(k[0] for k in po - no); new = collections.Counter(k[0] for k in no - po)
            dist_new = sorted({round(x[4], 1) for x in d if (x[0], x[2]) in no - po})[:8]
            dist_gone = sorted({round(x[4], 1) for x in prev[2] if (x[0], x[2]) in po - no})[:8]
            print("   objects gone %s at %s; new %s at %s" % (dict(gone), dist_gone, dict(new), dist_new))
    prev = (sw, c, d)
