# Count the files on an XDVDFS (xiso) image, per top-level folder, and hash default.xbe.
import sys, struct, hashlib, collections

SECTOR = 2048
path = sys.argv[1]
f = open(path, 'rb')

base = None
for off in (0, 0x18300000, 0x30600000, 0x0FD90000):
    f.seek(off + 32 * SECTOR)
    if f.read(20) == b'MICROSOFT*XBOX*MEDIA':
        base = off
        break
if base is None:
    sys.exit('no XDVDFS volume descriptor')
f.seek(base + 32 * SECTOR + 20)
root_sec, root_size = struct.unpack('<II', f.read(8))

counts = collections.Counter()
xbe = None
nfiles = 0

def walk(sec, size, prefix):
    global nfiles, xbe
    f.seek(base + sec * SECTOR)
    data = f.read(size)
    seen = set()
    todo = [0]
    while todo:
        o = todo.pop()
        if o in seen or o * 4 + 14 > len(data):
            continue
        seen.add(o)
        p = o * 4
        left, right, s, sz, attr, nl = struct.unpack_from('<HHIIBB', data, p)
        if left == 0xFFFF:
            continue
        name = data[p + 14:p + 14 + nl].decode('latin-1')
        if left:
            todo.append(left)
        if right:
            todo.append(right)
        full = prefix + '\\' + name if prefix else name
        if attr & 0x10:
            if sz:
                walk(s, sz, full)
        else:
            nfiles += 1
            top = full.split('\\')[0] + ('\\' + full.split('\\')[1] if full.count('\\') >= 2 else '')
            counts[top] += 1
            if full.lower() == 'default.xbe':
                pos = f.tell()
                f.seek(base + s * SECTOR)
                b = f.read(sz)
                xbe = (sz, hashlib.sha256(b).hexdigest())
                f.seek(pos)

walk(root_sec, root_size, '')
print('files', nfiles)
for k in sorted(counts):
    print(' ', k, counts[k])
print('default.xbe', xbe)
