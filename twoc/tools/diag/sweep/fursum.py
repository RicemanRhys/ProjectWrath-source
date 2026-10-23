"""fursum.py LOG... -- step 102: the fur-shell draws (drawn from 0x9FF2A) in a run's draw logs: per palette
signature, the index counts, items, positions (itemx), programs; plus the player's position if logged."""
import re, sys, collections
for log in sys.argv[1:]:
    recs, cur = [], None
    for raw in open(log, 'rb'):
        line = raw.decode('latin1').rstrip()
        m = re.match(r'\[D3DB\] drawlog swap (\d+) #(\d+) (\S+) prim \d+ n (\d+) fvf (\S+).*?tex0 (\S+).*?from ([^;]*);', line)
        if m:
            cur = dict(swap=int(m.group(1)), n=int(m.group(4)), vs=m.group(5), tex=m.group(6), frm=m.group(7), lines=[])
            recs.append(cur)
            continue
        if line.startswith('[D3DB] drawlog swap'):
            cur = None
            continue
        if cur is not None and line.startswith('[D3DB] drawlog   '):
            cur['lines'].append(line)
    fur = [r for r in recs if '9FF2A' in r['frm'].split(' < ')[:5]]
    swaps = sorted(set(r['swap'] for r in recs))
    print('== %s: %d draws logged in %d swaps, %d fur-shell draws' % (log.split('/')[-1], len(recs), len(swaps), len(fur)))
    by = collections.OrderedDict()
    for r in fur:
        t = '\n'.join(r['lines'])
        pal = (re.search(r'palette \S+ data \S+ sig (\S+)', t) or [None, '-'])[1]
        at = re.search(r'at \((\S+) (\S+) (\S+)\)', t)
        at = tuple(round(float(x), 1) for x in at.groups()) if at else None
        mat = (re.search(r' mat (\S+)', t) or [None, '-'])[1]
        k = pal
        e = by.setdefault(k, dict(n=collections.Counter(), at=collections.Counter(), vs=collections.Counter(), mat=collections.Counter(), swaps=set()))
        e['n'][r['n']] += 1
        e['at'][at] += 1
        e['vs'][r['vs']] += 1
        e['mat'][mat] += 1
        e['swaps'].add(r['swap'])
    for k, e in by.items():
        print('  palette %s: draws %d in %d swaps; indices %s; vs %s; at %s; mat %s' % (
            k, sum(e['n'].values()), len(e['swaps']), dict(e['n']), ','.join(e['vs']), dict(e['at'].most_common(4)),
            ','.join(list(e['mat'])[:4])))
