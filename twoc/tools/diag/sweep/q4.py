"""q4.py LOG... -- step 102 part 2: the program draws with 4 vertices (the decal bias's draws) per log: pass, vs, blend, z."""
import re, sys, collections
for log in sys.argv[1:]:
    recs, cur = [], None
    for raw in open(log, 'rb'):
        l = raw.decode('latin1').rstrip()
        m = re.match(r'\[D3DB\] drawlog swap (\d+) #\d+ (\S+) prim (\d+) n (\d+) fvf (\S+) mirrored: vertex program.*?tex0 (\S+) \(fmt (\S+), (\S+),.*?pass (\S+);', l)
        if m:
            cur = dict(sw=m.group(1), prim=m.group(3), n=m.group(4), vs=m.group(5), tex=m.group(8), fmt=m.group(7), pas=m.group(9), t='')
            recs.append(cur); continue
        if l.startswith('[D3DB] drawlog swap'):
            cur = None; continue
        if cur is not None and l.startswith('[D3DB] drawlog   '):
            cur['t'] += l + '\n'
    c = collections.Counter()
    for r in recs:
        v = re.search(r'inputs over (\d+) vertices', r['t'])
        if not v or v.group(1) != '4':
            continue
        b = re.search(r'blend (\S+) (\S+) (\S+) .*?atest (\S+) \S+ ref \S+ z (\S+)', r['t'])
        c[(r['pas'], r['vs'], r['prim'], r['n'], r['fmt'], r['tex'], b.groups() if b else None)] += 1
    nsw = len(set(r['sw'] for r in recs)) or 1
    print('== %s: %d four-vertex program draws a swap' % (log.split('/')[-1], sum(c.values()) // nsw))
    for k, n in c.most_common(8):
        print('   %3d pass %s vs %s prim %s n %s fmt %s %s blend/atest/z %s' % (n, *k))
