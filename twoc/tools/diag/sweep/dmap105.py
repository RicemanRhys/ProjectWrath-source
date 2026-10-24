"""dmap.py INTERP.BIN SWAP X0 Y0 X1 Y1 STEP -- over a box of the 480x270 picture: the band (draw 530) minus the
wall (draw 94) depth in D24 steps where both cover the point (exact arithmetic from the recorded screen z), as
rows of numbers ('.' where one is missing)."""
import sys
import numpy as np
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import interp_recio as rio
import rollpix as rp
fr = next(f for f in rio.read(sys.argv[1]) if f["swap"] == int(sys.argv[2]))
x0, y0, x1, y1, st = map(float, sys.argv[3:8])
draws = [c for c in fr["cmds"] if c["kind"] == 0]
want = {94: None, 530: None}
for n in want:
    c = draws[n - 1]
    V = rp.clip_of(c)
    tris = []
    for t in rp.triangles(c["full"]):
        if len(set(t)) == 3:
            tris.append([V[i] for i in t])
    want[n] = tris


def depth(tris, nx, ny):
    best = None
    for poly in tris:
        p = poly
        for pl in (lambda q: q[2], lambda q: q[3] - q[2]):
            p = rp.clip_poly(p, pl)
            if not p:
                break
        if len(p) < 3:
            continue
        ins, z = rp.cover(p, nx, ny, 0.0)
        if ins and (best is None or z < best):
            best = z
    return best


for y in np.arange(y0, y1 + 1e-9, st):
    row = []
    for x in np.arange(x0, x1 + 1e-9, st):
        nx, ny = x / 480 * 2 - 1, 1 - y / 270 * 2
        a, b = depth(want[530], nx, ny), depth(want[94], nx, ny)
        row.append("%4d" % round((a - b) * 16777215) if a is not None and b is not None else "   .")
    print("y %6.2f " % y + "".join(row))
