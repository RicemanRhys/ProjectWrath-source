"""fs90sum.py DIR [PREFIX...] -- step 90: per group of fullscreen starts (TAG_NN.err.log, TAG = V_RT_MODE_rN),
the starts with pace's 3 misses, how long frames stopped (misses to the next frame shown, from the
recovery lines), recoveries by step, the longest Present block (watchdog), and the swap chain remade."""
import glob, os, re, sys
from collections import defaultdict

d = sys.argv[1]
pre = sys.argv[2:] or ['']
g = defaultdict(lambda: defaultdict(int))
for f in sorted(glob.glob(os.path.join(d, '*.err.log'))):
    tag = os.path.basename(f)[:-8]
    if not any(tag.startswith(p) for p in pre):
        continue
    key = re.sub(r'_r\d+_\d+$', '', tag)
    t = open(f, encoding='utf-8', errors='replace').read()
    s = g[key]
    s['starts'] += 1
    s['misses'] += 'released no frame' in t
    s['watched_ok'] += 'no recovery needed' in t
    for k in (1, 2, 3):
        s['step%d' % k] += len(re.findall(r'display recovery \d+: .*step %d' % k, t)) and 1
    s['gaveup'] += 'left to the player' in t
    s['remade'] += 'swap chain made again after' in t
    s['remake_fail'] += 'not made again' in t
    s['watchdog'] += 'render thread watchdog: the' in t
    s['fs'] += 'borderless fullscreen,' in t
    s['ended0'] += 'ended with code 0' in t
    s['killed'] += 0
for k in sorted(g):
    s = g[k]
    print('%-18s starts %2d  3-misses %2d  (frames again by themselves %2d)  step1 %d step2 %d step3 %d gave-up %d'
          '  remade %d (failed %d)  watchdog %d  fullscreen %d  code0 %d' % (
              k, s['starts'], s['misses'], s['watched_ok'], s['step1'], s['step2'], s['step3'], s['gaveup'],
              s['remade'], s['remake_fail'], s['watchdog'], s['fs'], s['ended0']))
