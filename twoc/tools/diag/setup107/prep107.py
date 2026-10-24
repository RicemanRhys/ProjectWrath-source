# prep107.py ROOT [--gf] [--image NAME] [--exe PATH]: a test's data folder (in scratch):
#   --gf          game_files copied from scratch\fx\gf copy (the disc's files), with a stand-in save
#                 (UDATA\56550003\4D5C926D6357\crash.sav, 12 KB of random bytes; never the user's) and a file of the
#                 player's own at the top (my_notes.txt), both of which a replace must carry across
#   --image NAME  a synthetic Redump-style image at ROOT\NAME (sparse; the XISO's data at 0x18300000)
#   --exe PATH    the exe copied to ROOT\crash_twoc.exe
# "{NA}" in ROOT: a folder name with letters outside ASCII (as t107.py).
import os, shutil, subprocess, sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
NA = 'Test \u00e9 \u03a9'
S = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WOC = r"C:\Users\User\Desktop\Desktop Stuff\Emulators & Roms\Roms\Xbox OG\Crash Bandicoot - The Wrath of Cortex (USA).xiso.iso"
#   --exedir SUB  the exe goes into ROOT\SUB instead (made)
#   --ro SUB      ROOT\SUB made, and the current user denied creating files and folders in it (WD, AD);
#   --unro SUB    that taken away again
#   --deep N      prints (and makes) a folder below ROOT whose full path is about N characters, of long names
#                 with letters outside ASCII; --exe then goes there
a = sys.argv[1:]
root = a[0].replace('{NA}', NA)
os.makedirs(root, exist_ok=True)
user = os.environ['USERNAME']

def lp(p):
    return '\\\\?\\' + os.path.abspath(p)

exedir = root
if '--exedir' in a:
    exedir = os.path.join(root, a[a.index('--exedir') + 1])
if '--deep' in a:
    n = int(a[a.index('--deep') + 1])
    exedir = root
    k = 0
    while len(exedir) < n:
        exedir = os.path.join(exedir, 'Ünïcødé Ωμέγα 長い名前のフォルダー %d' % k)
        k += 1
    os.makedirs(lp(exedir), exist_ok=True)
    print('deep folder: %d characters' % len(exedir))
    open(os.path.join(S, 't107', 'deep_%s.txt' % os.path.basename(root)), 'w', encoding='utf-8').write(exedir)
if '--copyiso' in a:                    # the user's image copied (read only) into the folder made above
    dst = lp(os.path.join(exedir, a[a.index('--copyiso') + 1]))
    with open(WOC, 'rb', buffering=0) as i, open(dst, 'wb') as o:
        shutil.copyfileobj(i, o, 8 << 20)
    print('image copied: %d characters' % len(dst))
if '--unro' in a:
    d = os.path.join(root, a[a.index('--unro') + 1])
    r = subprocess.run(['icacls', d, '/remove:d', user], capture_output=True, text=True)
    print('unro', d, r.returncode, r.stdout.strip()[-60:])
    sys.exit(0)
if '--exe' in a:
    os.makedirs(lp(exedir), exist_ok=True)
    shutil.copy(a[a.index('--exe') + 1].replace('{NA}', NA), lp(os.path.join(exedir, 'crash_twoc.exe')))
if '--ro' in a:
    d = os.path.join(root, a[a.index('--ro') + 1])
    os.makedirs(d, exist_ok=True)
    r = subprocess.run(['icacls', d, '/deny', user + ':(WD,AD)'], capture_output=True, text=True)
    print('ro', d, r.returncode, r.stdout.strip()[-60:])
    try:
        open(os.path.join(d, 'probe.txt'), 'w').close()
        print('  STILL WRITABLE')
    except OSError as e:
        print('  writing there fails as it should:', e.__class__.__name__)
if '--gf' in a:
    g = os.path.join(root, 'game_files')
    if os.path.exists(g):
        sys.exit('game_files already there: ' + g)
    shutil.copytree(os.path.join(S, 'fx', 'gf copy'), g)
    sv = os.path.join(g, 'UDATA', '56550003', '4D5C926D6357')
    os.makedirs(sv)
    open(os.path.join(sv, 'crash.sav'), 'wb').write(os.urandom(12288))
    open(os.path.join(g, 'my_notes.txt'), 'w').write('a file of the player\'s own in game_files\n')
    print('game_files prepared with a stand-in save and my_notes.txt')
if '--request' in a:                    # as the F1 menu's Choose another disc image... leaves it
    open(os.path.join(root, 'game_files.setup-request.txt'), 'w').write('test: open the setup screen\n')
if '--image' in a:
    p = os.path.join(root, a[a.index('--image') + 1])
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, 'wb').close()
    subprocess.run(['fsutil', 'sparse', 'setflag', p], check=True, capture_output=True)
    with open(p, 'r+b') as o, open(WOC, 'rb', buffering=0) as i:
        o.seek(0x18300000)
        shutil.copyfileobj(i, o, 8 << 20)
        o.truncate(7825162240)
    print('image', p)
print('prepared', root)
