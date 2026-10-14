"""Decode sub_000AA330's per-level reverb switch: level index -> case address, with level names."""
import struct
d = open(r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\game_files\default.xbe", "rb").read()
base, = struct.unpack_from('<I', d, 0x104)
nsec, secaddr = struct.unpack_from('<II', d, 0x11c)
secs = []
for i in range(nsec):
    fl, va, vs, ra, rs = struct.unpack_from('<IIIII', d, secaddr - base + i * 56)
    secs.append((va, vs, ra, rs))
def rd(va, n):
    for s in secs:
        if s[0] <= va < s[0] + s[3]:
            return d[s[2] + va - s[0]:s[2] + va - s[0] + n]
def cstr(va):
    b = rd(va, 64)
    return b.split(b'\0')[0].decode('latin1') if b else '?'

idx = rd(0xAA500, 37)
tab = struct.unpack('<%dI' % (max(idx) + 1), rd(0xAA4CC, 4 * (max(idx) + 1)))
for lev in range(1, 38):
    case = tab[idx[lev - 1]]
    name = cstr(struct.unpack_from('<I', rd(0x19B138 + 0x58 * lev, 4))[0]) if lev < 44 else ''
    print('level %2d %-28s case %#x' % (lev, name, case))
