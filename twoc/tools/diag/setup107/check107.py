# check107.py ROOT [--hash] [--save NAME]: one line on a test's data folder: game_files (files of the manifest
# present / with the right size / with the right SHA-256 with --hash), leftovers (game_files.new, .old, the journal,
# the request file, game_files.leftover_*, game_files.old_*), the save NAME (game_files\UDATA\...\NAME) carried.
import hashlib, os, sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
NA = 'Test \u00e9 \u03a9'
import re
root = sys.argv[1].replace('{NA}', NA)
for m in re.findall(r'\{DEEP:(\w+)\}', root):
    root = root.replace('{DEEP:%s}' % m, open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                                             't107', 'deep_%s.txt' % m), encoding='utf-8').read())
do_hash = '--hash' in sys.argv
save = sys.argv[sys.argv.index('--save') + 1] if '--save' in sys.argv else None
MAN = r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\data\disc_usa.tsv"
man = [l.rstrip('\r\n').split('\t') for l in open(MAN, encoding='utf-8') if l.strip() and not l.startswith('#')]

def lp(p):
    p = os.path.abspath(p)
    return '\\\\?\\' + p if not p.startswith('\\\\?\\') else p

def state(d):
    if not os.path.isdir(lp(d)):
        return 'missing'
    have = size_ok = hash_ok = 0
    for path, size, sha in man:
        f = lp(os.path.join(d, path.replace('/', os.sep)))
        if os.path.isfile(f):
            have += 1
            if os.path.getsize(f) == int(size):
                size_ok += 1
                if do_hash:
                    h = hashlib.sha256()
                    with open(f, 'rb') as g:
                        for b in iter(lambda: g.read(1 << 20), b''):
                            h.update(b)
                    hash_ok += h.hexdigest() == sha
    extra = []
    for r, ds, fs in os.walk(lp(d)):
        for n in fs:
            rel = os.path.relpath(os.path.join(r, n), lp(d)).replace(os.sep, '/')
            if rel not in {m[0] for m in man}:
                extra.append(rel)
    s = '%d/%d there, %d right size' % (have, len(man), size_ok)
    if do_hash:
        s += ', %d right SHA-256' % hash_ok
    if extra:
        s += '; %d others (%s%s)' % (len(extra), ', '.join(extra[:4]), ', ...' if len(extra) > 4 else '')
    return s

print('game_files:', state(os.path.join(root, 'game_files')))
left = [n for n in os.listdir(lp(root)) if n.startswith('game_files.') or n.startswith('game_files_')]
for n in left:
    p = os.path.join(root, n)
    print('LEFTOVER %s: %s' % (n, state(p) if os.path.isdir(lp(p)) else 'a file'))
if not left:
    print('no leftovers (no game_files.new, .old, journal or request)')
if save:
    hits = []
    for r, ds, fs in os.walk(lp(os.path.join(root, 'game_files'))):
        if save in fs:
            hits.append(os.path.relpath(os.path.join(r, save), lp(root)))
    print('save %s: %s' % (save, hits or 'NOT FOUND in game_files'))
