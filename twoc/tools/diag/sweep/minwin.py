"""minwin.py min|restore -- step 79: minimise or restore the bridge window (ShowWindow), while it renders."""
import ctypes, sys
u = ctypes.windll.user32
h = u.FindWindowW(None, "Crash TWOC - D3D bridge (host)")
print("window", h, sys.argv[1])
u.ShowWindow(h, 6 if sys.argv[1] == "min" else 9)     # SW_MINIMIZE, SW_RESTORE
