"""Post keys to the bridge window without focusing it (step 54, notes 18.57).

  python winkeys.py altenter | esc | KEY ...   KEY: up down left right enter space tab backspace lshift rshift
                                                lctrl f1-f11, a letter or digit, or a VK number; wait:S sleeps
                                                S seconds; down:KEY / up:KEY hold and let go (step 57: the
                                                keyboard's pad reads the window's own key messages)
  python winkeys.py rect                       print the window and client rectangles and the style
  python winkeys.py click X Y                  left click at client X, Y (the window's own pixels);
                                                rclick X Y the right button
"""
import ctypes, sys, time
from ctypes import wintypes

u = ctypes.windll.user32
TITLE = "Crash TWOC - D3D bridge (host)"
VK = {"esc": 0x1B, "enter": 0x0D, "space": 0x20, "tab": 0x09, "up": 0x26, "down": 0x28, "left": 0x25,
      "right": 0x27, "backspace": 0x08, "lshift": 0x10, "rshift": 0x10, "lctrl": 0x11, "delete": 0x2E}
VK.update({"f%d" % i: 0x6F + i for i in range(1, 12)})
SC = {0x1B: 0x01, 0x0D: 0x1C, 0x20: 0x39, 0x09: 0x0F, 0x26: 0x48, 0x28: 0x50, 0x25: 0x4B, 0x27: 0x4D, 0x58: 0x2D}
EXT = {0x26, 0x28, 0x25, 0x27, 0x2E}                                 # extended keys (arrows, Delete)


def vk_of(name):
    if name in VK:
        return VK[name]
    if len(name) == 1 and name.isalnum():
        return ord(name.upper())
    return int(name, 0)


def key_lp(name, vk, up):
    sc = 0x36 if name == "rshift" else SC.get(vk) or u.MapVirtualKeyW(vk, 0)
    lp = 1 | (sc << 16) | ((1 << 24) if vk in EXT else 0)
    return lp | (3 << 30) if up else lp

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
    elif a in ("click", "rclick"):
        break
    elif a.startswith("wait:"):
        time.sleep(float(a[5:]))
    elif a.startswith("down:") or a.startswith("up:"):
        name = a.split(":", 1)[1]
        vk = vk_of(name)
        up = a.startswith("up:")
        post(0x0101 if up else 0x0100, vk, key_lp(name, vk, up))     # WM_KEYUP / WM_KEYDOWN
    else:
        vk = vk_of(a)
        post(0x0100, vk, key_lp(a, vk, False))                        # WM_KEYDOWN
        if vk != 0x1B and not 0x70 <= vk <= 0x7B:                     # step 86: no WM_CHAR for F1-F12 either
            post(0x0102, vk, 1)                                       # WM_CHAR for text keys
        time.sleep(0.05)                                              # long enough for a game poll to see it
        post(0x0101, vk, key_lp(a, vk, True))                         # WM_KEYUP
    time.sleep(0.15)
for btn, (dn, upm) in (("click", (0x0201, 0x0202)), ("rclick", (0x0204, 0x0205))):
    if btn in sys.argv:
        i = sys.argv.index(btn)
        x, y = int(sys.argv[i + 1]), int(sys.argv[i + 2])
        lp = (y << 16) | (x & 0xFFFF)
        post(0x0200, 0, lp)                                           # WM_MOUSEMOVE
        time.sleep(0.1)
        post(dn, 1 if btn == "click" else 2, lp)                      # WM_LBUTTONDOWN / WM_RBUTTONDOWN
        time.sleep(0.1)
        post(upm, 0, lp)
