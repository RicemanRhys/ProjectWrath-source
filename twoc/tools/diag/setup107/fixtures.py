# fixtures.py OUTDIR: step 107's test inputs, all in scratch, from the user's images read only.
#   redump.iso      a synthetic Redump-style (XGD1) image: a stand-in video partition (an ISO 9660 mark at 0x8001,
#                   zeros) and the XISO's game partition at 0x18300000, the file sized as a full XGD1 dump
#                   (7,825,162,240 bytes), sparse (only the written parts take disk space)
#   truncated.iso   the XISO's first 600 MB
#   random.iso      10 MB of random bytes
#   empty\          an empty folder
#   other_game\     Crash Twinsanity's default.xbe alone (from its image)
#   pal_xbe\        the extracted game with default.xbe's certificate region set to PAL (Crashdat a junction)
#   v2_xbe\         ... with its version set to 2
#   damaged.iso     a copy of the XISO with one byte of Crashdat/chars.dat changed (a damaged image)
# The junctions point at the repo's game_files\Crashdat: remove them with rmdir, never a recursive delete.
import os, sys, struct, subprocess, shutil, hashlib

WOC = r"C:\Users\User\Desktop\Desktop Stuff\Emulators & Roms\Roms\Xbox OG\Crash Bandicoot - The Wrath of Cortex (USA).xiso.iso"
TWIN = r"C:\Users\User\Desktop\Desktop Stuff\Emulators & Roms\Roms\Xbox OG\Crash Twinsanity (USA, Asia).xiso.iso"
GF = r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\game_files"
out = os.path.abspath(sys.argv[1])
os.makedirs(out, exist_ok=True)
SECTOR = 2048

def ro(p):
    return open(p, 'rb', buffering=0)       # read only (Python opens with read and write sharing)

# redump.iso
red = os.path.join(out, 'redump.iso')
if not os.path.exists(red):
    with open(red, 'wb') as o:
        pass
    subprocess.run(['fsutil', 'sparse', 'setflag', red], check=True, capture_output=True)
    with open(red, 'r+b') as o, ro(WOC) as i:
        o.seek(0x8000)
        o.write(b'\x01CD001\x01')           # a video partition's ISO 9660 descriptor's start (a stand-in)
        o.seek(0x18300000)
        while True:
            b = i.read(8 << 20)
            if not b:
                break
            o.write(b)
        o.truncate(7825162240)
    print('redump.iso', os.path.getsize(red))

# truncated.iso
tr = os.path.join(out, 'truncated.iso')
if not os.path.exists(tr):
    with open(tr, 'wb') as o, ro(WOC) as i:
        left = 600 << 20
        while left:
            b = i.read(min(left, 8 << 20))
            o.write(b)
            left -= len(b)
    print('truncated.iso', os.path.getsize(tr))

rnd = os.path.join(out, 'random.iso')
if not os.path.exists(rnd):
    with open(rnd, 'wb') as o:
        o.write(os.urandom(10 << 20))

os.makedirs(os.path.join(out, 'empty'), exist_ok=True)

# Twinsanity's default.xbe
def xiso_file(iso, name):
    f = ro(iso)
    f.seek(32 * SECTOR + 20)
    rs, rz = struct.unpack('<II', f.read(8))
    f.seek(rs * SECTOR)
    d = f.read(rz)
    todo = [0]
    while todo:
        p = todo.pop() * 4
        l, r, s, sz, at, nl = struct.unpack_from('<HHIIBB', d, p)
        if l == 0xFFFF:
            continue
        n = d[p + 14:p + 14 + nl].decode('latin-1')
        if n.lower() == name.lower():
            f.seek(s * SECTOR)
            return f.read(sz)
        if l: todo.append(l)
        if r: todo.append(r)

og = os.path.join(out, 'other_game')
os.makedirs(og, exist_ok=True)
if not os.path.exists(os.path.join(og, 'default.xbe')):
    open(os.path.join(og, 'default.xbe'), 'wb').write(xiso_file(TWIN, 'default.xbe'))

def doctored(name, off_in_cert, value):
    d = os.path.join(out, name)
    os.makedirs(d, exist_ok=True)
    x = bytearray(open(os.path.join(GF, 'default.xbe'), 'rb').read())
    base, cert = struct.unpack_from('<I', x, 0x104)[0], struct.unpack_from('<I', x, 0x118)[0]
    struct.pack_into('<I', x, cert - base + off_in_cert, value)
    open(os.path.join(d, 'default.xbe'), 'wb').write(x)
    j = os.path.join(d, 'Crashdat')
    if not os.path.exists(j):
        subprocess.run(['cmd', '/c', 'mklink', '/J', j, os.path.join(GF, 'Crashdat')], check=True, capture_output=True)

doctored('pal_xbe', 0xA0, 4)
doctored('v2_xbe', 0xAC, 2)

dm = os.path.join(out, 'damaged.iso')
if not os.path.exists(dm):
    shutil.copyfile(WOC, dm)
    # chars.dat's place on the image, from its directory entry
    f = ro(WOC)
    f.seek(32 * SECTOR + 20)
    rs, rz = struct.unpack('<II', f.read(8))
    def find(sec, size, path):
        f.seek(sec * SECTOR)
        d = f.read(size)
        todo = [0]
        while todo:
            p = todo.pop() * 4
            l, r, s, sz, at, nl = struct.unpack_from('<HHIIBB', d, p)
            if l == 0xFFFF:
                continue
            n = d[p + 14:p + 14 + nl].decode('latin-1')
            if n.lower() == path[0].lower():
                return (s, sz) if len(path) == 1 else find(s, sz, path[1:])
            if l: todo.append(l)
            if r: todo.append(r)
    s, sz = find(rs, rz, ['Crashdat', 'chars.dat'])
    with open(dm, 'r+b') as o:
        o.seek(s * SECTOR + sz // 2)
        b = o.read(1)
        o.seek(s * SECTOR + sz // 2)
        o.write(bytes([b[0] ^ 0xFF]))
    print('damaged.iso: chars.dat byte', sz // 2, 'flipped')
print('fixtures in', out)
for n in sorted(os.listdir(out)):
    print(' ', n)
