# emptydirs.py ISO: the image's folders with no file anywhere below them.
import struct, sys
SECTOR = 2048
f = open(sys.argv[1], 'rb', buffering=0)
f.seek(32 * SECTOR + 20)
rs, rz = struct.unpack('<II', f.read(8))
dirs, files = [], []
def walk(s, z, pre):
    if not z:
        return
    f.seek(s * SECTOR)
    d = f.read(z)
    todo = [0]
    while todo:
        p = todo.pop() * 4
        if p + 14 > len(d):
            continue
        l, r, s2, sz, at, nl = struct.unpack_from('<HHIIBB', d, p)
        if l == 0xFFFF:
            continue
        n = d[p + 14:p + 14 + nl].decode('latin-1')
        full = pre + '/' + n if pre else n
        if at & 0x10:
            dirs.append(full)
            walk(s2, sz, full)
        else:
            files.append(full)
        if l: todo.append(l)
        if r: todo.append(r)
walk(rs, rz, '')
empty = [d for d in dirs if not any(x.startswith(d + '/') for x in files)]
print(len(dirs), 'folders,', len(empty), 'with no file below them:')
for d in empty:
    print(' ', d)
