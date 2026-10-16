"""boxclick.py SECS [BUTTON] -- step 83: for SECS seconds, find message boxes (#32770) whose title starts with
"Crash TWOC", print their text, and click BUTTON (default: the first of Yes/OK/No found in that order unless
BUTTON names one: OK, Yes, No) with BM_CLICK. Prints each box once."""
import ctypes, ctypes.wintypes as w, sys, time

u = ctypes.windll.user32
secs = float(sys.argv[1])
want = sys.argv[2] if len(sys.argv) > 2 else None
EnumProc = ctypes.WINFUNCTYPE(ctypes.c_bool, w.HWND, w.LPARAM)


def text(h):
    n = u.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u.GetWindowTextW(h, b, n + 1)
    return b.value


def cls(h):
    b = ctypes.create_unicode_buffer(64)
    u.GetClassNameW(h, b, 64)
    return b.value


done = set()
t0 = time.time()
while time.time() - t0 < secs:
    boxes = []
    u.EnumWindows(EnumProc(lambda h, l: (boxes.append(h) if cls(h) == '#32770' and text(h).startswith('Crash TWOC')
                                         and u.IsWindowVisible(h) else None) or True), 0)
    for h in boxes:
        if h in done:
            continue
        kids = []
        u.EnumChildWindows(h, EnumProc(lambda c, l: kids.append(c) or True), 0)
        statics = [text(c) for c in kids if cls(c) == 'Static' and text(c)]
        buttons = {text(c).replace('&', ''): c for c in kids if cls(c) == 'Button'}
        print('BOX "%s": %s | buttons %s' % (text(h), ' / '.join(statics).replace('\n', ' \\n '), list(buttons)),
              flush=True)
        pick = want if want in buttons else next((b for b in ('Yes', 'OK', 'No') if b in buttons), None)
        if pick:
            time.sleep(0.5)
            u.SendMessageW(buttons[pick], 0x00F5, 0, 0)     # BM_CLICK
            print('clicked', pick, flush=True)
        done.add(h)
    time.sleep(0.3)
