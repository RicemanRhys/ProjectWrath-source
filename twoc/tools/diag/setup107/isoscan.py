# isoscan.py ISO [GAMEDIR] [--manifest OUT]
# Reads an XDVDFS image read-only: finds the volume, walks every directory, hashes every file (SHA-256),
# parses default.xbe's certificate, and optionally compares with an extracted folder hash by hash.
import sys, os, struct, hashlib, time

SECTOR = 2048
OFFS = (0, 0x18300000, 0x30600000, 0x0FD90000)

def open_ro(path):
    return open(path, 'rb', buffering=0)

def find_base(f):
    for off in OFFS:
        f.seek(off + 32 * SECTOR)
        if f.read(20) == b'MICROSOFT*XBOX*MEDIA':
            return off
    return None

def walk(f, base, sec, size, prefix, out, depth=0):
    f.seek(base + sec * SECTOR)
    data = f.read(size)
    todo = [0]
    seen = set()
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
        full = prefix + '/' + name if prefix else name
        if attr & 0x10:
            out.append(('d', full, s, sz, attr))
            if sz:
                walk(f, base, s, sz, full, out, depth + 1)
        else:
            out.append(('f', full, s, sz, attr))

def xbe_cert(b):
    base = struct.unpack_from('<I', b, 0x104)[0]
    cert_va = struct.unpack_from('<I', b, 0x118)[0]
    c = cert_va - base
    tid = struct.unpack_from('<I', b, c + 8)[0]
    title = b[c + 0x0C:c + 0x0C + 80].decode('utf-16-le').split('\0')[0]
    media, region, rating, disk, ver = struct.unpack_from('<IIIII', b, c + 0x9C)
    return dict(title_id='%08X' % tid, title=title, media='%08X' % media, region='%08X' % region,
                rating=rating, disk=disk, version='%08X' % ver)

def main():
    iso = sys.argv[1]
    gd = None
    man = None
    a = sys.argv[2:]
    if '--manifest' in a:
        i = a.index('--manifest'); man = a[i + 1]; del a[i:i + 2]
    if a:
        gd = a[0]
    t0 = time.time()
    f = open_ro(iso)
    total = os.fstat(f.fileno()).st_size
    base = find_base(f)
    if base is None:
        sys.exit('no XDVDFS volume descriptor')
    f.seek(base + 32 * SECTOR + 20)
    root_sec, root_size = struct.unpack('<II', f.read(8))
    ts = struct.unpack('<Q', f.read(8))[0]
    print('image', iso, 'size', total, 'partition at', hex(base), 'root sector', root_sec, 'size', root_size, 'filetime', ts)
    ents = []
    walk(f, base, root_sec, root_size, '', ents)
    files = [e for e in ents if e[0] == 'f']
    dirs = [e for e in ents if e[0] == 'd']
    print('files', len(files), 'dirs', len(dirs), 'bytes', sum(e[3] for e in files))
    rows = []
    for _, path, s, sz, attr in sorted(files, key=lambda e: e[1].lower()):
        f.seek(base + s * SECTOR)
        h = hashlib.sha256()
        left = sz
        while left:
            n = min(left, 1 << 20)
            b = f.read(n)
            if len(b) != n:
                sys.exit('short read ' + path)
            h.update(b)
            left -= n
        rows.append((path, sz, h.hexdigest(), s))
        if path.lower() == 'default.xbe':
            f.seek(base + s * SECTOR)
            print('default.xbe', sz, h.hexdigest(), xbe_cert(f.read(sz)))
    maxpath = max(len(r[0]) for r in rows)
    print('longest path', maxpath, max(rows, key=lambda r: len(r[0]))[0])
    print('largest file', max(rows, key=lambda r: r[1])[:2])
    nonascii = [r[0] for r in rows if any(ord(c) > 126 for c in r[0])]
    print('non-ascii names', len(nonascii), nonascii[:5])
    print('hashed in %.1f s' % (time.time() - t0))
    if man:
        with open(man, 'w', newline='\n') as o:
            for p, sz, hx, s in rows:
                o.write('%s\t%d\t%s\n' % (p, sz, hx))
        print('manifest written', man)
    if gd:
        diff = 0
        seen = set()
        for p, sz, hx, s in rows:
            lp = os.path.join(gd, p.replace('/', os.sep))
            seen.add(os.path.normcase(os.path.normpath(lp)))
            if not os.path.isfile(lp):
                print('MISSING in folder', p); diff += 1; continue
            st = os.path.getsize(lp)
            if st != sz:
                print('SIZE differs', p, st, sz); diff += 1; continue
            h = hashlib.sha256()
            with open(lp, 'rb') as g:
                for b in iter(lambda: g.read(1 << 20), b''):
                    h.update(b)
            if h.hexdigest() != hx:
                print('HASH differs', p); diff += 1
        extra = []
        for root, ds, fs in os.walk(gd):
            for n in fs:
                q = os.path.normcase(os.path.normpath(os.path.join(root, n)))
                if q not in seen:
                    extra.append(os.path.relpath(os.path.join(root, n), gd))
        print('compared', len(rows), 'files: differ', diff, '; folder files not on the disc', len(extra))
        for e in extra[:40]:
            print('  extra', e)

main()
