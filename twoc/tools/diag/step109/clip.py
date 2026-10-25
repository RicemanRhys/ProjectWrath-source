"""clip.py save FILE | restore FILE | check FILE: the clipboard's text kept in FILE before a test and put back after
(the user's clipboard is left as it was, if it held text); check: FILE (a Copy build info paste) holds no user name,
PC name or path."""
import ctypes, ctypes.wintypes as wt, os, re, sys, time
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
u = ctypes.WinDLL('user32', use_last_error=True)
k = ctypes.WinDLL('kernel32', use_last_error=True)
u.GetClipboardData.restype = ctypes.c_void_p
k.GlobalLock.restype = ctypes.c_void_p
k.GlobalLock.argtypes = [ctypes.c_void_p]
k.GlobalUnlock.argtypes = [ctypes.c_void_p]
k.GlobalAlloc.restype = ctypes.c_void_p
u.SetClipboardData.argtypes = [ctypes.c_uint, ctypes.c_void_p]
cmd, f = sys.argv[1], sys.argv[2]

def opened():
    for _ in range(50):
        if u.OpenClipboard(0):
            return True
        time.sleep(0.05)
    return False

if cmd == 'save':
    assert opened()
    h = u.GetClipboardData(13)
    t = ctypes.wstring_at(k.GlobalLock(h)) if h else None
    if h:
        k.GlobalUnlock(h)
    u.CloseClipboard()
    if t is None:
        print('clipboard: no text (nothing to put back)')
        if os.path.exists(f):
            os.remove(f)
    else:
        open(f, 'w', encoding='utf-8', newline='').write(t)
        print('clipboard: %d characters kept' % len(t))
elif cmd == 'restore':
    if not os.path.exists(f):
        print('nothing to put back')
        sys.exit(0)
    t = open(f, encoding='utf-8', newline='').read()
    b = (t + '\0').encode('utf-16-le')
    assert opened()
    u.EmptyClipboard()
    g = k.GlobalAlloc(2, len(b))
    p = k.GlobalLock(g)
    ctypes.memmove(p, b, len(b))
    k.GlobalUnlock(g)
    u.SetClipboardData(13, g)
    u.CloseClipboard()
    print('clipboard: %d characters put back' % len(t))
elif cmd == 'check':
    t = open(f, encoding='utf-8').read()
    user, pc, home = os.environ.get('USERNAME', ''), os.environ.get('COMPUTERNAME', ''), os.environ.get('USERPROFILE', '')
    probs = []
    if home and home.lower() in t.lower():
        probs.append('the home folder')
    if re.search(r'(?i)\\users\\(?!<user>)', t):
        probs.append('a \\Users\\ path')
    if user and re.search(r'(?<![A-Za-z0-9_-])' + re.escape(user) + r'(?![A-Za-z0-9_-])', t):
        probs.append('the user name "%s"' % user)
    if pc and re.search(r'(?i)' + re.escape(pc), t):
        probs.append("the PC's name")
    if re.search(r'(?i)(?<![A-Za-z0-9])[a-z]:[\\/]', t) or '\\\\' in t:
        probs.append('a path')
    print('%d lines, %d characters' % (t.count('\n'), len(t)))
    print('CLEAN: no user name, PC name or path' if not probs else 'NOT CLEAN: ' + ', '.join(probs))
