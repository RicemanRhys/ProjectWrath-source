"""ms92sum.py TAG... -- step 92 item 5: a ms92.sh run's 10-s intervals grouped by the ambient occlusion in effect
for the whole interval (from the level's load on): guest swaps/s, frames shown a second, shown at 1 refresh (% of
measured), at another gap than presented, swap gaps over 25 ms, busy / game side / replay side ms, the render
thread's waits, the adaptive rates allowed; and the [AO] lines' GPU ms per pass."""
import re, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))


def summ(tag):
    lines = open(os.path.join(HERE, 'runs', tag + '.err.log'), encoding='utf-8', errors='replace').read().splitlines()
    mode, changed, loaded, seen = 'off', True, False, False
    rows, cur, gpu = [], None, {}
    for l in lines:
        if '[PATH]' in l and re.search(r'levels\\\w\\(\w+)\\\1\.ter', l):
            loaded = True
        m = re.search(r'host ambient occlusion: (\w+),', l)
        if m:
            mode, changed = m.group(1), True
            seen = seen or mode != 'off'
            continue
        m = re.match(r'\[D3DB\] host \+([\d.]+) s: guest swaps \d+ \(\+\d+, ([\d.]+)/s\), host presents \d+ \(\+(\d+)\)', l)
        if m:
            if cur and not cur['bad']:
                rows.append(cur)
            cur = dict(mode=mode, bad=changed or not loaded or (mode == 'off' and not seen), gs=float(m.group(2)), pr=int(m.group(3)) / 10.0)
            changed = False
            continue
        m = re.match(r'\[AO\] (\w+), .*GPU ms a pass \((\d+) timed\): depth ([\d.]+), prefilter ([\d.]+), occlusion ([\d.]+), denoise ([\d.]+), apply ([\d.]+), all ([\d.]+)', l)
        m3 = re.match(r'\[AO\] \w+, strength \d+%, radius [\d.]+, (\w+) size', l)
        if m and int(m.group(2)) > 20:
            gpu.setdefault(m.group(1) + ' ' + m3.group(1), []).append([float(x) for x in m.groups()[2:]])
        m2 = re.match(r'\[AO\] \w+, strength \d+%, radius [\d.]+, (\w+) size (\d+x\d+)', l)
        if m2:
            size = m2.group(1) + ' ' + m2.group(2)
        if cur is None:
            continue
        m = re.search(r'frames shown this interval, by refreshes since the last shown: 1 (\d+), 2 (\d+), 3 (\d+), 4 (\d+), 5 (\d+), 6 (\d+), 7 (\d+), more (\d+); unmeasured (\d+); at another gap than presented (\d+)', l)
        if m:
            g = list(map(int, m.groups()))
            cur['shown1'] = g[0]; cur['meas'] = sum(g[:8]); cur['um'] = g[8]; cur['off'] = g[9]
        m = re.search(r'swap gaps this interval: over 25 ms (\d+)', l)
        if m:
            cur['gaps'] = int(m.group(1))
        m = re.search(r'measured frames (\d+), ([\d.]+) ms apart, ([\d.]+) ms of it busy(?: \(the game side ([\d.]+), the replay side ([\d.]+)\))?', l)
        if m:
            cur['busy'] = float(m.group(3)); cur['g'] = float(m.group(4) or 0); cur['r'] = float(m.group(5) or 0)
        m = re.search(r'game frames allowed ([^;]*);', l)
        if m:
            cur['allow'] = ' '.join('%s:%s' % (a.split('.')[0], b) for a, b in re.findall(r'([\d.]+) fps: (\d+)', m.group(1)) if b != '0')
        m = re.search(r'host render thread: (on|off); frames replayed on it (\d+), the game waited ([\d.]+) ms a frame in its Swap, the render thread ([\d.]+) ms', l)
        if m:
            cur['gw'] = float(m.group(3)); cur['rw'] = float(m.group(4))
    if cur and not cur['bad']:
        rows.append(cur)
    print('== %s' % tag)
    for md in ('off', 'gtao', 'ssao'):
        rs = [r for r in rows if r['mode'] == md and 'busy' in r]
        if not rs:
            continue
        n = len(rs)
        av = lambda k: sum(r.get(k, 0) for r in rs) / n
        meas = sum(r.get('meas', 0) for r in rs)
        s1 = sum(r.get('shown1', 0) for r in rs)
        print('  %-4s n%d  updates %.1f/s  presents %.1f/s  shown at 1 refresh %.1f%% (of %d measured, %d unmeasured), '
              'another gap %d, gaps>25ms %d  busy %.2f (game %.2f, replay %.2f)  waits game %.2f render %.2f  allowed %s'
              % (md, n, av('gs'), av('pr'), 100.0 * s1 / meas if meas else 0, meas, sum(r.get('um', 0) for r in rs),
                 sum(r.get('off', 0) for r in rs), sum(r.get('gaps', 0) for r in rs), av('busy'), av('g'), av('r'),
                 av('gw'), av('rw'), ' | '.join(r.get('allow', '') for r in rs)))
    for md, ls in gpu.items():
        a = [sum(x[i] for x in ls) / len(ls) for i in range(6)]
        print('  GPU %-9s (%d lines): depth %.3f prefilter %.3f occlusion %.3f denoise %.3f apply %.3f = %.3f ms a pass'
              % (md, len(ls), *a))


for t in sys.argv[1:]:
    summ(t)
