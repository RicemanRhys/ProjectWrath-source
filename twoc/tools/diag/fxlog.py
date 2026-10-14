"""fxlog.py -- what Crash sends the GP DSP, read from outside a running crash_twoc.exe.

  fxlog.py dump OUTPREFIX          GP/EP memory windows, the sound manager, the effects-image descriptor
  fxlog.py watch OUT.txt SECS      4 Hz: level, path section/point, reverb preset, listener factor;
                                   on any change of the preset or of the reverb/xtalk state words, a line
                                   and a dump of those words (OUT.txt.states/)

The GP's memory window (guest 0xFE830000..0xFE840000: X 0x0000, mixbuf 0x5000, Y 0x6000, P 0xA000)
is plain guest RAM in this build (main.c), so what DirectSound writes there reads back as written.
"""
import os, struct, sys, time, hashlib
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap

GP = 0xFE830000
EP = 0xFE850000
MGR = 0x427A58          # the game's sound manager (IDirectSound* at +0, desc* at +8, preset +0x18)
LEVEL = 0x19C068
PATHOBJ = 0x17B0DC
IMG = int(os.environ.get('FX_IMG', '80BC8000'), 16)   # the image copy (doorbell - 0x810)

h = memsnap.attach(memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe')))

def u32(va):
    return memsnap.u32(h, va)

def f32(va):
    b = memsnap.read(h, va, 4)
    return struct.unpack('<f', b)[0] if b else float('nan')

def desc():
    p = u32(MGR + 8)
    if not p:
        return p, []
    n = u32(p)
    maps = []
    for i in range(min(n or 0, 16)):
        maps.append(struct.unpack('<8I', memsnap.read(h, p + 8 + 32 * i, 32)))
    return p, maps

def state_words(m):
    """The effect's state block as DSP words (each stored in a 32-bit slot)."""
    so, ss = m[2], m[3]
    if not so or not ss or ss > 0x4000:
        return b''
    return memsnap.read(h, so, ss) or b''

def scene():
    lev = u32(LEVEL)
    po = u32(PATHOBJ)
    sec = pt = None
    if po:
        b = memsnap.read(h, po + 0x48, 4)
        if b:
            sec = struct.unpack_from('<b', b, 0)[0]
            pt = struct.unpack_from('<h', b, 2)[0]
    return dict(level=lev, sec=sec, pt=pt, preset=u32(MGR + 0x18), dist=f32(MGR + 0x14),
                block=u32(0x427A50), ds=u32(MGR))

if sys.argv[1] == 'dump':
    out = sys.argv[2]
    open(out + '.gp.bin', 'wb').write(memsnap.read(h, GP, 0x10000) or b'')
    open(out + '.ep.bin', 'wb').write(memsnap.read(h, EP, 0x10000) or b'')
    open(out + '.mgr.bin', 'wb').write(memsnap.read(h, MGR, 0x40) or b'')
    p, maps = desc()
    with open(out + '.desc.txt', 'w') as f:
        f.write('desc %s scene %s\n' % (hex(p or 0), scene()))
        for i, m in enumerate(maps):
            f.write('fx%d ' % i + ' '.join('%08x' % x for x in m) + '\n')
    print(open(out + '.desc.txt').read())
    sys.exit()

out, secs = sys.argv[2], float(sys.argv[3])
sd = out + '.states'
os.makedirs(sd, exist_ok=True)
f = open(out, 'w', buffering=1)
t0 = time.time()
last = {}
lasth = {}
nd = 0
while time.time() - t0 < secs:
    t = time.time() - t0
    try:
        s = scene()
        p, maps = desc()
    except Exception as e:
        f.write('%.2f error %s\n' % (t, e))
        break
    if s['level'] is None:
        f.write('%.2f process gone\n' % t)
        break
    key = (s['level'], s['preset'], s['block'], s['dist'])
    if key != last.get('k'):
        f.write('%.2f level %s sec %s pt %s preset %s dist %.1f block %s\n' % (
            t, s['level'], s['sec'], s['pt'], s['preset'], s['dist'], s['block']))
        last['k'] = key
    # section/point every 2 s, for the map of presets by place
    if t - last.get('pt', -9) >= 2:
        f.write('%.2f   at level %s sec %s pt %s preset %s\n' % (t, s['level'], s['sec'], s['pt'], s['preset']))
        last['pt'] = t
    img = memsnap.read(h, IMG, 0x6400 + 0x2000) or b''
    hh = hashlib.md5(img).hexdigest()[:10]
    if lasth.get('img') != hh:
        nd += 1
        fn = os.path.join(sd, '%04d_img_%.1f.bin' % (nd, t))
        open(fn, 'wb').write(img)
        f.write('%.2f image copy %s -> %s (level %s preset %s)\n' % (t, hh, os.path.basename(fn), s['level'], s['preset']))
        lasth['img'] = hh
    for i in (0, 1, 8, 9):
        if i < len(maps):
            w = state_words(maps[i])
            hh = hashlib.md5(w).hexdigest()[:10]
            if lasth.get(i) != hh:
                nd += 1
                fn = os.path.join(sd, '%04d_fx%d_%.1f.bin' % (nd, i, t))
                open(fn, 'wb').write(w)
                f.write('%.2f fx%d state %s (%d bytes at %08x) -> %s\n' % (t, i, hh, len(w), maps[i][2], os.path.basename(fn)))
                lasth[i] = hh
    time.sleep(0.25)
f.write('end\n')
