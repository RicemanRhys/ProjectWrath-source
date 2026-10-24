"""drag.py TAG TO_PCT -- step 104: drag the Menu scale slider with posted mouse messages (no real pointer) from its
grab to the place of TO_PCT, from the last "[OVL] menu scale slider at" line of runs/TAG.err.log (picture pixels;
the window is in real pixels with a render size, so client = picture)."""
import ctypes, os, re, sys, time
u = ctypes.windll.user32
h = u.FindWindowW(None, "Crash TWOC - D3D bridge (host)")
log = os.path.join(os.path.dirname(os.path.abspath(__file__)), "runs", sys.argv[1] + ".err.log")
to = int(sys.argv[2])
m = None
for l in open(log, "rb").read().decode("utf-8", "replace").splitlines():
    r = re.search(r"menu scale slider at (\d+),(\d+) - (\d+),(\d+) \((\d+)%\)", l)
    if r:
        m = [int(x) for x in r.groups()]
x0, y0, x1, y1, cur = m
def xat(p):
    f = (p - 50) / 150.0
    return int(x0 + (x1 - x0) * (0.04 + 0.92 * f))
y = (y0 + y1) // 2
from ctypes import wintypes
rc = wintypes.RECT()
u.GetClientRect(h, ctypes.byref(rc))
BW = int(sys.argv[3]) if len(sys.argv) > 3 else rc.right
K = rc.right / BW
def lp(x, y):
    x, y = int(x * K), int(y * K)
    return (y << 16) | (x & 0xFFFF)
WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP = 0x200, 0x201, 0x202
xs, xe = xat(cur), xat(to)
u.PostMessageW(h, WM_MOUSEMOVE, 0, lp(xs, y)); time.sleep(0.1)
u.PostMessageW(h, WM_LBUTTONDOWN, 1, lp(xs, y)); time.sleep(0.1)
for k in range(1, 11):
    u.PostMessageW(h, WM_MOUSEMOVE, 1, lp(xs + (xe - xs) * k // 10, y)); time.sleep(0.05)
time.sleep(0.3)
u.PostMessageW(h, WM_LBUTTONUP, 0, lp(xe, y))
print("dragged", cur, "->", to, "x", xs, xe, "y", y)
