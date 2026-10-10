"""interp_joints.py FILE SWAP... [-v] -- step 66 (notes 18.69): on RECOMP_D3D_BRIDGE_INTERP_REC frames, how far apart the
joints of skinned meshes come in interpolated sub-frames. Each skinned program draw (bones from c124) is paired with the
frame before's as d3d_host.c pairs it (kind + mesh + handedness, nearest by all bones); two of its bones share a joint
where one bind-space point maps to one place under both, in both frames (least squares). Per swap: the largest bone
turn, and the worst gap at t = 1/4, 1/2, 3/4 between the two bones' places for the joint, bones slerped one by one
(`cur`, before step 66) and blended in their skeleton's frame G (`skel`, step 66: skinned objects holding one identical
moving bone, G the bone most of them hold). A torn limb is a gap of a tenth of a unit or more; skel stays at the
solver's noise (~0.002). Slow (seconds a swap). -v lists the draws and bones with cur gaps over 0.05."""
import struct, sys
import numpy as np

SKINNED = (0xA98754DF, 0x8D225D6D, 0xA4AE6206)


def load(path):
    b = open(path, "rb").read(); o = 0; frames = []
    while o < len(b):
        magic, swap, n, nc = struct.unpack_from("<4I", b, o); o += 16
        o += nc * 136
        cmds = []
        for i in range(n):
            r = struct.unpack_from("<12I", b, o); o += 48
            if r[0] != 0:
                continue
            f = np.frombuffer(b, np.float32, 32 + 48 + 768, o).astype(np.float64); o += 4 * (32 + 48 + 768)
            cm = struct.unpack_from("<6I", b, o); o += 24
            cmds.append(dict(i=i, cls=r[1], target=r[4], vkey=r[7], mesh=r[11], camera=f[:32],
                             consts=f[80:].reshape(192, 4), cmask=cm))
        frames.append((swap, cmds))
    return frames


def reads(cm, r):
    return cm[r >> 5] >> (r & 31) & 1


def hand(m):
    det = np.linalg.det(m); l = np.prod(np.linalg.norm(m, axis=1))
    return 1 if det > 1e-3 * l else -1 if det < -1e-3 * l else 0


def world(d):
    C = d["camera"][:16].reshape(4, 4); P = d["camera"][16:].reshape(4, 4)
    return d["consts"][96:100].T @ np.linalg.inv(P) @ C


def key(d):
    b0 = np.array([d["consts"][124 + i // 3, i % 3] for i in range(9)]).reshape(3, 3)
    return (d["vkey"], d["mesh"], hand(world(d)[:3, :3]), hand(b0))


def bmask(d):
    return [not any(reads(d["cmask"], 124 + 3 * k + j) for j in range(3)) for k in range(22)]


def dist(o, p):
    dd = np.sum((world(o)[3, :3] - world(p)[3, :3]) ** 2); m = [a and b for a, b in zip(bmask(o), bmask(p))]
    bs = [np.sum((o["consts"][124 + 3 * k:127 + 3 * k, 3] - p["consts"][124 + 3 * k:127 + 3 * k, 3]) ** 2)
          for k in range(22) if m[k]]
    return dd + (np.mean(bs) if bs else 0)


def skinned(cmds):
    return [d for d in cmds if d["cls"] == 1 and d["vkey"] in SKINNED and not d["target"]]


def qm(m):
    tr = np.trace(m)
    if tr > 0:
        s = np.sqrt(tr + 1) * 2
        return np.array([(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, s / 4])
    i = np.argmax(np.diag(m)); j, k = (i + 1) % 3, (i + 2) % 3
    s = np.sqrt(1 + m[i, i] - m[j, j] - m[k, k]) * 2; q = np.zeros(4)
    q[i] = s / 4; q[j] = (m[j, i] + m[i, j]) / s; q[k] = (m[k, i] + m[i, k]) / s; q[3] = (m[k, j] - m[j, k]) / s
    return q


def mq(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def slerp(a, b, t):
    d = a @ b
    if d < 0:
        a = -a; d = -d
    if d > .9995:
        q = a + (b - a) * t
    else:
        th = np.arccos(d); q = (np.sin((1 - t) * th) * a + np.sin(t * th) * b) / np.sin(th)
    return q / np.linalg.norm(q)


def m4(c):
    m = np.eye(4); m[:3] = c; return m


def split(M):                           # axes are columns, scaled
    L = np.linalg.norm(M[:3, :3], axis=0); R = M[:3, :3] / np.maximum(L, 1e-12)
    return R, L, np.abs(R.T @ R - np.eye(3)).max() < 1e-2 and np.linalg.det(R) > .98


def cur(Ma, Mb, t):                     # d3d_host.c's aff_lerp on a bone
    Ra, La, oa = split(Ma); Rb, Lb, ob = split(Mb); M = np.eye(4)
    if oa and ob:
        M[:3, :3] = mq(slerp(qm(Ra), qm(Rb), t)) * (La + (Lb - La) * t)
    else:
        M[:3, :3] = Ma[:3, :3] + (Mb[:3, :3] - Ma[:3, :3]) * t
    M[:3, 3] = Ma[:3, 3] + (Mb[:3, 3] - Ma[:3, 3]) * t
    return M


def turn(Ma, Mb):
    Ra = split(Ma)[0]; Rb = split(Mb)[0]
    return np.degrees(np.arccos(np.clip((np.trace(Ra.T @ Rb) - 1) / 2, -1, 1)))


def joints(d, p):
    bm = bmask(d); mats = {}; out = []
    for b in range(22):
        if not bm[b]:
            continue
        Ma = m4(p["consts"][124 + 3 * b:127 + 3 * b]); Mb = m4(d["consts"][124 + 3 * b:127 + 3 * b])
        if split(Ma)[2] and split(Mb)[2]:
            mats[b] = (Ma, Mb)
    ks = sorted(mats)
    for x in range(len(ks)):
        for y in range(x + 1, len(ks)):
            (Ai, Bi), (Aj, Bj) = mats[ks[x]], mats[ks[y]]
            if np.allclose(Ai, Aj) and np.allclose(Bi, Bj):
                continue
            D = np.vstack([(Ai - Aj)[:3], (Bi - Bj)[:3]])
            e = np.linalg.lstsq(D[:, :3], -D[:, 3], rcond=None)[0]
            if np.abs(D[:, :3] @ e + D[:, 3]).max() < 2e-3 and np.linalg.norm(e) < 50:
                out.append((ks[x], ks[y], np.append(e, 1)))
    return mats, out


def main():
    fr = load(sys.argv[1]); want = [int(a) for a in sys.argv[2:] if not a.startswith("-")]
    verbose = "-v" in sys.argv
    idx = {s: k for k, (s, _) in enumerate(fr)}
    for s in want:
        if s not in idx or s - 1 not in idx:
            print("swap %d: not recorded with the one before" % s); continue
        A = skinned(fr[idx[s - 1]][1]); B = skinned(fr[idx[s]][1]); used = set(); objs = []
        for d in B:
            k = key(d); c = [p for p in A if key(p) == k and p["i"] not in used]
            if not c:
                continue
            p = min(c, key=lambda p: dist(d, p)); used.add(p["i"])
            mats, js = joints(d, p)
            objs.append((d, mats, js))
        par = list(range(len(objs)))
        def find(x):
            while par[x] != x:
                par[x] = par[par[x]]; x = par[x]
            return x
        seen = {}
        for oi, (d, mats, js) in enumerate(objs):
            for b, (Ma, Mb) in mats.items():
                if np.allclose(Ma, Mb):
                    continue
                h = Mb[:3].astype(np.float32).tobytes()
                if h in seen:
                    par[find(oi)] = find(seen[h])
                else:
                    seen[h] = oi
        groups = {}
        for oi in range(len(objs)):
            groups.setdefault(find(oi), []).append(oi)
        wc = ws = mx = 0.0; nj = 0
        for mem in groups.values():
            cnt = {}
            for oi in mem:
                for b, (Ma, Mb) in objs[oi][1].items():
                    if np.allclose(Ma, Mb):
                        continue
                    h = Mb[:3].astype(np.float32).tobytes(); cnt.setdefault(h, [0, Ma, Mb]); cnt[h][0] += 1
            G = max(cnt.values(), key=lambda v: v[0])[1:] if cnt else None
            for oi in mem:
                d, mats, js = objs[oi]
                for Ma, Mb in mats.values():
                    mx = max(mx, turn(Ma, Mb))
                for i, j, e in js:
                    nj += 1
                    for t in (.25, .5, .75):
                        g = np.linalg.norm(cur(*mats[i], t)[:3] @ e - cur(*mats[j], t)[:3] @ e)
                        wc = max(wc, g)
                        if verbose and g > .05 and t == .5:
                            print("   #%d mesh %08X b%d-b%d cur %.3f" % (d["i"], d["mesh"], i, j, g))
                        if G is not None:
                            Gt = cur(G[0], G[1], t); iGa = np.linalg.inv(G[0]); iGb = np.linalg.inv(G[1])
                            sk = lambda Ma, Mb: Gt @ ((1 - t) * (iGa @ Ma) + t * (iGb @ Mb))
                            ws = max(ws, np.linalg.norm(sk(*mats[i])[:3] @ e - sk(*mats[j])[:3] @ e))
        print("swap %d: %d joints, largest bone turn %.0f deg, worst joint gap: cur %.3f skel %.3f" % (s, nj, mx, wc, ws))


if __name__ == "__main__":
    main()
