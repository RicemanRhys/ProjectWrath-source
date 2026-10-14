"""imgcmp.py A.bin B.bin -- diff two dumps of the in-memory effects image (fxlog), words named by region."""
import struct, sys
img = open(r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\game_files\Crashdat\sfx\dsstdfx.bin", 'rb').read()
a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read()
DESC = 0x6110
regions = [('loader', 0, 0x818)]
n = struct.unpack_from('<I', img, DESC)[0]
for i in range(n):
    co, cs, so, ss, yo, ys, xo, xs = struct.unpack_from('<8I', img, DESC + 8 + 32 * i)
    regions.append(('fx%d code' % i, co, co + cs))
    regions.append(('fx%d state' % i, so, so + ss))
regions.append(('desc', DESC, len(img)))
def where(o):
    for nm, x, y in regions:
        if x <= o < y:
            return '%s +%#x (w%d)' % (nm, o - x, (o - x) // 4)
    return 'beyond +%#x' % (o - len(img)) if o >= len(img) else 'gap'
def s24(x):
    x &= 0xFFFFFF
    return x - (1 << 24) if x & 0x800000 else x
m = min(len(a), len(b))
k = 0
for o in range(0, m, 4):
    x = struct.unpack_from('<I', a, o)[0]
    y = struct.unpack_from('<I', b, o)[0]
    if x != y:
        k += 1
        print('%05x %-26s %08x -> %08x   (frac %.6f -> %.6f)' % (o, where(o), x, y, s24(x) / 8388608.0, s24(y) / 8388608.0))
print(k, 'words differ')
