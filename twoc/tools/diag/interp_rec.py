"""interp_rec.py FILE -- RECOMP_D3D_BRIDGE_INTERP_REC frames: cameras, classes, and why draws weren't classed."""
import collections, struct, sys
import numpy as np

np.set_printoptions(precision=4, suppress=True, linewidth=160)
b = open(sys.argv[1], "rb").read()
o = 0
frames = []
while o < len(b):
    magic, swap, n, nc = struct.unpack_from("<4I", b, o); o += 16
    assert magic == 0x46524543, hex(magic)
    cams = []
    for i in range(nc):
        m = np.frombuffer(b, np.float32, 32, o).astype(np.float64); o += 128
        draws, fl = struct.unpack_from("<2I", b, o); o += 8
        cams.append((m, draws, fl))
    cmds = []
    for i in range(n):
        r = struct.unpack_from("<12I", b, o); o += 48
        if r[0] != 0:
            cmds.append(dict(kind=r[0]))
            continue
        f = np.frombuffer(b, np.float32, 32 + 48 + 768, o).astype(np.float64); o += 4 * (32 + 48 + 768)
        cm = struct.unpack_from("<6I", b, o); o += 24
        cmds.append(dict(kind=0, cls=r[1], cam=r[2], ncpos=r[3], target=r[4], tw=r[5] & 0xFFFF, th=r[5] >> 16,
                         fvf=r[6], vkey=r[7], flags=r[8], nv=r[9], camera=f[:32], world=f[32:48].reshape(4, 4),
                         view=f[48:64].reshape(4, 4), proj=f[64:80].reshape(4, 4), consts=f[80:].reshape(192, 4),
                         cmask=cm))
    frames.append((swap, cams, cmds))

for swap, cams, cmds in frames:
    print("=" * 100)
    print("swap %d: %d commands, %d cameras" % (swap, len(cmds), len(cams)))
    for i, (m, draws, fl) in enumerate(cams):
        print("  cam %d: draws %d flags %d pos %s" % (i, draws, fl, m[12:15]))
    kinds = collections.Counter()
    for d in cmds:
        if d["kind"]:
            kinds["cmd kind %d" % d["kind"]] += 1
            continue
        fl = d["flags"]
        what = "vp" if fl & 2 else "ff" if fl & 1 else "rhw"
        rhw = (d["fvf"] & 0xE) == 4
        if fl & 1 and rhw:
            what = "ff-rhw"
        kinds["%s cls %d tgt %s" % (what, d["cls"], "bb" if not d["target"] else "%dx%d" % (d["tw"], d["th"]))] += 1
    for k, v in sorted(kinds.items()):
        print("   %5d  %s" % (v, k))
    # why were vp / ff draws into the back buffer unclassed?
    shown = 0
    for i, d in enumerate(cmds):
        if d["kind"] or d["cls"] or d["target"]:
            continue
        fl = d["flags"]
        cam = d["camera"]
        C = cam[:16].reshape(4, 4); P = cam[16:].reshape(4, 4)
        if fl & 2:
            m = d["consts"][96:100]
            wvp = m.T
            try:
                W = wvp @ np.linalg.inv(P) @ C
            except np.linalg.LinAlgError:
                W = None
            if shown < 6:
                print("  unclassed vp draw %d key %08X nv %d cmask3 %08X: W col3 %s" % (i, d["vkey"], d["nv"], d["cmask"][3],
                      None if W is None else W[:, 3]))
                shown += 1
        elif fl & 1 and (d["fvf"] & 0xE) != 4:
            vc = d["view"] @ C
            if shown < 12:
                print("  unclassed ff draw %d fvf %X: |VC-I| max %.4g; view row3 %s cam pos %s" % (i, d["fvf"], np.abs(vc - np.eye(4)).max(), d["view"][3], C[3]))
                shown += 1
