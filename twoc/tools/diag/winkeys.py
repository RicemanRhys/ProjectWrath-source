"""Post keys to the bridge window without focusing it (step 54, notes 18.57).

  python winkeys.py altenter | esc | KEY ...   KEY: up down left right enter space tab x, or a VK number;
                                                wait:S sleeps S seconds
  python winkeys.py rect                       print the window and client rectangles and the style
  python winkeys.py click X Y                  left click at client X, Y (the window's own pixels)
"""
import ctypes, sys, time
from ctypes import wintypes

u = ctypes.windll.user32
TITLE = "Crash TWOC - D3D bridge (host)"
VK = {"esc": 0x1B, "enter": 0x0D, "space": 0x20, "tab": 0x09, "up": 0x26, "down": 0x28, "left": 0x25,
      "right": 0x27, "x": 0x58}
SC = {0x1B: 0x01, 0x0D: 0x1C, 0x20: 0x39, 0x09: 0x0F, 0x26: 0x48, 0x28: 0x50, 0x25: 0x4B, 0x27: 0x4D, 0x58: 0x2D}

h = u.FindWindowW(None, TITLE)
if not h:
    sys.exit("no window")


def post(msg, wp, lp):
    u.PostMessageW(h, msg, wp, lp & 0xFFFFFFFF)


for a in sys.argv[1:]:
    if a == "rect":
        r = wintypes.RECT()
        u.GetWindowRect(h, ctypes.byref(r))
        c = wintypes.RECT()
        u.GetClientRect(h, ctypes.byref(c))
        print("window", r.left, r.top, r.right, r.bottom, "client", c.right, c.bottom,
              "style %08X" % (u.GetWindowLongW(h, -16) & 0xFFFFFFFF))
    elif a == "altenter":
        post(0x0104, 0x0D, 1 | (0x1C << 16) | (1 << 29))            # WM_SYSKEYDOWN, Alt held
        post(0x0105, 0x0D, 1 | (0x1C << 16) | (1 << 29) | (3 << 30))
    elif a == "click":
        break
    elif a.startswith("wait:"):
        time.sleep(float(a[5:]))
    else:
        vk = VK.get(a) or int(a, 0)
        post(0x0100, vk, 1 | (SC.get(vk, 0) << 16))                  # WM_KEYDOWN
        if vk not in (0x1B,):
            post(0x0102, vk, 1)                                       # WM_CHAR for text keys
        post(0x0101, vk, 1 | (SC.get(vk, 0) << 16) | (3 << 30))      # WM_KEYUP
    time.sleep(0.15)
if "click" in sys.argv:
    i = sys.argv.index("click")
    x, y = int(sys.argv[i + 1]), int(sys.argv[i + 2])
    lp = (y << 16) | (x & 0xFFFF)
    post(0x0200, 0, lp)                                               # WM_MOUSEMOVE
    time.sleep(0.1)
    post(0x0201, 1, lp)                                               # WM_LBUTTONDOWN
    time.sleep(0.1)
    post(0x0202, 0, lp)                                               # WM_LBUTTONUP
