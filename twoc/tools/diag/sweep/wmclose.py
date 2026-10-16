"""wmclose.py -- step 79: post WM_CLOSE to the bridge window (the player closing it while it renders)."""
import ctypes
u = ctypes.windll.user32
h = u.FindWindowW(None, "Crash TWOC - D3D bridge (host)")
print("window", h)
u.PostMessageW(h, 0x0010, 0, 0)
