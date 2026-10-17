"""esc.py -- step 77: post the overlay's key to the bridge window (opens or closes the settings overlay). Step 86:
the key is F1 (Esc only closes it now); `python esc.py esc` posts Esc."""
import ctypes, sys
u = ctypes.windll.user32
h = u.FindWindowW(None, "Crash TWOC - D3D bridge (host)")
print("window", h)
vk, sc = (0x1B, 0x01) if sys.argv[1:] == ["esc"] else (0x70, 0x3B)     # VK_ESCAPE or VK_F1, their scan codes
u.PostMessageW(h, 0x0100, vk, 0x00000001 | (sc << 16))     # WM_KEYDOWN, repeat 1
u.PostMessageW(h, 0x0101, vk, 0xC0000001 | (sc << 16))     # WM_KEYUP
