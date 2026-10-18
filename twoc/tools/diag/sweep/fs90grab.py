"""fs90grab.py PKGDIR OUT.png [SECS] -- start PKGDIR's Play Crash.bat (ShellExecute), wait SECS (default 10) for the deferred
switch to fullscreen, grab the whole screen through GDI (what the player sees, the taskbar included), print the bottom
rows' mean colour and whether the taskbar's window is visible on top, then close the game."""
import ctypes, os, subprocess, sys, time
from PIL import ImageGrab

u = ctypes.windll.user32
ctypes.windll.shcore.SetProcessDpiAwareness(2)
pkg, out = sys.argv[1], sys.argv[2]
secs = float(sys.argv[3]) if len(sys.argv) > 3 else 10
os.startfile(os.path.join(pkg, 'Play Crash.bat'), cwd='C:\\')
time.sleep(secs)
img = ImageGrab.grab(all_screens=False)
img.save(out)
w, h = img.size
px = img.load()
for y in (h - 1, h - 2, h - 30):
    s = [px[x, y] for x in range(0, w, 16)]
    print('row %d: mean %s' % (y, tuple(sum(c[i] for c in s) // len(s) for i in range(3))))
tray = u.FindWindowW('Shell_TrayWnd', None)
r = (ctypes.c_long * 4)()
u.GetWindowRect(tray, r)
print('screen %dx%d; taskbar visible %d at %s; game window in front %s' % (
    w, h, u.IsWindowVisible(tray), list(r), u.GetForegroundWindow() == u.FindWindowW(None, 'Crash TWOC - D3D bridge (host)')))
u.PostMessageW(u.FindWindowW(None, 'Crash TWOC - D3D bridge (host)'), 0x0010, 0, 0)
time.sleep(4)
