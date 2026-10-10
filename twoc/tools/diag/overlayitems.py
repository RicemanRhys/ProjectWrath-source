"""overlayitems.py ERRLOG -- step 65: each logged swap's overlay-pass draws by the code that queued them (the queue chain's
callers past the text and model helpers), with the union of their screen boxes (program draws have none in the log)."""
import re, sys, collections
lines = open(sys.argv[1], 'rb').read().decode('latin1').splitlines()
rx = re.compile(r'drawlog swap (\d+) #(\d+) (\w+) .*fvf ([0-9A-F]+) ([^;]*);.*pass ([0-9A-F]+); box \(([-\d.e+]+), ([-\d.e+]+)\)-\(([-\d.e+]+), ([-\d.e+]+)\)')
ri = re.compile(r'item ([0-9A-F]+) type (\d+)(?: queued from (.*))?')
HELP = {'9D2A1', '9E1D3', '9E1AA', '83369', '35F91', 'AF6F2', '5E723', 'A6219', 'A63C0', '91429', 'AF8DF', '5EF46', '39DF0', '895D7', '95395', 'B642F', '5E610', 'FF9AD', 'F9DF5', 'F9BD5', '8965A'}
cur = None
groups = collections.OrderedDict()
for k, l in enumerate(lines):
    m = rx.search(l)
    if m:
        cur = m
        continue
    mi = ri.search(l)
    if mi and cur and cur.group(6) == '8967E':
        ch = (mi.group(3) or '').split(' < ')
        own = [c for c in ch if c and c not in HELP]
        key = (cur.group(1), ' < '.join(own[:2]) if own else '?')
        b = [float(cur.group(i)) for i in range(7, 11)]
        g = groups.setdefault(key, [0, [1e30, 1e30, -1e30, -1e30], set()])
        g[0] += 1
        if b[0] < 1e29:
            g[1] = [min(g[1][0], b[0]), min(g[1][1], b[1]), max(g[1][2], b[2]), max(g[1][3], b[3])]
        g[2].add(cur.group(3) + (' vp' if 'program' in cur.group(5) else ''))
        cur = None
for (s, o), (n, b, kinds) in groups.items():
    bx = '(%.0f,%.0f)-(%.0f,%.0f)' % tuple(b) if b[0] < 1e29 else '(no box)'
    print('swap %s  %-22s %3d draws  %-26s %s' % (s, o, n, bx, ','.join(sorted(kinds))))
