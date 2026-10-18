"""dlsum.py ERRLOG -- one line per drawn draw of a draw log: index, pass, target, blend, z enable/write, fog, path."""
import re, sys

cur = None
rows = []
for ln in open(sys.argv[1], encoding="utf-8", errors="replace"):
    m = re.search(r"drawlog swap (\d+) #(\d+) (\S+) prim (\d+) n (\d+) .*? (mirrored[^;]*|skipped[^;]*);.*?target (\S+); .*?pass (\w+); box \(([-\d.e+]+), ([-\d.e+]+)\)-\(([-\d.e+]+), ([-\d.e+]+)\)", ln)
    if m:
        cur = dict(i=int(m.group(2)), path=m.group(3), n=int(m.group(5)), how=m.group(6)[:22], tgt=m.group(7),
                   pas=m.group(8), box=tuple(float(m.group(k)) for k in range(9, 13)))
        rows.append(cur)
        continue
    if cur is None:
        continue
    m = re.search(r"drawlog   blend (\d+) (\w+) (\w+) op \w+ atest (\d+) \w+ ref \w+ z (\d+)/(\d+);.*fog (\d+)$", ln.strip())
    if m:
        cur.update(blend=int(m.group(1)), sb=m.group(2), db=m.group(3), at=int(m.group(4)), z=int(m.group(5)),
                   zw=int(m.group(6)), fog=int(m.group(7)))
    m = re.search(r"drawlog   fog table (\d+) start (\S+) end (\S+) density (\S+) colour (\w+)", ln)
    if m:
        cur.update(ft=m.group(1), fs=m.group(2), fe=m.group(3), fc=m.group(5))
for r in rows:
    b = r["box"]
    bx = "-" if b[0] > 1e20 else "(%d,%d)-(%d,%d)" % b
    print("#%-4d %-6s pass %-6s tgt %-8s %-4s n %-5d blend %s %s/%s at %s z %s/%s fog %s t%s %s..%s %s" % (
        r["i"], r["path"], r["pas"], r["tgt"], r["how"][:8], r["n"], r.get("blend"), r.get("sb"), r.get("db"),
        r.get("at"), r.get("z"), r.get("zw"), r.get("fog"), r.get("ft"), r.get("fs"), r.get("fe"), bx))
