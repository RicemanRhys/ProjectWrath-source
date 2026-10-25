"""explorer_win.py NAME [--close]: the Explorer windows (CabinetWClass) whose title holds NAME; with --close, WM_CLOSE
to each (the window "Open logs folder" opened)."""
import ctypes, ctypes.wintypes as wt, sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
u = ctypes.WinDLL('user32')
name = sys.argv[1].replace('{NA}', 'Test é Ω')
found = []
CB = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
def cb(h, l):
    c = ctypes.create_unicode_buffer(64)
    u.GetClassNameW(h, c, 64)
    if c.value == 'CabinetWClass':
        t = ctypes.create_unicode_buffer(512)
        u.GetWindowTextW(h, t, 512)
        if name.lower() in t.value.lower():
            found.append((h, t.value))
    return True
u.EnumWindows(CB(cb), 0)
for h, t in found:
    print('Explorer window "%s"' % t)
    if '--close' in sys.argv:
        u.PostMessageW(h, 0x10, 0, 0)
        print('  closed')
if not found:
    print('no Explorer window with "%s"' % name)
