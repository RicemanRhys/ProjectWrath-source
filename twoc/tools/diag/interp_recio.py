"""recio.py -- read RECOMP_D3D_BRIDGE_INTERP_REC files (d3d_host.c ic_rec_dump) and the rolling capture's interp.bin.
Step 105: frames headed "FREF" (RECOMP_D3D_BRIDGE_ROLLING_FULL) carry each draw in full after its record
(d3d_host.c rec_full): the draw's 'full' dict holds prim, counts, stride, the program's input mask and microcode,
the states, each stage's texture, the viewport, the target, the vertices (bytes) and the indices."""
import struct
import numpy as np


def _full(b, o):
    h = struct.unpack_from("<48I", b, o); o += 192
    assert h[0] == 0x4C4C5546, hex(h[0])
    f = dict(prim=h[1], prim_count=h[2], stride=h[3], nv=h[4], ni=h[5], inputs=h[6], n_insns=h[7],
             tex=[dict(key=h[13 + s], format=h[17 + s], w=h[21 + s] & 0xFFFF, h=h[21 + s] >> 16, data=h[25 + s])
                  for s in range(4)],
             vp=(h[29], h[30], h[31], h[32], struct.unpack("<f", struct.pack("<I", h[33]))[0],
                 struct.unpack("<f", struct.pack("<I", h[34]))[0]),
             target=dict(data=h[35], w=h[36] & 0xFFFF, h=h[36] >> 16, format=h[37]), ps_token=h[38],
             flags=h[40], hud_dx=struct.unpack("<f", struct.pack("<I", h[41]))[0], hud=h[42], fvf=h[43])
    f["rs"] = dict(struct.iter_unpack("<2I", b[o:o + 8 * h[8]])); o += 8 * h[8]
    f["tss"] = []
    for s in range(4):
        f["tss"].append(dict(struct.iter_unpack("<2I", b[o:o + 8 * h[9 + s]]))); o += 8 * h[9 + s]
    f["ps_rs"] = dict(struct.iter_unpack("<2I", b[o:o + 8 * h[39]])); o += 8 * h[39]
    f["code"] = list(struct.unpack_from("<%dI" % (4 * h[7]), b, o)); o += 16 * h[7]
    f["verts"] = b[o:o + h[4] * h[3]]; o += h[4] * h[3]
    f["indices"] = np.frombuffer(b, np.uint16, h[5], o).copy() if h[5] else None; o += 2 * h[5]
    return f, o


def read(path):
    b = open(path, "rb").read()
    o = 0
    frames = []
    while o < len(b):
        magic, swap, n, nc = struct.unpack_from("<4I", b, o); o += 16
        assert magic in (0x46524543, 0x46524546), hex(magic)
        full = magic == 0x46524546
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
            c = dict(kind=0, idx=i, cls=r[1], cam=r[2], ncpos=r[3], target=r[4], tw=r[5] & 0xFFFF,
                     th=r[5] >> 16, fvf=r[6], vkey=r[7], flags=r[8], nv=r[9], nprim=r[10], mesh=r[11],
                     camera=f[:32], world32=f32[32:48].reshape(4, 4), world=f[32:48].reshape(4, 4),
                     view=f[48:64].reshape(4, 4), proj=f[64:80].reshape(4, 4),
                     consts32=f32[80:].reshape(192, 4), consts=f[80:].reshape(192, 4), cmask=cm)
            if full:
                c["full"], o = _full(b, o)
            cmds.append(c)
        frames.append(dict(swap=swap, cams=cams, cmds=cmds, full=full))
    return frames
