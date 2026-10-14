"""interp_pairlog.py ERRLOG [KEY] -- the pair log (RECOMP_D3D_BRIDGE_PAIRLOG) summed: per swap, decisions; per key with 3+
objects a swap (identical objects), how many paired a step of over half the object's size (a neighbour's place)
and how many were new or snapped. KEY: that key's objects swap by swap."""
import sys, re, collections
rows = collections.defaultdict(list)
for l in open(sys.argv[1], encoding="latin-1"):
    m = re.search(r"pairlog swap (\d+) obj (\d+) key (\w+)(?: \(kind \w+ mesh \w+\))? pos (\S+) (\S+) (\S+)(?: prev (-?\d+) step (\S+)(?: off its motion \S+)? near (\S+) size (\S+))?(?: skeleton -?\d+)? (\S+)$", l.strip())
    if m:
        rows[int(m.group(1))].append(dict(obj=int(m.group(2)), key=m.group(3), pos=tuple(float(m.group(i)) for i in (4, 5, 6)),
                                         prev=int(m.group(7) or -1), step=float(m.group(8) or -1), near=float(m.group(9) or -1),
                                         size=float(m.group(10) or 0), what=m.group(11)))
tot = collections.Counter()
far = 0
groups = collections.Counter()
for s in sorted(rows):
    r = rows[s]
    c = collections.Counter(x["what"] for x in r)
    tot.update(c)
    bykey = collections.defaultdict(list)
    for x in r:
        bykey[x["key"]].append(x)
    f = [x for k, v in bykey.items() if len(v) >= 3 for x in v if x["what"] == "pair" and x["size"] > 0 and x["step"] > 0.5 * x["size"]]
    far += len(f)
    for k, v in bykey.items():
        if len(v) >= 3:
            groups[k] = max(groups[k], len(v))
    print("swap %d: %d objects, %s; paired over half their size in a group: %d %s" % (
        s, len(r), dict(c), len(f), " ".join("%s:%.2f/%.2f" % (x["key"][:4], x["step"], x["size"]) for x in f[:6])))
print("total", dict(tot), "far pairs", far)
print("groups (3+):", groups.most_common(10))
if len(sys.argv) > 2:
    for s in sorted(rows):
        for x in rows[s]:
            if x["key"] == sys.argv[2]:
                print(s, x)
