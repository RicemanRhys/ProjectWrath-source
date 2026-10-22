"""recls.py REC.bin [VERTS] -- step 100 item 5: an INTERP_REC file's frames: per frame the draws by class (0 none, the
camera's classes, ...) and, for draws with VERTS vertices (default 4: the decals), each one's class, camera, the
camera-position registers it holds, its program and c96-c99 (WVP) row 3, so decals and walls can be compared."""
import collections, struct, sys
import numpy as np
b = open(sys.argv[1], "rb").read()
want = int(sys.argv[2]) if len(sys.argv) > 2 else 4
o = 0
while o + 16 <= len(b):
    magic, swap, n, ncam = struct.unpack_from("<4I", b, o)
    if magic != 0x46524543:
        print("bad magic at", o); break
    o += 16 + ncam * (128 + 8)
    cls = collections.Counter()
    rows = []
    for i in range(n):
        r = struct.unpack_from("<12I", b, o); o += 48
        if r[0] != 0:                    # not a draw
            continue
        cam = np.frombuffer(b, np.float32, 32, o); o += 128
        o += 64 * 3                      # world, view, proj
        k = np.frombuffer(b, np.float32, 768, o).reshape(192, 4); o += 3072
        o += 24
        cls[r[1]] += 1
        if r[9] == want and r[7]:
            rows.append((i, r[1], r[2], r[3], r[7], r[11], k[96:100].T.copy()))
    print("swap %d: %d commands, %d cameras, draws by class %s" % (swap, n, ncam, dict(sorted(cls.items()))))
    for i, c, cm, ncp, vp, mesh, wvp in rows[:12]:
        print("  #%d cls %d cam %d cpos %d prog %08X mesh %08X  WVP row3 %s" % (i, c, cm, ncp, vp, mesh, np.array2string(wvp[3], precision=3)))
