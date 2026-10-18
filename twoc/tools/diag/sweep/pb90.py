"""pb90.py TAG -- step 90's "Pause when in the background" check on a launch.py run (RECOMP_PAUSE_UNFOCUSED=1, windowed):
the game window brought in front, then the taskbar made the foreground (the window in the background), then back,
then minimised and restored; the game's frame counter (0x25A660, through memsnap) read at each step."""
import ctypes, ctypes.wintypes as w, subprocess, sys, time, os

u = ctypes.windll.user32
k = ctypes.windll.kernel32
TITLE = "Crash TWOC - D3D bridge (host)"
tag = sys.argv[1]


def front(h):
    fg = u.GetForegroundWindow()
    a = u.GetWindowThreadProcessId(fg, None)
    b = k.GetCurrentThreadId()
    u.AttachThreadInput(b, a, True)
    u.SetWindowPos(h, 0, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040)
    r = u.SetForegroundWindow(h)
    u.AttachThreadInput(b, a, False)
    return r


sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
MH = memsnap.attach(memsnap.find_pid())


def counter(h):
    v = memsnap.u32(MH, 0x25A660)
    return -1 if v is None else v


h = u.FindWindowW(None, TITLE)
tray = u.FindWindowW("Shell_TrayWnd", None)
steps = [("front", lambda: front(h)), ("background (taskbar in front)", lambda: front(tray)),
         ("front again", lambda: front(h)), ("minimised", lambda: u.ShowWindow(h, 6)),
         ("restored", lambda: (u.ShowWindow(h, 9), front(h)))]
for name, f in steps:
    f()
    c0 = counter(h)
    time.sleep(4)
    c1 = counter(h)
    fg = u.GetForegroundWindow()
    print("%-30s frame counter %d -> %d (+%d in 4 s), foreground ours %s, iconic %d" % (
        name, c0, c1, c1 - c0, fg == h, u.IsIconic(h)), flush=True)
