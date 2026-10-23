"""dsub2.py DIR [OUT.png] -- step 102 part 2 (dchk.sh, Crash standing): per batch (bias 0, then on), each sub-frame
against its own t = 1 frame: pixels differing (any channel > 24). With the camera still, the bias-0 batch's count is
the scene's own animation; what the bias-on batch adds is the bias's doing. OUT: the bias-on batch's largest sub-frame
difference in red over its t = 1 frame, beside the bias-0 batch's largest."""
import os, re, sys
import numpy as np
from PIL import Image

d = sys.argv[1]
fs = sorted(f for f in os.listdir(d) if f.startswith('hostsub'))
sw = sorted(set(int(re.match(r'hostsub(\d+)_', f).group(1)) for f in fs))
groups, cur = [], [sw[0]]
for s in sw[1:]:
    if s - cur[-1] > 30:
        groups.append(cur)
        cur = [s]
    else:
        cur.append(s)
groups.append(cur)
if len(groups) != 2:
    print('%s: swaps %s: expected 2 batches, got %d' % (d, sw, len(groups)))
    sys.exit(0)


def img(s, k):
    return np.asarray(Image.open(os.path.join(d, 'hostsub%05d_%dof4.bmp' % (s, k))).convert('RGB')).astype(int)


out = []
for g, name in zip(groups, ('bias 0', 'bias on')):
    counts, best = [], (-1, None)
    for s in g:
        t1 = img(s, 4)
        for k in (1, 2, 3):
            m = (np.abs(img(s, k) - t1) > 24).any(axis=2)
            counts.append(int(m.sum()))
            if m.sum() > best[0]:
                o = t1.copy().astype(np.uint8)
                o[m] = (255, 0, 0)
                best = (int(m.sum()), o)
    print('  %s: sub-frames against their t = 1: %s px' % (name, counts))
    out.append(best[1])
if len(sys.argv) > 2:
    a, b = (Image.fromarray(x) for x in out)
    a.thumbnail((960, 540))
    b.thumbnail((960, 540))
    w = Image.new('RGB', (a.width + b.width, a.height))
    w.paste(a, (0, 0))
    w.paste(b, (a.width, 0))
    w.save(sys.argv[2])
