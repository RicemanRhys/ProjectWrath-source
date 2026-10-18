"""windows.py [key F1|ESC] : the package smoke test's helper (smoke_test.ps1, notes 18.91): list crash_twoc.exe's visible
top-level windows; with 'key NAME', post that key (WM_KEYDOWN + WM_KEYUP) to the bridge window."""
import ctypes, ctypes.wintypes as w, subprocess, sys
u = ctypes.windll.user32
if len(sys.argv) > 2 and sys.argv[1] == 'key':
    vk = {'F1': 0x70, 'ESC': 0x1B}[sys.argv[2]]
    h = u.FindWindowW(None, "Crash TWOC - D3D bridge (host)")
    u.PostMessageW(h, 0x0100, vk, 1)
    u.PostMessageW(h, 0x0101, vk, 0xC0000001)
    print('posted', sys.argv[2], 'to', h)
    sys.exit(0)
out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq crash_twoc.exe', '/FO', 'CSV', '/NH'],
                     capture_output=True, text=True).stdout
pids = {int(l.split('","')[1]) for l in out.splitlines() if l.startswith('"crash_twoc')}
res = []
CB = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
def cb(h, _):
    pid = w.DWORD()
    u.GetWindowThreadProcessId(h, ctypes.byref(pid))
    if pid.value in pids and u.IsWindowVisible(h):
        b = ctypes.create_unicode_buffer(256)
        u.GetWindowTextW(h, b, 256)
        r = w.RECT()
        u.GetWindowRect(h, ctypes.byref(r))
        res.append(f'window {h}: "{b.value}" {r.right - r.left}x{r.bottom - r.top}')
    return True
u.EnumWindows(CB(cb), 0)
print(f'crash_twoc pids {sorted(pids)}; {len(res)} visible windows')
for r in res:
    print(r)
