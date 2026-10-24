"""ext.py REC -- step 106: per recorded frame: draws, Crash's bone-0 origin (8D225D6D c124-126 col 3), speed (units/s
at 60 Hz from the frame before), the eye and view direction from the fixed-function draws' view matrix."""
import sys
import numpy as np
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import interp_recio as rio
np.set_printoptions(precision=2, suppress=True)
prev = None
fvfs = {}
for f in rio.read(sys.argv[1]):
    pos = None; eye = None; fwd = None
    nd = 0
    for c in f["cmds"]:
        if c["kind"] != 0: continue
        nd += 1
        fvfs[c["fvf"]] = fvfs.get(c["fvf"], 0) + 1
        if pos is None and c["vkey"] == 0x8D225D6D:
            pos = c["consts"][124:127][:, 3].copy()
        if eye is None and c["vkey"] == 0 and abs(np.linalg.det(c["view"])) > 1e-6 and c["view"][3, 3] == 1:
            iv = np.linalg.inv(c["view"]); eye = iv[3, :3]; fwd = iv[2, :3]
    sp = np.linalg.norm(pos - prev) * 60 if (pos is not None and prev is not None) else float("nan")
    print(f["swap"], nd, "crash", pos, "speed %.1f" % sp, "eye", eye, "fwd", fwd)
    prev = pos
print({hex(k): v for k, v in sorted(fvfs.items(), key=lambda x: -x[1])[:10]})
