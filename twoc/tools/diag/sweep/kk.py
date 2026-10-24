"""kk.py tap|down|up KEY [SECS] -- post a key to the bridge window (no focus): tap = down, SECS (0.05), up.
KEY: a letter, f1-f11, up down left right space lshift lctrl home, or a VK number."""
import ctypes, sys, time
u = ctypes.windll.user32
h = u.FindWindowW(None, "Crash TWOC - D3D bridge (host)")
VK = {"up": 0x26, "down": 0x28, "left": 0x25, "right": 0x27, "space": 0x20, "lshift": 0x10, "lctrl": 0x11,
      "home": 0x24, "esc": 0x1B}
VK.update({"f%d" % i: 0x6F + i for i in range(1, 12)})
EXT = {0x26, 0x28, 0x25, 0x27, 0x24}
a, k = sys.argv[1], sys.argv[2]
vk = VK.get(k) or (ord(k.upper()) if len(k) == 1 else int(k, 0))
sc = u.MapVirtualKeyW(vk, 0)
if k == "lctrl":
    sc = 0x1D
lp = 1 | (sc << 16) | ((1 << 24) if vk in EXT else 0)
if a in ("tap", "down"):
    u.PostMessageW(h, 0x0100, vk, lp)
if a == "tap":
    time.sleep(float(sys.argv[3]) if len(sys.argv) > 3 else 0.05)
if a in ("tap", "up"):
    u.PostMessageW(h, 0x0101, vk, lp | 0xC0000000)
