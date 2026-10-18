"""aoset.py TAG [X0 Y0 X1 Y1 SCALE] -- the last sub-frame dump of each dumped swap of runs/TAG (in order: the
modes aocmp.sh switched through), side by side as runs/TAG/set.png (or the crop as crop.png)."""
import os, re, sys, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
d = os.path.join(HERE, 'runs', sys.argv[1])
by = {}
for f in os.listdir(d):
    m = re.match(r'hostsub(\d+)_(\d+)of(\d+)\.bmp$', f)
    if m:
        sw = int(m.group(1))
        if sw not in by or int(m.group(2)) > by[sw][0]:
            by[sw] = (int(m.group(2)), f)
groups, last = [], None
for sw in sorted(by):                   # a trigger read late dumps several swaps in a row: the first of each run
    if last is None or sw - last > 20:
        groups.append(by[sw][1])
    last = sw
args = sys.argv[2:7] if len(sys.argv) > 6 else ['0', '0', '1920', '1080', '0.5']
out = os.path.join(d, 'crop.png' if len(sys.argv) > 6 else 'set.png')
subprocess.run([sys.executable, os.path.join(HERE, 'cmp.py'), out] + args + [os.path.join(d, g) for g in groups])
