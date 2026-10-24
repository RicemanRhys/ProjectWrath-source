"""rollpix.py INTERP.BIN SWAP X Y [--pic W H] [--all] -- step 105: which draws of one game frame of a full rolling
capture (RECOMP_D3D_BRIDGE_ROLLING_FULL=1, kit launcher 7) cover a point of the picture, in draw order, with
their depth there: the offline bisection of a glitch seen in the capture's frames.

X Y are in the capture's picture (480x270 by default; --pic W H for another size, e.g. a 1920x1080 grab of the
same 16:9 picture). Each draw is taken back to clip space as the host draws it: a vertex program run on its
recorded inputs and constants (the NV2A rules of vscheck.py, vectorised here), back through c58/c59 as xbox_d3d8
does; a fixed-function draw through its recorded world, view and projection (pre-transformed ones as they are).
Triangles are clipped as D3D11 clips (0 <= z <= w) and tested against the point. Printed per covering triangle:
the draw's number in the frame, program or FVF, mesh, its blend / depth / stencil states, the corners' w and the
depth (z/w) at the point; the last opaque depth-writing draw there and each blended draw after it are what made
the pixel. --all also lists the draws that come within 2 pixels.

SWAP: the guest swap of the frame (the picture's sSWAP in its name), or "last"."""
import struct, sys
import numpy as np
import interp_recio as rio
import vscheck as vc

F = np.float32
RS = dict(ZENABLE=7, ZWRITEENABLE=14, SRCBLEND=19, DESTBLEND=20, CULLMODE=22, ZFUNC=23, ALPHABLENDENABLE=27,
          STENCILENABLE=52, STENCILFUNC=56)


def run_prog(code, consts, regs):
    """All vertices through the NV2A program at once: regs is {register: (n, 4) float32}; returns oPos (n, 4)."""
    prog = [vc.decode(code[i:i + 4]) for i in range(0, len(code), 4)]
    n = next(iter(regs.values())).shape[0] if regs else 1
    c = np.array(consts, dtype=F).reshape(192, 4)
    R = np.zeros((13, n, 4), dtype=F)
    R[12, :, 3] = 1
    a0 = np.zeros(n, dtype=np.int64)
    one = np.zeros((n, 4), dtype=F); one[:, 3] = 1
    with np.errstate(all="ignore"):
        for d in prog:
            def rd(i):
                s = d["src"][i]
                if s["mux"] == 1:
                    v = R[s["r"]]
                elif s["mux"] == 2:
                    v = regs.get(d["v"], one)
                elif s["mux"] == 3:
                    k = d["const"] + (a0 if d["a0x"] else 0)
                    k = np.broadcast_to(k, (n,))
                    ok = (k >= 0) & (k < 192)
                    v = np.where(ok[:, None], c[np.clip(k, 0, 191)], 0).astype(F)
                else:
                    v = np.zeros((n, 4), dtype=F)
                v = v[:, list(s["swz"])]
                return -v if s["neg"] else v

            def mul(a, b):
                r = a * b
                r[(a == 0) | (b == 0)] = 0
                return r
            mac = vc.MAC[d["mac"]] if d["mac"] < len(vc.MAC) else "NOP"
            ilu = vc.ILU[d["ilu"]]
            mres = ires = None
            if mac != "NOP":
                a = rd(0)
                b = rd(1) if 1 in vc.mac_srcs(mac) else None
                cc = rd(2) if 2 in vc.mac_srcs(mac) else None
                if mac == "MOV": mres = a
                elif mac == "MUL": mres = mul(a, b)
                elif mac == "ADD": mres = a + cc
                elif mac == "MAD": mres = mul(a, b) + cc
                elif mac == "DP3": mres = np.repeat(mul(a, b)[:, :3].sum(1, dtype=F)[:, None], 4, 1)
                elif mac == "DPH": mres = np.repeat((mul(a, b)[:, :3].sum(1, dtype=F) + b[:, 3])[:, None], 4, 1)
                elif mac == "DP4": mres = np.repeat(mul(a, b).sum(1, dtype=F)[:, None], 4, 1)
                elif mac == "DST": mres = np.stack([np.ones(n, F), a[:, 1] * b[:, 1], a[:, 2], b[:, 3]], 1)
                elif mac == "MIN": mres = np.fmin(a, b)
                elif mac == "MAX": mres = np.fmax(a, b)
                elif mac == "SLT": mres = (a < b).astype(F)
                elif mac == "SGE": mres = (a >= b).astype(F)
                elif mac == "ARL": a0 = np.floor(a[:, 0]).astype(np.int64)
            if ilu != "NOP":
                x = rd(2)[:, 0].astype(np.float64)
                if ilu == "MOV": ires = rd(2)
                elif ilu == "RCP": r = np.where(x != 0, 1.0 / np.where(x != 0, x, 1), np.inf); ires = np.repeat(r[:, None], 4, 1).astype(F)
                elif ilu == "RCC":
                    r = np.where(x != 0, 1.0 / np.where(x != 0, x, 1), 1.884467e19)
                    r = np.where(r > 0, np.clip(r, 5.42101e-20, 1.884467e19), np.clip(r, -1.884467e19, -5.42101e-20))
                    ires = np.repeat(r[:, None], 4, 1).astype(F)
                elif ilu == "RSQ":
                    ax = np.abs(x); r = np.where(ax != 0, 1.0 / np.sqrt(np.where(ax != 0, ax, 1)), np.inf)
                    ires = np.repeat(r[:, None], 4, 1).astype(F)
                elif ilu == "EXP":
                    fl = np.floor(x); ires = np.stack([2.0 ** fl, x - fl, 2.0 ** x, np.ones(n)], 1).astype(F)
                elif ilu == "LOG":
                    t = np.abs(x); e = np.floor(np.log2(np.where(t > 0, t, 1)))
                    ires = np.stack([np.where(t > 0, e, -np.inf), np.where(t > 0, t / 2.0 ** e, 1),
                                     np.where(t > 0, np.log2(np.where(t > 0, t, 1)), -np.inf), np.ones(n)], 1).astype(F)
                elif ilu == "LIT":
                    s = rd(2); t = np.zeros((n, 4), F); t[:, 0] = 1; t[:, 3] = 1
                    p = s[:, 0] > 0; t[p, 1] = s[p, 0]
                    q = p & (s[:, 1] > 0); t[q, 2] = np.power(s[q, 1], s[q, 3])
                    ires = t
            if mres is not None and d["mac_mask"]:
                for k in range(4):
                    if d["mac_mask"] & (8 >> k):
                        R[d["r"]][:, k] = mres[:, k]
            if ires is not None and d["ilu_mask"]:
                rr = 1 if mac != "NOP" else d["r"]
                for k in range(4):
                    if d["ilu_mask"] & (8 >> k):
                        R[rr][:, k] = ires[:, k]
            if d["o_mask"] and d["orb"] != 0 and vc.OUTS.get(d["addr"] & 0xF) == "oPos":
                res = ires if d["omux"] == 1 else mres
                if res is not None:
                    for j in range(4):
                        if d["o_mask"] & (8 >> j):
                            R[12][:, j] = res[:, j]
            if d["final"]:
                break
    return R[12].astype(np.float64)


def clip_of(c):
    """A draw's vertices in clip space (n, 4), or None."""
    f = c["full"]
    if not f["nv"]:
        return None
    raw = np.frombuffer(f["verts"], np.uint8).reshape(f["nv"], f["stride"])
    if f["n_insns"]:
        regs = {}
        k = 0
        for r in range(16):
            if f["inputs"] >> r & 1:
                regs[r] = raw[:, 16 * k:16 * k + 16].copy().view(F).reshape(f["nv"], 4)
                k += 1
        op = run_prog(f["code"], c["consts32"].reshape(-1), regs)
        k = c["consts"]
        sc = np.where(k[58, :3] != 0, k[58, :3], 1.0)
        w = op[:, 3:4]
        return np.hstack([(op[:, :3] - k[59, :3]) / sc * w, w])
    pos = raw[:, :16].copy().view(F).reshape(f["nv"], 4).astype(np.float64)
    if (f["fvf"] & 0xE) == 0x4:          # XYZRHW: viewport pixels, as xbox_d3d8 takes them
        vx, vy, vw, vh = f["vp"][:4]
        rhw = np.where(pos[:, 3] != 0, pos[:, 3], 1.0)
        x = (pos[:, 0] - vx) / max(vw, 1) * 2 - 1
        y = 1 - (pos[:, 1] - vy) / max(vh, 1) * 2
        return np.stack([x, y, pos[:, 2], np.ones(len(x))], 1) / rhw[:, None]
    p = np.hstack([pos[:, :3], np.ones((f["nv"], 1))])
    return p @ c["world"] @ c["view"] @ c["proj"]


def triangles(f):
    idx = f["indices"] if f["indices"] is not None else np.arange(f["nv"])
    p = f["prim"]
    if p == 4:
        return [tuple(idx[i:i + 3]) for i in range(0, len(idx) - 2, 3)]
    if p == 5:
        return [tuple(idx[i:i + 3]) for i in range(len(idx) - 2)]
    if p == 6:
        return [(idx[0], idx[i], idx[i + 1]) for i in range(1, len(idx) - 1)]
    return []


def clip_poly(poly, plane):
    out = []
    for i in range(len(poly)):
        a, b = poly[i], poly[(i + 1) % len(poly)]
        da, db = plane(a), plane(b)
        if da >= 0:
            out.append(a)
        if (da >= 0) != (db >= 0):
            out.append(a + da / (da - db) * (b - a))
    return out


def cover(poly, x, y, margin):
    """(inside, depth at (x, y)) for a convex clip-space polygon against NDC (x, y)."""
    s = [(q[0] / q[3], q[1] / q[3], q[2] / q[3]) for q in poly]
    sign = 0
    for i in range(len(s)):
        x0, y0 = s[i][:2]
        x1, y1 = s[(i + 1) % len(s)][:2]
        L = np.hypot(x1 - x0, y1 - y0) or 1.0
        d = ((x1 - x0) * (y - y0) - (y1 - y0) * (x - x0)) / L
        if abs(d) <= margin:
            continue
        if sign == 0:
            sign = 1 if d > 0 else -1
        elif (d > 0) != (sign > 0):
            return False, None
    a, b, c = np.array(s[0]), np.array(s[1]), np.array(s[2])
    m = np.array([[b[0] - a[0], c[0] - a[0]], [b[1] - a[1], c[1] - a[1]]])
    if abs(np.linalg.det(m)) < 1e-15:
        return True, a[2]
    u, v = np.linalg.solve(m, [x - a[0], y - a[1]])
    return True, a[2] + u * (b[2] - a[2]) + v * (c[2] - a[2])


def main():
    a = sys.argv[1:]
    pw, ph = 480, 270
    if "--pic" in a:
        i = a.index("--pic"); pw, ph = int(a[i + 1]), int(a[i + 2]); del a[i:i + 3]
    show_all = "--all" in a
    a = [t for t in a if t != "--all"]
    frames = [f for f in rio.read(a[0]) if f["full"]]
    if not frames:
        sys.exit("no full records in %s (RECOMP_D3D_BRIDGE_ROLLING_FULL=1)" % a[0])
    fr = frames[-1] if a[1] == "last" else next(f for f in frames if f["swap"] == int(a[1]))
    X, Y = float(a[2]), float(a[3])
    nx, ny = X / pw * 2 - 1, 1 - Y / ph * 2
    margin = 2.0 / pw * 2 if show_all else 0.0
    print("swap %d: %d commands; point (%g, %g) of a %dx%d picture = NDC (%.4f, %.4f)" % (
        fr["swap"], len(fr["cmds"]), X, Y, pw, ph, nx, ny))
    n_draw = 0
    for c in fr["cmds"]:
        if c["kind"] != 0:
            continue
        n_draw += 1
        f = c["full"]
        if f["target"]["data"]:
            continue                    # a render-to-texture pass, not the picture
        V = clip_of(c)
        if V is None:
            continue
        hits = []
        for t in triangles(f):
            if len(set(t)) < 3:
                continue
            poly = [V[i] for i in t]
            for pl in (lambda q: q[2], lambda q: q[3] - q[2]):
                poly = clip_poly(poly, pl)
                if not poly:
                    break
            if len(poly) < 3:
                continue
            inside, z = cover(poly, nx, ny, margin)
            if inside:
                hits.append((t, [round(V[i][3], 3) for i in t], z))
        if hits:
            rs = f["rs"]
            what = ("program %08X" % c["vkey"]) if f["n_insns"] else ("FVF %X" % f["fvf"])
            st = "blend %s %X/%X, z %s write %s func %s, stencil %s, cull %s" % (
                rs.get(RS["ALPHABLENDENABLE"]), rs.get(RS["SRCBLEND"], 0), rs.get(RS["DESTBLEND"], 0),
                rs.get(RS["ZENABLE"]), rs.get(RS["ZWRITEENABLE"]), rs.get(RS["ZFUNC"]), rs.get(RS["STENCILENABLE"]),
                rs.get(RS["CULLMODE"]))
            print("draw %4d (command %d) %s mesh %08X tex0 %08X; %s" % (n_draw, c["idx"], what, c["mesh"],
                  f["tex"][0]["key"], st))
            for t, w, z in hits[:4]:
                print("      triangle %s: w %s, depth here %.9f (D24 %d)" % (list(map(int, t)), w, z,
                      int(round(z * 16777215))))


if __name__ == "__main__":
    main()
