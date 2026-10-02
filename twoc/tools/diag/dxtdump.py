"""Decode a DXT texture straight from guest memory (step 52, notes 18.55).

  python dxtdump.py HEADER OUT.png

Reads the texture header at guest VA HEADER (Common, Data, Lock, Format, Size) from a running
crash_twoc.exe, decodes level 0 if it is DXT1/DXT3/DXT5 (format 0C/0E/0F; DXT blocks are not
swizzled on the NV2A) and writes colour and alpha side by side, scaled up. Step 52 used it on
Bamboozled's ball highlight map (01C78F10) while the rings drew black.
"""
import struct
import sys
import memsnap
from PIL import Image

h = memsnap.attach(memsnap.find_pid())
hdr = int(sys.argv[1], 16)
common, data, lock, fmt, size = struct.unpack("<5I", memsnap.read(h, hdr, 20))
f = (fmt >> 8) & 0xFF
w, ht = 1 << ((fmt >> 20) & 15), 1 << ((fmt >> 24) & 15)
print("header %08X: common %08X data %08X format %08X (fmt %02X, %ux%u, levels %u) size %08X" % (
    hdr, common, data, fmt, f, w, ht, (fmt >> 16) & 15, size))
va = 0x80000000 + data
bpb = 8 if f == 0x0C else 16
raw = memsnap.read(h, va, (w // 4) * (ht // 4) * bpb)


def c565(c):
    return ((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31


img = Image.new("RGBA", (w, ht))
for by in range(ht // 4):
    for bx in range(w // 4):
        o = (by * (w // 4) + bx) * bpb
        b = raw[o:o + bpb]
        alpha = [255] * 16
        if bpb == 16:
            cb = b[8:]
            if f == 0x0F:
                a0, a1 = b[0], b[1]
                bits = int.from_bytes(b[2:8], "little")
                pal = [a0, a1] + ([((6 - i) * a0 + (1 + i) * a1) // 7 for i in range(6)] if a0 > a1 else
                                  [((4 - i) * a0 + (1 + i) * a1) // 5 for i in range(4)] + [0, 255])
                alpha = [pal[(bits >> (3 * i)) & 7] for i in range(16)]
            else:
                bits = int.from_bytes(b[0:8], "little")
                alpha = [((bits >> (4 * i)) & 15) * 17 for i in range(16)]
        else:
            cb = b
        c0, c1, idx = struct.unpack("<HHI", cb)
        p0, p1 = c565(c0), c565(c1)
        cols = [p0, p1, tuple((2 * x + y) // 3 for x, y in zip(p0, p1)), tuple((x + 2 * y) // 3 for x, y in zip(p0, p1))]
        if bpb == 8 and c0 <= c1:
            cols[2] = tuple((x + y) // 2 for x, y in zip(p0, p1))
            cols[3] = (0, 0, 0)
        for i in range(16):
            r, g, bl = cols[(idx >> (2 * i)) & 3]
            img.putpixel((bx * 4 + i % 4, by * 4 + i // 4), (r, g, bl, alpha[i]))
out = Image.new("RGB", (2 * w, ht))
out.paste(img.convert("RGB"), (0, 0))
out.paste(img.getchannel("A").convert("RGB"), (w, 0))
out.resize((8 * w, 4 * ht), Image.NEAREST).save(sys.argv[2])
print("first 32 bytes:", raw[:32].hex())
