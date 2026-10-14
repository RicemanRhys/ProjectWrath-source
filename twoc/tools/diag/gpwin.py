"""gpwin.py XYP SECS -- fill a running crash_twoc.exe's GP memory window with a DSP memory image and hold it
(notes 18.75).

XYP is gpfx.exe's GPFX_DUMP output (X 0x1000, Y 0x800, P 0x1000 words, 32 bits each): the GP's memory as
DSOUND's loader and the effects program leave it. The window (guest 0xFE830000: X 0x0000, Y 0x6000,
P 0xA000) is plain guest RAM in this build and stays zero, but DirectSound reads an effect's state back
from it before it recomputes a preset (IDirectSound_SetI3DL2Listener -> sub_00139A36 -> sub_0013958D
reads 0x118 bytes at the descriptor's state pointer, 0xFE830200 for the reverb): with zeros there the
reverb's late-line delays read 0 and its four late lines are written off (gains 0, notes 18.75). With the
window filled, DSOUND computes the preset as it would on hardware. Rewritten every 0.5 s for SECS seconds.
"""
import ctypes, ctypes.wintypes as wt, os, sys, time
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap

K32 = memsnap.K32
K32.WriteProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                   ctypes.POINTER(ctypes.c_size_t)]
GP = 0xFE830000
d = open(sys.argv[1], 'rb').read()
x, y, p = d[:0x4000], d[0x4000:0x6000], d[0x6000:0xA000]
secs = float(sys.argv[2])
t0 = time.time()
pid = None
while not pid and time.time() - t0 < 60:
    pid = memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe'))
    time.sleep(0.2)
h = K32.OpenProcess(0x0438, False, pid)
n = 0
while time.time() - t0 < secs:
    ok = True
    for off, blob in ((0x0000, x), (0x6000, y), (0xA000, p)):
        got = ctypes.c_size_t()
        ok &= bool(K32.WriteProcessMemory(h, ctypes.c_void_p(GP + off + memsnap.OFFSET), blob, len(blob),
                                          ctypes.byref(got)))
    if not ok and not memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe')):
        break
    n += ok
    time.sleep(0.5)
print('gpwin: %d fills over %.0f s; X:0x80.. now %s' % (n, time.time() - t0,
      (memsnap.read(h, GP + 0x200, 16) or b'').hex()))
