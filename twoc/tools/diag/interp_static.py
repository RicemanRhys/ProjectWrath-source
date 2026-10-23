"""interp_static.py REC [--from SWAP] [--to SWAP] [--detail]

For each pair of consecutive recorded frames, replicates d3d_host.c's object
path for program (IC_VP) objects: W = WVP P^-1 Cam, pairing by key and
nearest W origin, w_move, W(t) = aff_lerp, and the composed matrix
W(t) W^-1 WVP C(t) against the camera-only WVP C(t). Reports, for objects
whose W didn't move (within noise), how far a point moves on screen
(pixels at 3840x2160) in sub-frames t = 1/4, 1/2, 3/4.
"""
import sys, math, collections
import numpy as np
sys.path.insert(0, __file__.rsplit("\\", 1)[0])
import interp_recio as recio

W_PX, H_PX = 3840, 2160


def q_from_m3(m):
    m = m.reshape(9)
    tr = m[0] + m[4] + m[8]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        return np.array([(m[7] - m[5]) / s, (m[2] - m[6]) / s, (m[3] - m[1]) / s, 0.25 * s])
    if m[0] > m[4] and m[0] > m[8]:
        s = math.sqrt(1.0 + m[0] - m[4] - m[8]) * 2
        return np.array([0.25 * s, (m[1] + m[3]) / s, (m[2] + m[6]) / s, (m[7] - m[5]) / s])
    if m[4] > m[8]:
        s = math.sqrt(1.0 + m[4] - m[0] - m[8]) * 2
        return np.array([(m[1] + m[3]) / s, 0.25 * s, (m[5] + m[7]) / s, (m[2] - m[6]) / s])
    s = math.sqrt(1.0 + m[8] - m[0] - m[4]) * 2
    return np.array([(m[2] + m[6]) / s, (m[5] + m[7]) / s, 0.25 * s, (m[3] - m[1]) / s])


def m3_from_q(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def slerp(qa, qb, t):
    qa = qa.copy()
    dot = qa @ qb
    if dot < 0:
        qa = -qa; dot = -dot
    if dot > 0.9995:
        sa, sb = 1 - t, t
    else:
        th = math.acos(dot)
        sa = math.sin((1 - t) * th) / math.sin(th); sb = math.sin(t * th) / math.sin(th)
    q = sa * qa + sb * qb
    return q / np.linalg.norm(q)


def aff_rot(r):
    l = np.linalg.norm(r, axis=1)
    if (l < 1e-6).any():
        return None
    rn = r / l[:, None]
    for i in range(3):
        for j in range(i + 1, 3):
            if abs(rn[i] @ rn[j]) > 1e-2:
                return None
    if np.linalg.det(rn) < 0.98:
        return None
    return q_from_m3(rn), l


def aff_lerp(wa, wb, t):
    ra, rb = wa[:3, :3], wb[:3, :3]
    A, B = aff_rot(ra), aff_rot(rb)
    o = np.eye(4)
    if A is not None and B is not None:
        q = slerp(A[0], B[0], t)
        r = m3_from_q(q)
        o[:3, :3] = r * (A[1] + (B[1] - A[1]) * t)[:, None]
    else:
        o[:3, :3] = ra + (rb - ra) * t
    o[3, :3] = wa[3, :3] + (wb[3, :3] - wa[3, :3]) * t
    return o


def hand(m):
    det = np.linalg.det(m[:3, :3]); l = np.prod(np.linalg.norm(m[:3, :3], axis=1))
    return 1 if det > 1e-3 * l else -1 if det < -1e-3 * l else 0


def cam_q(m):
    c = m[:16].reshape(4, 4)
    l = np.linalg.norm(c[:3, :3], axis=1)
    rn = c[:3, :3] / l[:, None]
    return q_from_m3(rn), l


def cam_sub(a, b, t):
    """g_isub's c (P_b^-1 Cam_b Cam(t)^-1 P(t)) for cameras a (before) and b."""
    A, B = a[:16].reshape(4, 4), b[:16].reshape(4, 4)
    qa, la = cam_q(a); qb, lb = cam_q(b)
    q = slerp(qa, qb, t); r = m3_from_q(q)
    ci = np.zeros((4, 4))
    for j in range(3):
        l = la[j] + (lb[j] - la[j]) * t
        ci[j, :3] = r[j] * l
        ci[j, 3] = B[j, 3]
    ci[3, :3] = A[3, :3] + (B[3, :3] - A[3, :3]) * t
    ci[3, 3] = B[3, 3]
    pi = a[16:].reshape(4, 4) + (b[16:].reshape(4, 4) - a[16:].reshape(4, 4)) * t
    vfix = B @ np.linalg.inv(ci)
    pb_inv = np.linalg.inv(b[16:].reshape(4, 4))
    return pb_inv @ vfix @ pi


def objects(fr):
    mc = max(range(len(fr["cams"])), key=lambda i: fr["cams"][i]["draws"])
    out = []
    for d in fr["cmds"]:
        if d["kind"] or d["cls"] != 1 or d["cam"] != mc or not d["mesh"]:
            continue
        cam = d["camera"]
        C = cam[:16].reshape(4, 4); P = cam[16:].reshape(4, 4)
        wvp = d["consts"][96:100].T.copy()
        W = wvp @ np.linalg.inv(P) @ C
        key = (d["vkey"], d["mesh"], d["nv"], d["nprim"], d["target"], hand(W))
        out.append(dict(d=d, W=W, wvp=wvp, key=key, pos=W[3, :3]))
    return mc, out


def pair(ca, cb):
    by = collections.defaultdict(list)
    for j, o in enumerate(ca):
        by[o["key"]].append(j)
    cand = []
    for i, o in enumerate(cb):
        for j in by.get(o["key"], []):
            cand.append((float(np.sum((o["pos"] - ca[j]["pos"]) ** 2)), i, j))
    cand.sort()
    pi, pj = {}, set()
    for dd, i, j in cand:
        if i in pi or j in pj:
            continue
        pi[i] = j; pj.add(j)
    return pi


def clip(x, M):
    return x @ M


def px(c):
    return np.array([c[0] / c[3] * W_PX / 2, -c[1] / c[3] * H_PX / 2])


def aff_lerp_exact(wa, wb, t):
    """aff_lerp, plus each end's residual (what the rebuilt matrix misses of
    the recorded one) blended linearly: exact at t = 0 and t = 1."""
    return aff_lerp(wa, wb, t) + (1 - t) * (wa - aff_lerp(wa, wb, 0.0)) + t * (wb - aff_lerp(wa, wb, 1.0))


CAPS = {16098: "Gold Rush 1", 16542: "Gold Rush 2", 16761: "Gold Rush 3", 17563: "Gold Rush 4",
        28040: "Medieval 1", 31420: "Medieval 2"}


def cap_of(swap):
    k = max([c for c in CAPS if c <= swap], default=None)
    return CAPS.get(k, "swap %d" % swap) if k is not None and swap - k < 40 else "swaps from %d" % swap


variants = ("cur", "exact")


def collect(path):
    frames = recio.read(path)
    rows = []
    for fa, fb in zip(frames, frames[1:]):
        if fb["swap"] != fa["swap"] + 1:
            continue
        ma, oa = objects(fa)
        mb, ob = objects(fb)
        ca_m = fa["cams"][ma]["m"]; cb_m = fb["cams"][mb]["m"]
        if np.array_equal(fa["cams"][ma]["m32"], fb["cams"][mb]["m32"]):
            continue                    # camera didn't move
        pi = pair(oa, ob)
        Cs = {t: cam_sub(ca_m, cb_m, t) for t in (0.25, 0.5, 0.75)}
        campos = cb_m[12:15]
        fw = cb_m[8:11] / np.linalg.norm(cb_m[8:11])
        for i, o in enumerate(ob):
            if i not in pi:
                continue
            p = oa[pi[i]]
            Wb, Wa = o["W"], p["W"]
            wmove = bool((np.abs(Wb - Wa) > 1e-5 * (1.0 + np.abs(Wb))).any())
            dt = float(np.linalg.norm(Wb[3, :3] - Wa[3, :3]))
            dr = float(np.abs(Wb[:3, :3] - Wa[:3, :3]).max())
            static = dt < 1e-2 and dr < 1e-3
            Winv = np.linalg.inv(Wb)
            wI = bool(np.abs(Wb[:3, :3] - np.eye(3)).max() < 1e-3 and np.abs(Wb[3, :3]).max() < 0.05)
            pts = [np.array([0, 0, 0, 1.0])] if not wI else []
            for dist in (5.0, 20.0):
                pts.append(np.append(campos + dist * fw, 1.0) @ Winv)
            worst = {v: 0.0 for v in variants}
            for t, C in Cs.items():
                M0 = o["wvp"] @ C
                Ms = {"cur": (aff_lerp(Wa, Wb, t) @ Winv @ o["wvp"]) @ C if wmove else M0,
                      "exact": (aff_lerp_exact(Wa, Wb, t) @ Winv @ o["wvp"]) @ C if wmove else M0}
                for x in pts:
                    c0 = clip(x, M0)
                    if c0[3] <= 0.1:
                        continue
                    for v in variants:
                        s = float(np.linalg.norm(px(clip(x, Ms[v])) - px(c0)))
                        worst[v] = max(worst[v], s)
            rows.append(dict(cap=cap_of(fb["swap"]), swap=fb["swap"], vkey=o["d"]["vkey"], mesh=o["d"]["mesh"],
                             nv=o["d"]["nv"], wmove=wmove, dt=dt, dr=dr, static=static, sh=worst,
                             orig=float(np.linalg.norm(o["pos"])), campos=float(np.linalg.norm(campos)),
                             camd=float(np.linalg.norm(o["pos"] - campos)), w33=float(Wb[3, 3]),
                             wt=float(np.linalg.norm(Wb[3, :3])) if wI else -1, wI=wI))
    return rows


def main():
    rows = collect(sys.argv[1])
    detail = "--detail" in sys.argv
    if not rows:
        print("no rows"); return
    st = [r for r in rows if r["static"]]
    print("program objects paired %d, static (W within 1e-2 units / 1e-3) %d, of them w_move true %d" %
          (len(rows), len(st), sum(r["wmove"] for r in st)))
    print("worst sub-frame shift on screen, px at 3840x2160 (static objects; the camera-only result is the truth):")
    print("  %-14s %6s %8s %21s %21s %10s %9s" % ("capture", "static", "w_move", "now: median/90%/max", "exact ends: med/max",
                                                    "cam |pos|", "W=I err"))
    for cap in dict.fromkeys(r["cap"] for r in rows):
        v = [r for r in st if r["cap"] == cap]
        if not v:
            continue
        a = np.array([r["sh"]["cur"] for r in v]); b = np.array([r["sh"]["exact"] for r in v])
        e = [abs(r["w33"] - 1) for r in v if r["wI"]]
        tt = [r["wt"] for r in v if r["wI"]]
        print("  %-14s %6d %7.1f%% %6.2f %6.2f %7.2f %10.4f %10.4f %10.1f  w33 %.1e t %.4f" %
              (cap, len(v), 100.0 * sum(r["wmove"] for r in v) / len(v), np.median(a), np.percentile(a, 90), a.max(),
               np.median(b), b.max(), np.median([r["campos"] for r in v]), max(e) if e else 0, max(tt) if tt else 0))
    mv = [r for r in rows if not r["static"]]
    if mv:
        d = np.array([abs(r["sh"]["cur"] - r["sh"]["exact"]) for r in mv])
        print("moving objects %d: |now - exact ends| px median %.3f, max %.3f" % (len(mv), np.median(d), d.max()))
    by = collections.defaultdict(list)
    for r in st:
        by[(r["vkey"], r["wI"])].append(r)
    print("by program (static): n, w_move share, now median / max px, exact ends max, |W origin| median, camera distance")
    for k, v in sorted(by.items(), key=lambda kv: -max(r["sh"]["cur"] for r in kv[1])):
        s = [r["sh"]["cur"] for r in v]
        print("  %08X %-5s n %5d wmove %5.1f%% now %7.3f / %7.3f exact %7.4f  |W origin| %.0f  cam %.1f" %
              (k[0], "W=I" if k[1] else "", len(v), 100.0 * sum(r["wmove"] for r in v) / len(v), np.median(s), max(s),
               max(r["sh"]["exact"] for r in v), np.median([r["orig"] for r in v]), np.median([r["camd"] for r in v])))
    if detail:
        for r in sorted(st, key=lambda r: -r["sh"]["cur"])[:20]:
            print(r)


if __name__ == "__main__":
    main()
