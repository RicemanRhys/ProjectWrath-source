"""Step 107's harness: runs a crash_twoc.exe copy and drives its setup screen.

  python t107.py TAG ROOT EXE [--self] [--secs=N] [--env=K=V ...] [--steps="a;b;c"]

ROOT is the data folder: the working folder for a launcher-style start (env as Play Crash.bat, logs to
ROOT/TAG.err.log), or with --self the exe is started like a double-click (ShellExecute, working folder C:\\,
no RECOMP_ variables but RECOMP_DEV_*; the exe writes run_twoc.err.log in its data folder, copied to
ROOT/TAG.err.log at the end). Steps (';' between):
  wait:REGEX[@SECS]   until REGEX shows in the log (default 120 s); fails the run if not
  sleep:S
  key:NAME[,NAME]     posted to the setup window: tab enter space esc up down left right home end, or a VK number
  type:TEXT           WM_CHAR for each character
  tap:BUTTON          the SDL virtual pad (RECOMP_DEV_PAD_FILE): "xbox BUTTON=1" for 150 ms, then none
  grab:NAME           wgcgrab of the setup window (or the game window with grabg:NAME) into ROOT/../grabs/TAG_NAME
  dialog:PATH         the open file dialog: PATH typed into its name box, Open pressed
  close               WM_CLOSE to the setup window
  kill                taskkill /F (a power cut's stand-in)
  game                wait for the game's window, then 'wait:MAIN MENU OPEN' style checks follow
  end                 stop here (the process is killed if still running)
Prints one line per step and the exit code; ROOT/TAG.steps.txt keeps them.
"""
import ctypes, ctypes.wintypes as wt, os, re, shutil, subprocess, sys, time
sys.stdout.reconfigure(encoding="utf-8", errors="replace")

user32 = ctypes.WinDLL('user32', use_last_error=True)
shell32 = ctypes.WinDLL('shell32', use_last_error=True)
kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
WGC = r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag\wgcgrab.exe"
WM_KEYDOWN, WM_KEYUP, WM_CHAR, WM_CLOSE, WM_SETTEXT, BM_CLICK = 0x100, 0x101, 0x102, 0x10, 0xC, 0xF5
VK = dict(tab=9, enter=13, space=32, esc=27, up=38, down=40, left=37, right=39, home=36, end=35, back=8, f1=112)

# "{NA}" in any argument: a folder name with a space and letters outside ASCII and Windows-1252 (passed through
# Git Bash as ASCII, made here).
NA = 'Test é Ω'
def na(s):
    s = s.replace('{NA}', NA)
    for m in re.findall(r'\{DEEP:(\w+)\}', s):   # a deep folder prep107.py --deep made (its path in a file)
        here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        s = s.replace('{DEEP:%s}' % m, open(os.path.join(here, 't107', 'deep_%s.txt' % m), encoding='utf-8').read())
    return s
tag, root, exe = sys.argv[1], os.path.abspath(na(sys.argv[2])), os.path.abspath(na(sys.argv[3]))
opts = {'secs': '600', 'steps': ''}
envx = {}
self_start = False
for a in sys.argv[4:]:
    a = na(a)
    if a == '--self':
        self_start = True
    elif a.startswith('--env='):
        k, v = a[6:].split('=', 1)
        envx[k] = v
    elif a.startswith('--'):
        k, v = a[2:].split('=', 1)
        opts[k] = v
os.makedirs(root, exist_ok=True)
if opts.get('copyexe'):                 # put the exe (and nothing else) where EXE says, first
    os.makedirs(os.path.dirname(exe), exist_ok=True)
    shutil.copy(opts['copyexe'], exe)
HERE =os.path.dirname(os.path.dirname(os.path.abspath(__file__)))     # the scratch folder
grabs = os.path.join(HERE, 't107', 'grabs')
os.makedirs(grabs, exist_ok=True)
log = os.path.join(root, tag + '.err.log')
os.makedirs(os.path.join(HERE, 't107', 'pads'), exist_ok=True)
padf = os.path.join(HERE, 't107', 'pads', tag + '.pad.txt')    # an ASCII path: pads.c reads it with getenv
stepsf = open(os.path.join(root, tag + '.steps.txt'), 'w', encoding='utf-8')
open(padf, 'w').write('xbox\n')       # plugged in from the start: its first press isn't spent plugging it in
t0 = time.time()

def note(s):
    line = '%7.2f %s' % (time.time() - t0, s)
    print(line, flush=True)
    stepsf.write(line + '\n')
    stepsf.flush()

env = {k: v for k, v in os.environ.items() if not k.startswith('RECOMP_') and k != 'TWOC_LAUNCHER'}
env['RECOMP_DEV_PAD_FILE'] = padf
if not self_start:
    env.update(RECOMP_VBLANK='1', RECOMP_AC97_READY='1', RECOMP_D3D_BRIDGE='1', RECOMP_DEV_FAST_TITLE='1')
env.update(envx)

proc = None
old_ino = None
pid = None
hproc = None
if self_start:
    class SEI(ctypes.Structure):
        _fields_ = [('cbSize', wt.DWORD), ('fMask', ctypes.c_ulong), ('hwnd', wt.HWND), ('lpVerb', wt.LPCWSTR),
                    ('lpFile', wt.LPCWSTR), ('lpParameters', wt.LPCWSTR), ('lpDirectory', wt.LPCWSTR),
                    ('nShow', ctypes.c_int), ('hInstApp', wt.HINSTANCE), ('lpIDList', ctypes.c_void_p),
                    ('lpClass', wt.LPCWSTR), ('hkeyClass', wt.HKEY), ('dwHotKey', wt.DWORD), ('hIcon', wt.HANDLE),
                    ('hProcess', wt.HANDLE)]
    datadir = opts.get('datadir', os.path.dirname(exe))
    try:
        old_ino = os.stat(os.path.join(datadir, 'run_twoc.err.log')).st_ino
    except OSError:
        old_ino = None
    old = dict(os.environ)
    os.environ.clear()
    os.environ.update(env)
    sei = SEI(cbSize=ctypes.sizeof(SEI), fMask=0x40, lpVerb='open', lpFile=exe,
              lpParameters=opts.get('args'), lpDirectory='C:\\', nShow=1)
    ok = shell32.ShellExecuteExW(ctypes.byref(sei))
    os.environ.clear()
    os.environ.update(old)
    if not ok:
        sys.exit('ShellExecuteEx failed %d' % ctypes.get_last_error())
    hproc = sei.hProcess
    pid = kernel32.GetProcessId(hproc)
    datadir = opts.get('datadir', os.path.dirname(exe))
    log_src = os.path.join(datadir, 'run_twoc.err.log')
else:
    out = open(os.path.join(root, tag + '.out.log'), 'wb')
    err = open(log, 'wb')
    proc = subprocess.Popen([exe] + (opts['args'].split() if opts.get('args') else []), cwd=opts.get('cwd', root),
                            env=env, stdout=out, stderr=err)
    pid = proc.pid
    log_src = log
note('started pid %d (%s)' % (pid, 'like a double-click' if self_start else 'launcher-style'))

def alive():
    if proc:
        return proc.poll() is None
    return kernel32.WaitForSingleObject(hproc, 0) == 0x102

def code():
    if proc:
        return proc.poll()
    c = wt.DWORD()
    kernel32.GetExitCodeProcess(hproc, ctypes.byref(c))
    return c.value

def logtext():
    try:
        if self_start and old_ino and os.stat(log_src).st_ino == old_ino:
            return ''                   # still the last run's (the exe moves it into logs\ first)
        return open(log_src, 'rb').read().decode('utf-8', 'replace')
    except OSError:
        return ''

EnumProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

def windows(title_re=None, cls=None):
    res = []
    def cb(h, l):
        p = wt.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(h):
            b = ctypes.create_unicode_buffer(512)
            user32.GetWindowTextW(h, b, 512)
            c = ctypes.create_unicode_buffer(256)
            user32.GetClassNameW(h, c, 256)
            if (title_re is None or re.search(title_re, b.value)) and (cls is None or c.value == cls):
                res.append(h)
        return True
    user32.EnumWindows(EnumProc(cb), 0)
    return res

def setup_win(wait=30):
    end = time.time() + wait
    while time.time() < end:
        w = windows(r'^Crash TWOC - Setup$')
        if w:
            return w[0]
        time.sleep(0.2)
    return None

def post_key(h, vk):
    user32.PostMessageW(h, WM_KEYDOWN, vk, 1)
    time.sleep(0.06)
    user32.PostMessageW(h, WM_KEYUP, vk, 0xC0000001)
    time.sleep(0.12)

def dialog_fill(path, wait=20):
    end = time.time() + wait
    while time.time() < end:
        for d in windows(None, '#32770'):
            edits = []
            def cb(h, l):
                c = ctypes.create_unicode_buffer(64)
                user32.GetClassNameW(h, c, 64)
                if c.value == 'Edit':
                    par = user32.GetParent(h)
                    pc = ctypes.create_unicode_buffer(64)
                    user32.GetClassNameW(par, pc, 64)
                    if pc.value in ('ComboBox', 'ComboBoxEx32') or (pc.value == '#32770' and
                                                                     user32.GetDlgCtrlID(h) == 1152):
                        edits.append(h)     # the file name box, or the folder picker's "Folder:" box
                return True
            user32.EnumChildWindows(d, EnumProc(cb), 0)
            if edits:
                # backslashes (the folder picker refuses forward slashes with an error box), and the click posted,
                # so a box the dialog shows can't block this script
                user32.SendMessageW(edits[0], WM_SETTEXT, 0, ctypes.c_wchar_p(path.replace('/', '\\')))
                time.sleep(0.3)
                ok = user32.GetDlgItem(d, 1)
                user32.PostMessageW(ok, BM_CLICK, 0, 0)
                return True
        time.sleep(0.3)
    return False

import threading
locks = []

def hold_lock(path, secs):
    # opened as Python opens files: read and write sharing, no delete sharing: the folder can't be renamed
    try:
        f = open(path, 'rb')
    except OSError as e:
        note('lock %s: %s' % (path, e))
        return
    note('locked %s for %s s' % (os.path.basename(path), secs))
    time.sleep(secs)
    f.close()
    note('unlocked %s' % os.path.basename(path))

class PMC(ctypes.Structure):
    _fields_ = [('cb', wt.DWORD), ('PageFaultCount', wt.DWORD), ('PeakWorkingSetSize', ctypes.c_size_t),
                ('WorkingSetSize', ctypes.c_size_t), ('QuotaPeakPagedPoolUsage', ctypes.c_size_t),
                ('QuotaPagedPoolUsage', ctypes.c_size_t), ('QuotaPeakNonPagedPoolUsage', ctypes.c_size_t),
                ('QuotaNonPagedPoolUsage', ctypes.c_size_t), ('PagefileUsage', ctypes.c_size_t),
                ('PeakPagefileUsage', ctypes.c_size_t), ('PrivateUsage', ctypes.c_size_t)]

def game_win(wait=60):
    end = time.time() + wait
    while time.time() < end:
        w = windows(r'^Crash TWOC - D3D bridge')
        if w:
            return w[0]
        time.sleep(0.3)
    return None

failed = False
deadline = t0 + float(opts['secs'])
all_steps = [s for s in opts['steps'].split(';') if s.strip()]
for si, st in enumerate(all_steps):
    kind, _, arg = st.partition(':')
    next_kind = all_steps[si + 1].partition(':')[0].strip() if si + 1 < len(all_steps) else ''
    kind = kind.strip()
    if kind == 'wait':
        rx, _, tmo = arg.rpartition('@') if '@' in arg else (arg, '', '120')
        end = time.time() + float(tmo)
        while time.time() < end and not re.search(rx, logtext()) and alive():
            time.sleep(0.2)
        hit = re.search(rx, logtext())
        note('wait %s: %s' % (rx, 'seen' if hit else 'NOT SEEN (alive=%s)' % alive()))
        if not hit:
            failed = True
            break
    elif kind == 'sleep':
        time.sleep(float(arg))
    elif kind == 'key':
        h = setup_win()
        for k in arg.split(','):
            post_key(h, VK.get(k, None) or int(k))
        note('keys %s' % arg)
    elif kind == 'type':
        h = setup_win()
        for ch in arg:
            user32.PostMessageW(h, WM_CHAR, ord(ch), 1)
            time.sleep(0.01)
        note('typed %s' % arg)
    elif kind == 'tap':
        open(padf + '.tmp', 'w').write('xbox %s=1\n' % arg)
        os.replace(padf + '.tmp', padf)
        time.sleep(0.18)
        open(padf + '.tmp', 'w').write('xbox\n')
        os.replace(padf + '.tmp', padf)
        time.sleep(0.3)
        note('pad %s' % arg)
    elif kind in ('grab', 'grabg'):
        title = 'Crash TWOC - Setup' if kind == 'grab' else 'Crash TWOC - D3D bridge (host)'
        pre = os.path.join(grabs, '%s_%s' % (tag, arg))
        r = subprocess.run([WGC, title, pre, '0.3'], capture_output=True, timeout=150)
        note('grab %s -> %s_000.3s.bmp' % (arg, pre))
    elif kind == 'dialog':
        note('dialog %s: %s' % (arg, 'filled' if dialog_fill(arg) else 'NO DIALOG'))
    elif kind == 'close':
        h = setup_win(5)
        if h:
            user32.PostMessageW(h, WM_CLOSE, 0, 0)
        note('close posted' if h else 'close: no setup window')
    elif kind == 'kill':
        subprocess.run(['taskkill', '/F', '/PID', str(pid)], capture_output=True)
        time.sleep(1)
        note('killed (taskkill /F)')
    elif kind == 'waitexit':
        end = time.time() + float(arg or 60)
        while time.time() < end and alive():
            time.sleep(0.2)
        note('exit: %s' % ('code %s' % code() if not alive() else 'STILL RUNNING'))
    elif kind == 'gkey':                # keys to the game's own window (F1 = 112)
        h = game_win()
        for k in arg.split(','):
            post_key(h, VK.get(k, None) or int(k))
        note('game keys %s' % arg)
    elif kind == 'lock':                # lock:PATH@SECS (a thread holds it open meanwhile)
        p, _, secs = arg.rpartition('@')
        t = threading.Thread(target=hold_lock, args=(p, float(secs)), daemon=True)
        t.start()
        locks.append(t)
        time.sleep(0.3)
    elif kind == 'max':
        h = setup_win()
        user32.ShowWindow(h, 3)
        note('setup window maximised')
    elif kind == 'mem':
        pmc = PMC()
        pmc.cb = ctypes.sizeof(PMC)
        hp = kernel32.OpenProcess(0x1000, False, pid)
        ctypes.WinDLL('psapi').GetProcessMemoryInfo(hp, ctypes.byref(pmc), pmc.cb)
        note('memory: working set %.1f MB (peak %.1f), private %.1f MB' % (pmc.WorkingSetSize / 1048576.0,
             pmc.PeakWorkingSetSize / 1048576.0, pmc.PrivateUsage / 1048576.0))
    elif kind == 'follow':              # the game started itself again (Restart now): watch the new copy
        old = pid
        end = time.time() + float(arg or 30)
        newpid = None
        while time.time() < end and not newpid:
            out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq crash_twoc.exe', '/FO', 'CSV', '/NH'],
                                 capture_output=True, text=True).stdout
            for l in out.splitlines():
                p = l.split('","')
                if len(p) > 1 and int(p[1]) != old:
                    newpid = int(p[1])
            time.sleep(0.3)
        if newpid:
            pid = newpid
            proc = None
            hproc = kernel32.OpenProcess(0x101000, False, pid)     # SYNCHRONIZE | QUERY_LIMITED_INFORMATION
            note('following the new copy, pid %d' % pid)
        else:
            note('follow: NO NEW COPY')
            failed = True
            break
    elif kind == 'end':
        break
    if not alive() and kind not in ('kill', 'waitexit', 'end') and next_kind != 'follow':
        note('the process ended (code %s)' % code())
        break
if alive():
    subprocess.run(['taskkill', '/F', '/PID', str(pid)], capture_output=True)
    time.sleep(1)
    note('killed at the end')
note('exit code %s%s' % (code(), '; FAILED a wait' if failed else ''))
if self_start and os.path.exists(log_src):
    shutil.copy(log_src, log)
