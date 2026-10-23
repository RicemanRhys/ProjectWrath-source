"""recio.py -- read RECOMP_D3D_BRIDGE_INTERP_REC files (d3d_host.c ic_rec_dump)."""
import struct
import numpy as np


def read(path):
    b = open(path, "rb").read()
    o = 0
    frames = []
    while o < len(b):
        magic, swap, n, nc = struct.unpack_from("<4I", b, o); o += 16
        assert magic == 0x46524543, hex(magic)
        cams = []
        for i in range(nc):
            m32 = np.frombuffer(b, np.float32, 32, o).copy(); o += 128
            draws, fl = struct.unpack_from("<2I", b, o); o += 8
            cams.append(dict(m32=m32, m=m32.astype(np.float64), draws=draws, flags=fl))
        cmds = []
        for i in range(n):
            r = struct.unpack_from("<12I", b, o); o += 48
            if r[0] != 0:
                cmds.append(dict(kind=r[0], idx=i))
                continue
            f32 = np.frombuffer(b, np.float32, 32 + 48 + 768, o).copy(); o += 4 * (32 + 48 + 768)
            cm = struct.unpack_from("<6I", b, o); o += 24
            f = f32.astype(np.float64)
            cmds.append(dict(kind=0, idx=i, cls=r[1], cam=r[2], ncpos=r[3], target=r[4], tw=r[5] & 0xFFFF,
                             th=r[5] >> 16, fvf=r[6], vkey=r[7], flags=r[8], nv=r[9], nprim=r[10], mesh=r[11],
                             camera=f[:32], world32=f32[32:48].reshape(4, 4), world=f[32:48].reshape(4, 4),
                             view=f[48:64].reshape(4, 4), proj=f[64:80].reshape(4, 4),
                             consts32=f32[80:].reshape(192, 4), consts=f[80:].reshape(192, 4), cmask=cm))
        frames.append(dict(swap=swap, cams=cams, cmds=cmds))
    return frames
