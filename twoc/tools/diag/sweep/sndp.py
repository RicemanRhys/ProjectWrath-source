"""sndp.py [N] -- step 102 part 3: the level's sound run ([0x463810], 0x30-byte entries from id 0xB1): each entry's
label, name, volume word (+0x12), word +0x10, rate|flags (+0x1C), type (+0x20), id (+0x22); and both DRVD_BG entries."""
import struct, sys
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
h = memsnap.attach(memsnap.find_pid('crash_twoc.exe'))
n = int(sys.argv[1]) if len(sys.argv) > 1 else 24


def ent(a):
    d = memsnap.read(h, a, 0x30)
    lab = d[:16].split(b'\0')[0].decode('latin1')
    np_ = struct.unpack_from('<I', d, 0x18)[0]
    nm = memsnap.read(h, np_, 32).split(b'\0')[0].decode('latin1') if np_ else ''
    return '%08X %-16s %-24s w10 %04X vol %04X rf %08X type %02X id %04X' % (
        a, lab, nm, struct.unpack_from('<H', d, 0x10)[0], struct.unpack_from('<H', d, 0x12)[0],
        struct.unpack_from('<I', d, 0x1C)[0], d[0x20], struct.unpack_from('<H', d, 0x22)[0])


run = memsnap.u32(h, 0x463810)
print('run [0x463810] = %08X' % run)
for i in range(n):
    print(' %02X' % (0xB1 + i), ent(run + 0x30 * i))
print('DRVD entries:')
print(' ', ent(0x1A83D8))
print(' ', ent(0x1A9E18))
