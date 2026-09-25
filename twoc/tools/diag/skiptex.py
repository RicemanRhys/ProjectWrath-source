"""Decode the DXT textures a RECOMP_D3D_BRIDGE_SKIPTEX run wrote (step 34, notes 18.36).

  python skiptex.py DIR [--montage=OUT.png]

The bridge writes each texture bound by a draw it skips once per kind, next to
the host dumps: skiptex_<kind>_<header>_s<stage>_fmt<XX>_<W>x<H>.bmp for what
the host can upload (P8 through its palette, A8R8G8B8), and .raw (level 0, as
in guest memory) for DXT1 (0C), DXT3 (0E) and DXT5 (0F), which aren't
swizzled. This decodes each .raw to a .png beside it, and with --montage tiles
every skiptex image (BMPs too) with its name, to see what the skipped draws
would have shown. Needs Pillow.
"""
import glob
import os
import struct
import sys

from PIL import Image, ImageDraw


def rgb565(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def colour_block(b, dxt1):
    c0, c1, bits = struct.unpack("<HHI", b)
    p0, p1 = rgb565(c0), rgb565(c1)
    if c0 > c1 or not dxt1:
        p2 = tuple((2 * x + y) // 3 for x, y in zip(p0, p1))
        p3 = tuple((x + 2 * y) // 3 for x, y in zip(p0, p1))
        pal = [p0 + (255,), p1 + (255,), p2 + (255,), p3 + (255,)]
    else:
        p2 = tuple((x + y) // 2 for x, y in zip(p0, p1))
        pal = [p0 + (255,), p1 + (255,), p2 + (255,), (0, 0, 0, 0)]
    return [pal[(bits >> (2 * i)) & 3] for i in range(16)]


def alpha5(b):
    a0, a1 = b[0], b[1]
    bits = int.from_bytes(b[2:8], "little")
    if a0 > a1:
        pal = [a0, a1] + [((6 - i) * a0 + (i + 1) * a1) // 7 for i in range(6)]
    else:
        pal = [a0, a1] + [((4 - i) * a0 + (i + 1) * a1) // 5 for i in range(4)] + [0, 255]
    return [pal[(bits >> (3 * i)) & 7] for i in range(16)]


def decode(path):
    name = os.path.basename(path)
    fmt = int(name.split("_fmt")[1][:2], 16)
    w, h = (int(x) for x in name.rsplit("_", 1)[1][:-4].split("x"))
    data = open(path, "rb").read()
    bsize = 8 if fmt == 0x0C else 16
    im = Image.new("RGBA", (w, h))
    px = im.load()
    k = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            blk = data[k:k + bsize]
            k += bsize
            if len(blk) < bsize:
                break
            if fmt == 0x0C:
                texels = colour_block(blk, True)
            else:
                col = colour_block(blk[8:], False)
                if fmt == 0x0F:
                    al = alpha5(blk[:8])
                else:
                    al = [((blk[i // 2] >> (4 * (i & 1))) & 15) * 17 for i in range(16)]
                texels = [c[:3] + (a,) for c, a in zip(col, al)]
            for i, t in enumerate(texels):
                x, y = bx + (i & 3), by + (i >> 2)
                if x < w and y < h:
                    px[x, y] = t
    out = path[:-4] + ".png"
    im.save(out)
    return out


def main():
    d = sys.argv[1]
    opts = dict(a[2:].split("=", 1) for a in sys.argv[2:] if a.startswith("--") and "=" in a)
    for p in sorted(glob.glob(os.path.join(d, "*skiptex_*.raw"))):
        print(decode(p))
    if "montage" in opts:
        files = sorted(glob.glob(os.path.join(d, "*skiptex_*.png")) + glob.glob(os.path.join(d, "*skiptex_*.bmp")))
        tile, cols = 160, 8
        rows = (len(files) + cols - 1) // cols
        m = Image.new("RGB", (cols * tile, rows * (tile + 24)), (60, 60, 60))
        dr = ImageDraw.Draw(m)
        for i, f in enumerate(files):
            im = Image.open(f).convert("RGBA")
            im.thumbnail((tile, tile))
            bg = Image.new("RGBA", im.size, (255, 0, 255, 255))     # magenta shows alpha
            bg.alpha_composite(im)
            x, y = (i % cols) * tile, (i // cols) * (tile + 24)
            m.paste(bg.convert("RGB"), (x, y + 24))
            n = os.path.basename(f).replace("hostskiptex_", "")
            dr.text((x + 2, y + 1), n[:26], fill=(255, 255, 0))
            dr.text((x + 2, y + 12), n[26:52], fill=(255, 255, 0))
        m.save(opts["montage"])
        print(opts["montage"], len(files), "textures")


if __name__ == "__main__":
    main()
