# dlgtree.py: the child windows (class, id, parent class, text) of every dialog of crash_twoc.exe's process.
import ctypes, ctypes.wintypes as wt, subprocess, sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
user32 = ctypes.WinDLL('user32')
out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq crash_twoc.exe', '/FO', 'CSV', '/NH'], capture_output=True, text=True).stdout
pids = {int(l.split('","')[1]) for l in out.splitlines() if '","' in l}
EnumProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
def cls(h):
    b = ctypes.create_unicode_buffer(128); user32.GetClassNameW(h, b, 128); return b.value
def txt(h):
    b = ctypes.create_unicode_buffer(256); user32.GetWindowTextW(h, b, 256); return b.value
def top(h, l):
    p = wt.DWORD(); user32.GetWindowThreadProcessId(h, ctypes.byref(p))
    if p.value in pids and cls(h) == '#32770' and user32.IsWindowVisible(h):
        print('dialog', hex(h), txt(h))
        def ch(c, l2):
            if user32.IsWindowVisible(c):
                print('  %-24s id %5d parent %-20s %s' % (cls(c), user32.GetDlgCtrlID(c), cls(user32.GetParent(c)), txt(c)[:50]))
            return True
        user32.EnumChildWindows(h, EnumProc(ch), 0)
    return True
user32.EnumWindows(EnumProc(top), 0)
