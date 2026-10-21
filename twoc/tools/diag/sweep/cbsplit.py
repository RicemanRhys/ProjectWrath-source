"""cbsplit.py LOG... -- step 98 (notes 18.101), for a package zip's logs: per run, the means over cast_bug's 10-s intervals at full rate of
the game thread (g), its mirror draws (md, recording rec inside), the rest (own: the game's code and wrappers), its wait in Swap (gw), the
render thread (r), replays, draws, D3D11 calls, Present, pairing, the pace wait, the all-core MHz and the temperature; first / last 3
intervals and correlations with temperature, clock and time."""
import re, sys, statistics as st
for f in sys.argv[1:]:
    L = open(f, encoding='utf-8', errors='replace').read().splitlines()
    level = '-'; rows = []; cur = None
    for l in L:
        m = re.search(r'Crashdat\\levels\\\w\\(\w+)\\\1\.ter', l)
        if m and '[PATH]' in l: level = m.group(1)
        m = re.match(r'\[D3DB\] host \+([\d.]+) s: guest swaps \d+ \(\+(\d+), ([\d.]+)/s\)', l)
        if m:
            if cur: rows.append(cur)
            cur = dict(t=float(m.group(1)), n=int(m.group(2)), lvl=level); continue
        if cur is None: continue
        m = re.search(r'mirror time this interval: draws ([\d.]+) ms \(immediate [\d.]+ ms\), swaps ([\d.]+) ms', l)
        if m: cur['md'] = float(m.group(1)) / cur['n']; cur['sw'] = float(m.group(2)) / cur['n']
        m = re.search(r'([\d.]+) ms of it busy \(the game side ([\d.]+), the replay side ([\d.]+)\)', l)
        if m: cur['g'] = float(m.group(2)); cur['r'] = float(m.group(3))
        m = re.search(r'the game waited ([\d.]+) ms a frame in its Swap, the render thread ([\d.]+) ms', l)
        if m: cur['gw'] = float(m.group(1))
        m = re.search(r'([\d.]+) us a draw to record \(([\d.]+) ms a frame\), replays ([\d.]+) ms', l)
        if m: cur['rec'] = float(m.group(2)); cur['rep'] = float(m.group(3))
        m = re.search(r'the draws ([\d.]+) ms a game frame, ([\d.]+) ms of it in D3D11 calls \((\d+)%\); Present ([\d.]+) ms', l)
        if m: cur['dr'] = float(m.group(1)); cur['d3d'] = float(m.group(2)); cur['pres'] = float(m.group(4))
        m = re.search(r'pairing ([\d.]+) ms a frame', l)
        if m: cur['pair'] = float(m.group(1))
        m = re.search(r'CPU (\d+) MHz on average', l)
        if m and 'host power' in l: cur['mhz'] = int(m.group(1))
        m = re.search(r'; ([\d.]+) C \(', l)
        if m and 'host power' in l: cur['C'] = float(m.group(1))
        m = re.search(r'waited ([\d.]+) ms this interval \(([\d.]+) ms a frame\)', l)
        if m and 'host limiter' in l: cur['pace'] = float(m.group(2))
    if cur: rows.append(cur)
    cb = [r for r in rows if r['lvl'] == 'cast_bug' and r['n'] > 590 and 'g' in r and 'md' in r]
    print(f, len(cb), 'cast_bug intervals at full rate')
    keys = ['g', 'md', 'rec', 'gw', 'r', 'rep', 'dr', 'd3d', 'pres', 'pair', 'pace', 'mhz', 'C']
    for k in keys:
        v = [r[k] for r in cb if k in r]
        if v: print('  %-5s mean %8.2f  min %8.2f  max %8.2f' % (k, st.mean(v), min(v), max(v)))
    v = [r['g'] - r['md'] for r in cb]
    print('  own   mean %8.2f  min %8.2f  max %8.2f   (game side less mirror draws)' % (st.mean(v), min(v), max(v)))
    if len(cb) > 6:
        a, b = cb[:3], cb[-3:]
        print('  first 3: g %.2f r %.2f C %.0f MHz %.0f | last 3: g %.2f r %.2f C %.0f MHz %.0f' % (
            st.mean(x['g'] for x in a), st.mean(x['r'] for x in a), st.mean(x['C'] for x in a), st.mean(x['mhz'] for x in a),
            st.mean(x['g'] for x in b), st.mean(x['r'] for x in b), st.mean(x['C'] for x in b), st.mean(x['mhz'] for x in b)))
        try:
            print('  corr(g, C) %.2f  corr(g, MHz) %.2f  corr(r, C) %.2f  corr(g, t) %.2f' % (
                st.correlation([x['g'] for x in cb], [x['C'] for x in cb]),
                st.correlation([x['g'] for x in cb], [x['mhz'] for x in cb]),
                st.correlation([x['r'] for x in cb], [x['C'] for x in cb]),
                st.correlation([x['g'] for x in cb], [x['t'] for x in cb])))
        except Exception as e: print(e)
