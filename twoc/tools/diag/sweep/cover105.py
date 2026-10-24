"""cover.py ERRLOG SWAP X Y [R] -- every dumped program draw of guest swap SWAP (RECOMP_DEV_VSDUMP_N run): its
triangles through the NV2A program (vscheck's Vm), back to clip space through c58/c59, clipped as D3D11 does
(0 <= z <= w), projected to the 640x480 viewport; prints the draws and triangles that cover the point (X, Y) or
come within R units of it (default 0.5), with the depth (z/w) there, vertex w's and alphas, and the draw's line."""
import re, sys
import numpy as np
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import vscheck as vc
log, SW, X, Y = sys.argv[1], int(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])
R = float(sys.argv[5]) if len(sys.argv) > 5 else 0.5
lines = {}
for ln in open(log, encoding="utf-8", errors="replace"):
    m = re.search(r"drawlog swap (\d+) #(\d+) (\S+) prim (\d+) n (\d+)", ln)
    if m and int(m.group(1)) == SW:
        lines[int(m.group(2))] = (int(m.group(4)), ln.strip()[:160])
progs, _ = vc.parse(log)


def clip_poly(poly, plane):
    out = []
    for i in range(len(poly)):
        a, b = poly[i], poly[(i + 1) % len(poly)]
        da, db = plane(a), plane(b)
        if da >= 0:
            out.append(a)
        if (da >= 0) != (db >= 0):
            t = da / (da - db)
            out.append(a + t * (b - a))
    return out


def inside(px, py, scr):
    # point in convex polygon (either winding), with margin R
    n = len(scr)
    s = 0
    for i in range(n):
        x0, y0 = scr[i]
        x1, y1 = scr[(i + 1) % n]
        c = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0)
        L = np.hypot(x1 - x0, y1 - y0) or 1.0
        d = c / L
        if d > R:
            s |= 1
        elif d < -R:
            s |= 2
        if s == 3:
            return False
    return True


keys = []
for k, p in progs.items():
    if "." in k and ("swap %d " % SW) in p.get("head", ""):
        keys.append((int(k.split(".")[1]), k))
keys.sort()
print("program draws dumped in swap %d: %d" % (SW, len(keys)))
for n, k in keys:
    p = progs[k]
    prog = [vc.decode(p["code"][i:i + 4]) for i in range(0, len(p["code"]), 4)]
    c = p["const"].reshape(192, 4).astype(np.float64)
    sc = np.where(c[58, :3] != 0, c[58, :3], 1.0)
    V = []
    for v in p["verts"]:
        o = vc.Vm(p["const"]).run(prog, v)
        op = o["oPos"].astype(np.float64)
        w = op[3]
        V.append(np.concatenate([(op[:3] - c[59, :3]) / sc * w, [w, o["oD0"][3]]]))
    V = np.array(V)
    pr = lines.get(n, (5, ""))[0]
    tris = [(i, i + 1, i + 2) for i in range(len(V) - 2)] if pr == 6 else [(i, i + 1, i + 2) for i in range(0, len(V) - 2, 3)]
    hits = []
    for t in tris:
        poly = [V[i] for i in t]
        for pl in (lambda q: q[2], lambda q: q[3] - q[2]):
            poly = clip_poly(poly, pl)
            if not poly:
                break
        if len(poly) < 3:
            continue
        scr = [((q[0] / q[3] + 1) * 320, (1 - q[1] / q[3]) * 240) for q in poly]
        if inside(X, Y, scr):
            hits.append((t, [round(V[i][3], 2) for i in t], [round(V[i][4], 2) for i in t],
                         [(round(x), round(y)) for x, y in scr]))
    if hits:
        print("#%d %s (%d triangles here): %s" % (n, k, len(hits), lines.get(n, (0, ""))[1][:150]))
        for h in hits[:6]:
            print("     tri %s w %s alpha %s screen %s" % h)
