"""Re-render the bridge's render-target draws in software and compare (notes 18.35).

  python rtcheck.py TAG.err.log DUMPDIR TEX0.raw [--tol=8] [--out=DIR] [--filter=point|linear]

The NV2A executor can't be compared on render targets: it rasterises into a
swizzled texture as linear rows. So this is the check instead. Run

  drive.py TAG 14 --bridge --point --dump=DIR --dump-rt --rtlog      (--filter=point, the default)
  drive.py TAG 14 --bridge --dump=DIR --dump-rt --rtlog              (--filter=linear: the game's own)

and read texture 0's guest bytes while it runs (TEX0.raw, 32-bit A8R8G8B8,
swizzled, as the game made it). RECOMP_D3D_BRIDGE_RTLOG writes every
render-target clear ([D3DB] rtclear) and draw ([D3DB] rtdraw: states and every
vertex dword). This script replays them on the CPU: D3D11's rasterisation
(pixel centres at +0.5, top-left rule), point sampling of level 0 (what
--point makes the host do) or trilinear filtering over the texture's mip
chain (the game's LINEAR/LINEAR/LINEAR; the level of detail from each
triangle's texture gradients), the texture stage's MODULATE of texture and
diffuse, the alpha test and the blend, 8-bit targets. Then it compares each
target with the last host dump of it (DUMPDIR/hostNNN_rt_<Data>.bmp), RGB
within --tol (S3's bar, 8) and within 3, and writes the reference as rtref_<Data>.png in --out.

Point sampling can't match bit for bit where a pixel centre falls exactly on a
texel boundary (an 8- or 16-pixel quad over a 32-texel sprite puts every
sample there), since the GPU's tie-break follows its interpolation rounding;
those texels account for the differences left in step 33.

Handles what the title uses: pre-transformed TRIANGLELIST quads (FVF 0x144),
one texture, MODULATE/SELECTARG1/SELECTARG2, blend factors ZERO, ONE,
SRCALPHA, INVSRCALPHA with ADD, alpha test, WRAP/CLAMP. Anything else is
reported and skipped.
"""
import os
import re
import struct
import sys
from PIL import Image


def swz_offset(x, y, w, h):
    o, bit, i = 0, 0, 0
    while (1 << i) < max(w, h):
        if (1 << i) < w:
            o |= ((x >> i) & 1) << bit
            bit += 1
        if (1 << i) < h:
            o |= ((y >> i) & 1) << bit
            bit += 1
        i += 1
    return o


def load_mips(path, w, h):
    """Every level of a swizzled A8R8G8B8 texture, levels back to back."""
    raw = open(path, "rb").read()
    levels, base = [], 0
    while True:
        lv = []
        for y in range(h):
            row = []
            for x in range(w):
                o = base + 4 * swz_offset(x, y, w, h)
                b, g, r, a = raw[o], raw[o + 1], raw[o + 2], raw[o + 3]
                row.append((r / 255.0, g / 255.0, b / 255.0, a / 255.0))
            lv.append(row)
        levels.append(lv)
        base += 4 * w * h
        if (w == 1 and h == 1) or base + 4 * max(1, w // 2) * max(1, h // 2) > len(raw):
            return levels
        w, h = max(1, w // 2), max(1, h // 2)


def addr(i, n, mode):
    return i % n if mode == 1 else min(max(i, 0), n - 1)


def bilinear(lv, u, v, au, av):
    h, w = len(lv), len(lv[0])
    x, y = u * w - 0.5, v * h - 0.5
    x0, y0 = int(x // 1), int(y // 1)
    fx, fy = x - x0, y - y0
    out = []
    for c in range(4):
        t00 = lv[addr(y0, h, av)][addr(x0, w, au)][c]
        t10 = lv[addr(y0, h, av)][addr(x0 + 1, w, au)][c]
        t01 = lv[addr(y0 + 1, h, av)][addr(x0, w, au)][c]
        t11 = lv[addr(y0 + 1, h, av)][addr(x0 + 1, w, au)][c]
        out.append((t00 * (1 - fx) + t10 * fx) * (1 - fy) + (t01 * (1 - fx) + t11 * fx) * fy)
    return out


def trilinear(mips, u, v, lod, au, av):
    if lod <= 0:
        return bilinear(mips[0], u, v, au, av)
    l0 = int(lod)
    f = lod - l0
    if l0 >= len(mips) - 1:
        return bilinear(mips[-1], u, v, au, av)
    a, b = bilinear(mips[l0], u, v, au, av), bilinear(mips[l0 + 1], u, v, au, av)
    return [x * (1 - f) + y * f for x, y in zip(a, b)]


def f32(u):
    return struct.unpack("<f", struct.pack("<I", u))[0]


def argb(u):
    return (((u >> 16) & 255) / 255.0, ((u >> 8) & 255) / 255.0, (u & 255) / 255.0, ((u >> 24) & 255) / 255.0)


def factor(code, s):
    if code == 0:
        return 0.0
    if code == 1:
        return 1.0
    if code == 0x302:
        return s[3]
    if code == 0x303:
        return 1.0 - s[3]
    raise ValueError("blend factor %X" % code)


def edge(ax, ay, bx, by, px, py):
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax)


def is_top_left(ax, ay, bx, by, cw):
    # edge a->b in a triangle of winding cw (screen space, y down)
    dx, dy = bx - ax, by - ay
    if not cw:
        dx, dy = -dx, -dy
    return (dy == 0 and dx > 0) or dy < 0


def raster_tri(v, W, H, shade):
    (x0, y0), (x1, y1), (x2, y2) = [(p[0], p[1]) for p in v]
    area = edge(x0, y0, x1, y1, x2, y2)
    if area == 0:
        return
    cw = area < 0
    tl = [is_top_left(x1, y1, x2, y2, cw), is_top_left(x2, y2, x0, y0, cw), is_top_left(x0, y0, x1, y1, cw)]
    minx, maxx = max(0, int(min(x0, x1, x2))), min(W - 1, int(max(x0, x1, x2)) + 1)
    miny, maxy = max(0, int(min(y0, y1, y2))), min(H - 1, int(max(y0, y1, y2)) + 1)
    for py in range(miny, maxy + 1):
        cy = py + 0.5
        for px in range(minx, maxx + 1):
            cx = px + 0.5
            w0 = edge(x1, y1, x2, y2, cx, cy)
            w1 = edge(x2, y2, x0, y0, cx, cy)
            w2 = edge(x0, y0, x1, y1, cx, cy)
            if cw:
                w0, w1, w2 = -w0, -w1, -w2
            ok = True
            for w, t in zip((w0, w1, w2), tl):
                if w < 0 or (w == 0 and not t):
                    ok = False
                    break
            if ok:
                a = abs(area)
                shade(px, py, w0 / a, w1 / a, w2 / a)


def main():
    log, dumpdir, texpath = sys.argv[1:4]
    opts = dict(a[2:].split("=", 1) for a in sys.argv[4:] if "=" in a)
    tol = int(opts.get("tol", "8"))
    linear = opts.get("filter", "point") == "linear"
    out = opts.get("out", ".")
    targets = {}
    tex = None
    skipped = draws = 0
    rx_clear = re.compile(r"\[D3DB\] rtclear target ([0-9A-F]{8}) flags ([0-9A-F]+) colour ([0-9A-F]{8})")
    rx_draw = re.compile(r"\[D3DB\] rtdraw target ([0-9A-F]{8}) prim (\d+) verts (\d+) stride (\d+) fvf ([0-9A-F]+) "
                         r"tex0 ([0-9A-F]{8}) (\d+)x(\d+) fmt ([0-9A-F]+) blend (\d+) ([0-9A-F]+) ([0-9A-F]+) op "
                         r"([0-9A-F]+) atest (\d+) ([0-9A-F]+) ref ([0-9A-F]+) colorop (\d+) (\d+) (\d+) alphaop "
                         r"(\d+) (\d+) (\d+) addr (\d+) (\d+):(.*)")
    for line in open(log, encoding="latin1"):
        m = rx_clear.search(line)
        if m:
            t = int(m.group(1), 16)
            c = argb(int(m.group(3), 16))
            if int(m.group(2), 16) & 0xF0:
                targets[t] = [[list(c) for _ in range(128)] for _ in range(128)]
            continue
        m = rx_draw.search(line)
        if not m:
            continue
        g = m.groups()
        t = int(g[0], 16)
        prim, nv, stride, fvf = int(g[1]), int(g[2]), int(g[3]), int(g[4], 16)
        tw, th = int(g[6]), int(g[7])
        blend, sb, db, bop = int(g[9]), int(g[10], 16), int(g[11], 16), int(g[12], 16)
        atest, afunc, aref = int(g[13]), int(g[14], 16), int(g[15], 16)
        cop, ca1, ca2, aop, aa1, aa2 = [int(x) for x in g[16:22]]
        addru, addrv = int(g[22]), int(g[23])
        if prim != 5 or fvf != 0x144 or stride != 28 or bop != 0x8006 or afunc not in (0x204, 0x207):
            skipped += 1
            continue
        if tex is None:
            mips = load_mips(texpath, tw, th)
            tex = mips[0]
        dw = [int(x, 16) for x in g[24].split()]
        verts = []
        for i in range(nv):
            b = dw[7 * i:7 * i + 7]
            verts.append((f32(b[0]), f32(b[1]), argb(b[4]), f32(b[5]), f32(b[6])))
        buf = targets.setdefault(t, [[[0.0, 0.0, 0.0, 0.0] for _ in range(128)] for _ in range(128)])
        draws += 1

        def wrap(c, n, mode):
            i = int(c * n // 1) if c >= 0 else int(c * n) - (1 if c * n != int(c * n) else 0)
            return i % n if mode == 1 else min(max(i, 0), n - 1)

        for k in range(0, nv - 2, 3):
            tri = verts[k:k + 3]
            lod = 0.0
            if linear:              # the triangle's texture gradients, in texels per pixel
                import math
                (xa, ya, _, ua, va), (xb, yb, _, ub, vb), (xc, yc, _, uc, vc) = tri
                det = (xb - xa) * (yc - ya) - (xc - xa) * (yb - ya)
                if det:
                    dudx = ((ub - ua) * (yc - ya) - (uc - ua) * (yb - ya)) / det * tw
                    dudy = ((uc - ua) * (xb - xa) - (ub - ua) * (xc - xa)) / det * tw
                    dvdx = ((vb - va) * (yc - ya) - (vc - va) * (yb - ya)) / det * th
                    dvdy = ((vc - va) * (xb - xa) - (vb - va) * (xc - xa)) / det * th
                    rho = max(math.hypot(dudx, dvdx), math.hypot(dudy, dvdy))
                    lod = math.log2(rho) if rho > 0 else 0.0

            def shade(px, py, b0, b1, b2, tri=tri, lod=lod):
                u = b0 * tri[0][3] + b1 * tri[1][3] + b2 * tri[2][3]
                vv = b0 * tri[0][4] + b1 * tri[1][4] + b2 * tri[2][4]
                d = [b0 * tri[0][2][i] + b1 * tri[1][2][i] + b2 * tri[2][2][i] for i in range(4)]
                if linear:
                    tx = trilinear(mips, u, vv, lod, addru, addrv)
                else:
                    tx = tex[wrap(vv, th, addrv)][wrap(u, tw, addru)]

                def arg(a, i):
                    return tx[i] if a == 2 else d[i]

                def op(o, a1, a2, i):
                    if o == 4:
                        return arg(a1, i) * arg(a2, i)
                    if o == 2:
                        return arg(a1, i)
                    if o == 3:
                        return arg(a2, i)
                    raise ValueError("op %d" % o)

                s = [op(cop, ca1, ca2, i) for i in range(3)] + [op(aop, aa1, aa2, 3)]
                if atest:
                    a8 = int(round(s[3] * 255))
                    if afunc == 0x204 and not a8 > aref:
                        return
                dst = buf[py][px]
                if blend:
                    fs, fd = factor(sb, s), factor(db, s)
                    res = [min(1.0, s[i] * fs + dst[i] * fd) for i in range(4)]
                else:
                    res = s
                buf[py][px] = [round(c * 255) / 255.0 for c in res]

            raster_tri([(p[0], p[1]) for p in tri], 128, 128, shade)

    print("replayed %d draws (%d skipped) on %d targets" % (draws, skipped, len(targets)))
    names = sorted(os.listdir(dumpdir))
    for t, buf in sorted(targets.items()):
        dumps = [n for n in names if n.endswith("_rt_%08X.bmp" % t)]
        ref = Image.new("RGB", (128, 128))
        for y in range(128):
            for x in range(128):
                ref.putpixel((x, y), tuple(int(round(c * 255)) for c in buf[y][x][:3]))
        ref.save(os.path.join(out, "rtref_%08X.png" % t))
        if not dumps:
            print("target %08X: no host dump" % t)
            continue
        host = Image.open(os.path.join(dumpdir, dumps[-1])).convert("RGB")
        n = within = within3 = same = 0
        worst = 0
        for p, q in zip(host.getdata(), ref.getdata()):
            d = max(abs(p[i] - q[i]) for i in range(3))
            n += 1
            within += d <= tol
            within3 += d <= 3
            same += d == 0
            worst = max(worst, d)
        print("target %08X vs %s: %.2f%% within %d, %.2f%% within 3, %.2f%% identical, largest difference %d" %
              (t, dumps[-1], 100.0 * within / n, tol, 100.0 * within3 / n, 100.0 * same / n, worst))


if __name__ == "__main__":
    main()
