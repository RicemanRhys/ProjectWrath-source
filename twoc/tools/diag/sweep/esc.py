"""esc.py -- step 77: post Esc to the bridge window (opens or closes the settings overlay)."""
import ctypes
u = ctypes.windll.user32
h = u.FindWindowW(None, "Crash TWOC - D3D bridge (host)")
print("window", h)
u.PostMessageW(h, 0x0100, 0x1B, 0x00010001)     # WM_KEYDOWN VK_ESCAPE, repeat 1, scan 1
u.PostMessageW(h, 0x0101, 0x1B, 0xC0010001)     # WM_KEYUP
