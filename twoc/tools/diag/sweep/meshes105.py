"""meshes.py ERRLOG OUT.npz -- from a RECOMP_DEV_VSDUMP_N draw-log run: each dumped program draw's vertices in
index order (v0 xyzw for every index), keyed 'HASH.N', with its index count; saved for evalshad.py."""
import re, sys
import numpy as np
d = {}
cnt = {}
for ln in open(sys.argv[1], encoding="utf-8", errors="replace"):
    m = re.match(r"\[D3DB\] vsdump ([0-9A-F]{8}\.\d+) vertex (\d+) index (\d+): v0=([^ ]+)", ln)
    if m:
        d.setdefault(m.group(1), []).append([float(x) for x in m.group(4).split(",")])
np.savez(sys.argv[2], **{k: np.array(v) for k, v in d.items()})
for k in sorted(d, key=lambda s: int(s.split(".")[1])):
    print(k, len(d[k]))
