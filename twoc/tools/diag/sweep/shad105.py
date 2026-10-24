"""evalshad.py MESH.npz REC [W H] -- the rigid flattened shadow draws (8E116565 with a singular c96-c99) of every
recorded frame, matched in order to the dumped meshes (8E116565.N, N from 481 by draw-log number): each triangle's
clip positions (float64): the smallest w, NDC extent; prints triangles with w < 0.5, any |ndc| > 3, or area over
2% of the screen, and a per-frame summary. Also the viewport-space z (z/w) range."""
import sys
import numpy as np
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import interp_recio as rio
mz = np.load(sys.argv[1])
keys = sorted([k for k in mz.files if k.startswith("8E116565.")], key=lambda s: int(s.split(".")[1]))
keys = [k for k in keys if int(sys.argv[3]) <= int(k.split(".")[1]) <= int(sys.argv[4])] if len(sys.argv) > 3 else keys
fr = rio.read(sys.argv[2])
import os
def shadows(f):
    out = []
    for c in f["cmds"]:
        if c["kind"] == 0 and c["vkey"] == 0x8E116565:
            M = c["consts"][96:100].T
            sv = np.linalg.svd(M, compute_uv=False)
            if sv[-1] / sv[0] < 1e-5:
                out.append(c)
    return out
bymesh = {}
for g in rio.read(os.environ.get("ALIGN", sys.argv[2]))[:3]:
    sh = shadows(g)
    if len(sh) == len(keys) and all(len(mz[k]) == 3 * c["nprim"] for k, c in zip(keys, sh)):
        bymesh = {c["mesh"]: mz[k] for k, c in zip(keys, sh)}
        break
print("meshes known: %d" % len(bymesh))
for f in fr:
    sh = []
    for c in f["cmds"]:
        if c["kind"] != 0 or c["vkey"] != 0x8E116565:
            continue
        M = c["consts"][96:100].T
        sv = np.linalg.svd(M, compute_uv=False)
        if sv[-1] / sv[0] < 1e-5:
            sh.append(c)
    worst = (1e9, None)
    bad = 0
    zr = [1e9, -1e9]
    for j, c in enumerate(sh):
        v = bymesh.get(c["mesh"])
        if v is None or len(v) != 3 * c["nprim"]:
            continue
        M = c["consts"][96:100].T
        clip = v @ M
        w = clip[:, 3]
        ndc = clip[:, :3] / w[:, None]
        zr = [min(zr[0], ndc[:, 2].min()), max(zr[1], ndc[:, 2].max())]
        for t in range(len(v) // 3):
            tw = w[3 * t:3 * t + 3]
            tn = ndc[3 * t:3 * t + 3]
            e1 = tn[1, :2] - tn[0, :2]; e2 = tn[2, :2] - tn[0, :2]; a = abs(e1[0] * e2[1] - e1[1] * e2[0]) / 2 / 4    # share of the screen
            if tw.min() < worst[0]:
                worst = (tw.min(), (j, t))
            if tw.min() < 0.5 or abs(tn[:, :2]).max() > 3 or a > 0.02:
                bad += 1
                if bad <= 6:
                    print("swap %d shadow %d (#%d mesh %08X) tri %d: w %s ndc %s area %.3f" % (
                        f["swap"], j, c["idx"], c["mesh"], t, np.round(tw, 3), np.round(tn[:, :2], 2).tolist(), a))
    print("swap %d: %d shadow draws, smallest w %.3f at %s, %d odd triangles, z/w %.4f..%.4f" % (
        f["swap"], len(sh), worst[0], worst[1], bad, zr[0], zr[1]))
