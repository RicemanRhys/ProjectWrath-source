"""Parse an Xbox DSOUND effects image (dsstdfx.bin): descriptor table and state headers."""
import struct, sys

path = sys.argv[1]
d = open(path, 'rb').read()
DESC = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x6110

def u32(o):
    return struct.unpack_from('<I', d, o)[0]

def bin_name(a):
    # GP X memory: VP mixbins at 0x1400 + 0x20*bin; GP outputs at 0xC00 + 0x20*ch
    if 0x1400 <= a < 0x1800 and (a - 0x1400) % 0x20 == 0:
        b = (a - 0x1400) // 0x20
        names = {0: 'FL', 1: 'FR', 2: 'C', 3: 'LFE', 4: 'BL', 5: 'BR', 6: 'XTLK_FL', 7: 'XTLK_FR',
                 8: 'XTLK_BL', 9: 'XTLK_BR', 10: 'I3DL2'}
        return 'bin%d(%s)' % (b, names.get(b, 'FXSEND%d' % (b - 11)))
    if 0xC00 <= a < 0xD00 and (a - 0xC00) % 0x20 == 0:
        return 'out%d(%s)' % ((a - 0xC00) // 0x20, ['FL', 'FR', 'C', 'LFE', 'BL', 'BR', 'o6', 'o7'][(a - 0xC00) // 0x20])
    if 0x800 <= a < 0xC00 and a % 0x20 == 0:
        return 'tmp%x' % a
    return None

n = u32(DESC)
print('effects', n, 'second dword %#x' % u32(DESC + 4))
for i in range(n):
    r = struct.unpack_from('<8I', d, DESC + 8 + 32 * i)
    co, cs, so, ss, yo, ys, xo, xs = r
    print('fx%d code %#06x+%#06x state %#06x+%#05x ymem %#06x+%#x scratch %#07x+%#x' % (i, co, cs, so, ss, yo, ys, xo, xs))
    st = [u32(so + 4 * k) for k in range(ss // 4)]
    hdr = st[:5]
    rest = st[5:]
    ios = []
    for k, w in enumerate(rest):
        nm = bin_name(w)
        if nm is None:
            break
        ios.append(nm)
    print('   hdr scratchOff %#x scratchLen %#x yOff %#x yLen %#x flags %#x' % tuple(hdr))
    print('   io:', ' '.join(ios))
    params = rest[len(ios):]
    print('   params(%d):' % len(params), ' '.join('%06x' % w for w in params[:48]), '...' if len(params) > 48 else '')
