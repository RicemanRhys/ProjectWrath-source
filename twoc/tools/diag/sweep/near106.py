"""near.py INTERP.BIN SWAP [SWAP...] -- step 106: per draw of the frames, the triangles that reach the eye plane: some
corner with clip w < WMAX (env, default 0.5) and some other corner in front (w > 0). Prints the draw (number, program
or FVF, mesh, texture, prim, states) and each such triangle's corners (w, z, and x/w, y/w where w > 0)."""
import os, sys
import numpy as np
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import interp_recio as rio
import rollpix as rp
np.set_printoptions(precision=3, suppress=True, linewidth=200)
WMAX = float(os.environ.get("WMAX", 0.5))
want = set(int(s) for s in sys.argv[2:])
for fr in rio.read(sys.argv[1]):
    if not fr["full"] or fr["swap"] not in want:
        continue
    n = 0
    for c in fr["cmds"]:
        if c["kind"] != 0:
            continue
        n += 1
        f = c["full"]
        if f["target"]["data"]:
            continue
        V = rp.clip_of(c)
        if V is None:
            continue
        rows = []
        for t in rp.triangles(f):
            if len(set(t)) < 3:
                continue
            w = V[list(t), 3]
            if w.min() < WMAX and w.max() > 0:
                rows.append((t, V[list(t)]))
        if not rows:
            continue
        rs = f["rs"]
        what = ("program %08X" % c["vkey"]) if f["n_insns"] else ("FVF %X" % f["fvf"])
        print("swap %d draw %d %s mesh %08X tex0 %08X prim %d nv %d; blend %s z %s write %s cull %s: %d triangles" % (
            fr["swap"], n, what, c["mesh"], f["tex"][0]["key"], f["prim"], f["nv"], rs.get(27), rs.get(7), rs.get(14),
            rs.get(22), len(rows)))
        for t, P in rows[:6]:
            sx = ["(%.2f,%.2f)" % (p[0] / p[3], p[1] / p[3]) if p[3] > 0 else "behind" for p in P]
            print("    tri %s w %s z %s screen %s" % (list(map(int, t)), P[:, 3].round(4), P[:, 2].round(4), sx))
