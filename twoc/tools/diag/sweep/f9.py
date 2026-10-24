"""f9.py -- post F9 to the bridge window (the rolling capture's key)."""
import ctypes
u = ctypes.windll.user32
h = u.FindWindowW(None, "Crash TWOC - D3D bridge (host)")
u.PostMessageW(h, 0x0100, 0x78, 0x00000001 | (0x43 << 16))
u.PostMessageW(h, 0x0101, 0x78, 0xC0000001 | (0x43 << 16))
print("F9 posted", h)
