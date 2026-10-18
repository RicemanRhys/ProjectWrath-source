"""fs90.py PKGDIR OUTDIR TAG N SECS MODE [RT] -- step 90's fullscreen-start sweep.

Starts PKGDIR\\Play Crash.bat N times through ShellExecute (os.startfile, like a double-click; working
directory C:\\), waits for the game window, MODE 'behind' leaves it (it takes the focus itself) and
'front' brings the console's window (the bat's) in front of it at once; after SECS closes the game
window (WM_CLOSE), waits for the process to go and copies run_twoc.err.log to OUTDIR\\TAG_NN.err.log.
RT (on|off|auto) is written into the copy's crash_twoc.ini (render_thread) first. Prints one line a
run: stall lines, recovery lines, the longest swap gap, the watchdog.
"""
import ctypes, ctypes.wintypes as w, os, re, shutil, subprocess, sys, time

u = ctypes.windll.user32
k = ctypes.windll.kernel32
TITLE = "Crash TWOC - D3D bridge (host)"
pkg, out, tag, n, secs, mode = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), float(sys.argv[5]), sys.argv[6]
rt = sys.argv[7] if len(sys.argv) > 7 else None
os.makedirs(out, exist_ok=True)
ini = os.path.join(pkg, 'crash_twoc.ini')
if rt:
    lines = open(ini, encoding='utf-8').read().splitlines()
    lines = [l for l in lines if not l.startswith('render_thread')] + ['render_thread = ' + rt]
    open(ini, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')


def game_pids():
    o = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq crash_twoc.exe', '/FO', 'CSV', '/NH'],
                       capture_output=True, text=True).stdout
    return {int(l.split('","')[1]) for l in o.splitlines() if l.startswith('"crash_twoc')}


def windows_of(pids):
    res = []
    CB = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)

    def cb(h, _):
        pid = w.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(pid))
        if pid.value in pids and u.IsWindowVisible(h):
            res.append(h)
        return True
    u.EnumWindows(CB(cb), 0)
    return res


def console_window():
    """The newest top-level window titled like the bat's console (conhost or Windows Terminal)."""
    found = []
    CB = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)

    def cb(h, _):
        b = ctypes.create_unicode_buffer(512)
        u.GetWindowTextW(h, b, 512)
        if u.IsWindowVisible(h) and ('Play Crash' in b.value or 'crash_twoc' in b.value.lower()
                                     or b.value.endswith('cmd.exe')):
            found.append(h)
        return True
    u.EnumWindows(CB(cb), 0)
    return found[0] if found else None


def front(h):
    # the foreground lock: attach to the foreground thread's input, then SetForegroundWindow
    fg = u.GetForegroundWindow()
    a = u.GetWindowThreadProcessId(fg, None)
    b = k.GetCurrentThreadId()
    u.AttachThreadInput(b, a, True)
    u.SetWindowPos(h, 0, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040)   # HWND_TOP, NOSIZE|NOMOVE|SHOWWINDOW
    r = u.SetForegroundWindow(h)
    u.AttachThreadInput(b, a, False)
    return r


for i in range(n):
    if game_pids():
        print('a game is already running: stop'); sys.exit(1)
    log = os.path.join(pkg, 'run_twoc.err.log')
    t0 = time.time()
    os.startfile(os.path.join(pkg, 'Play Crash.bat'), cwd='C:\\')
    h = None
    while time.time() - t0 < 20 and not h:
        time.sleep(0.05)
        p = game_pids()
        if p:
            h = u.FindWindowW(None, TITLE)
    note = ''
    if not h:
        note = 'no window'
    elif mode == 'front':
        time.sleep(0.15)
        c = console_window()
        note = 'console %s front %s' % (c, front(c) if c else '-')
    fgs = []
    while time.time() - t0 < secs:
        time.sleep(0.5)
        fg = u.GetForegroundWindow()
        fgs.append('g' if fg == h else 'o')
    u.PostMessageW(u.FindWindowW(None, TITLE), 0x0010, 0, 0)
    t1 = time.time()
    while game_pids() and time.time() - t1 < 30:
        time.sleep(0.2)
    left = game_pids()
    if left:
        subprocess.run(['taskkill', '/F', '/IM', 'crash_twoc.exe'], capture_output=True)
        note += ' KILLED'
    time.sleep(2.0)
    dst = os.path.join(out, '%s_%02d.err.log' % (tag, i))
    try:
        shutil.copy(log, dst)
    except OSError as e:
        print(i, 'no log', e); continue
    t = open(dst, encoding='utf-8', errors='replace').read()
    stall = t.count('released no frame')
    rec = len(re.findall(r'display recovery \d+:', t))
    wd = t.count('render thread watchdog: the')
    gaps = [float(x) for x in re.findall(r'longest ([0-9.]+) ms', t)]
    fsl = 'fs' if 'borderless fullscreen now' in t or 'borderless fullscreen, ' in t else '--'
    modes = re.findall(r'presentation mode: ([a-z ()A-Z]+?)(?:, was|\s+at present)', t)
    print('%s_%02d stall %d recovery-lines %d watchdog %d longest %.0f ms %s fg %s %s modes %s' % (
        tag, i, stall, rec, wd, max(gaps) if gaps else -1, fsl, ''.join(fgs[:6]), note, '/'.join(modes[:4])),
        flush=True)
