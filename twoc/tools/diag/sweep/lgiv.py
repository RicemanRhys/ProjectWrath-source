"""lgiv.py LOG... -- step 89 (notes 18.92), for a Legion Go zip's logs: per run, the launcher, settings, events and one row per 10-s interval
(level, guest swaps/s, presents, shown-at-1, game side / replay side, replays ms, rt waits, baked, power)."""
import re, sys

EV = re.compile(r'host (swap chain|frame rate plan|picture|device \d|interpolation (on|off)|render thread: (on|off) from|'
                r'interpolation: [\d.]+ fps, |: the display released|: vsync pacing again|baked draws: o)|watchdog|\[OVL\]|\[BAT\]|'
                r'\[VK\]|CRASH\]|THREADCHECK|RECOMP_FRAMERATE|ack thread|\[XPP\] Start|\[CFG\] (settings written|render|framerate|full|adapt)|'
                r'\[DEV\] main menu|present fails [1-9]|\[SAVE\]|\[FILES\]|startup message|overlay ready')


def run(p):
    lines = open(p, encoding='utf-8', errors='replace').read().splitlines()
    rows, cur, level, events = [], None, '-', []
    print('=' * 100)
    print(p)
    for i, l in enumerate(lines, 1):
        m = re.search(r'Crashdat\\levels\\\w\\(\w+)\\\1\.ter', l)
        if m and '[PATH]' in l and m.group(1) != level:
            level = m.group(1)
            events.append('%d: LOAD %s' % (i, level))
        if EV.search(l) and not l.startswith('[D3DB] host +'):
            events.append('%d: %s' % (i, l[:260]))
        m = re.match(r'\[D3DB\] host \+([\d.]+) s: guest swaps (\d+) \(\+(\d+), ([\d.]+)/s\), host presents (\d+) \(\+(\d+)\)', l)
        if m:
            if cur:
                rows.append(cur)
            cur = dict(t=float(m.group(1)), gs=float(m.group(4)), pr=int(m.group(6)), lvl=level, ev=events)
            events = []
            continue
        if cur is None:
            continue
        m = re.search(r'host power: (.*)', l)
        if m:
            cur['pw'] = m.group(1).replace('plugged in, battery ', 'AC ').replace('on battery, battery ', 'BAT ')
        m = re.search(r'swap gaps this interval: over 25 ms (\d+), over 50 ms (\d+), over 100 ms (\d+), longest ([\d.]+)', l)
        if m:
            cur['gaps'] = '%s/%s/%s %s' % m.groups()
        m = re.search(r'frames shown this interval, by refreshes since the last shown: 1 (\d+), 2 (\d+), 3 (\d+), 4 (\d+), 5 (\d+), 6 (\d+), 7 (\d+), more (\d+); unmeasured (\d+)', l)
        if m:
            g = list(map(int, m.groups()))
            cur['shown'] = '1:%d 2:%d 3:%d 4+:%d um%d' % (g[0], g[1], g[2], sum(g[3:8]), g[8])
        m = re.search(r'replays ([\d.]+) ms a frame', l)
        if m:
            cur['rep'] = float(m.group(1))
        m = re.search(r'([\d.]+) us a draw to record', l)
        if m:
            cur['recus'] = float(m.group(1))
        m = re.search(r'draw calls this interval: (\d+), ([\d.]+) ms \(([\d.]+) us a draw', l)
        if m:
            cur['dc'] = '%s@%s' % (m.group(1), m.group(3))
        m = re.search(r'mirror time this interval: draws ([\d.]+) ms', l)
        if m:
            cur['mdraw'] = float(m.group(1))
        m = re.search(r'baked draws: (\d+) captured at first passes, (\d+) drawn from their capture, (\d+) drawn the ordinary way', l)
        if m:
            cur['bk'] = '%s/%s/%s' % m.groups()
        m = re.search(r'game frames allowed ([^;]*); changes (\d+); measured frames (\d+), ([\d.]+) ms apart, ([\d.]+) ms of it busy(?: \(the game side ([\d.]+), the replay side ([\d.]+)\))?', l)
        if m:
            cur['allow'] = ' '.join('%s:%s' % (a.split('.')[0], b) for a, b in re.findall(r'([\d.]+) fps: (\d+)', m.group(1)) if b != '0')
            cur['chg'] = int(m.group(2)); cur['apart'] = float(m.group(4)); cur['busy'] = float(m.group(5))
            cur['gside'] = m.group(6); cur['rside'] = m.group(7)
        m = re.search(r'interpolation: ([\d.]+) frames each game frame; frames interpolated (\d+) \(draws matched ([\d.]+)%', l)
        if m:
            cur['k'] = m.group(1); cur['match'] = m.group(3)
        m = re.search(r'host render thread: (on|off); frames replayed on it (\d+), the game waited ([\d.]+) ms a frame in its Swap, the render thread ([\d.]+) ms', l)
        if m:
            cur['rt'] = '%s gw%s rw%s' % (m.group(1), m.group(3), m.group(4))
        m = re.search(r'limiter: paced \d+ \(\+\d+\), late \d+ \(\+(\d+)\), waited (\d+) ms', l)
        if m:
            cur['lim'] = 'late%s w%s' % m.groups()
        m = re.search(r'\[APU-OUT\].*?(\d+) underruns', l)
        if m and m.group(1) != '0':
            cur['ur'] = m.group(1)
    if cur:
        rows.append(cur)
    for r in rows:
        for e in r['ev']:
            print('   ', e)
        print('%6.1f %-9s gs%5.1f pr%5d %-12s %-30s k%-5s m%-5s rep%-5s busy %-5s g%-5s r%-5s %-14s %-22s rec%-4s dc%-12s bk %-22s %s %s' % (
            r['t'], r['lvl'], r['gs'], r['pr'], r.get('gaps', ''), r.get('shown', ''), r.get('k', ''), r.get('match', ''),
            r.get('rep', ''), r.get('busy', ''), r.get('gside', ''), r.get('rside', ''), r.get('allow', ''), r.get('rt', ''),
            r.get('recus', ''), r.get('dc', ''), r.get('bk', ''), r.get('pw', ''), ('UR' + r['ur']) if 'ur' in r else ''))
    for e in events:
        print('   ', e)


for p in sys.argv[1:]:
    run(p)
