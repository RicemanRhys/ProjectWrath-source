import sys
p = sys.argv[1]
s = open(p, encoding='utf-8').read()
add = '''    elif kind == 'gclose':              # step 109: WM_CLOSE to the game's window (the player closing it)
        h = game_win(10)
        if h:
            user32.PostMessageW(h, WM_CLOSE, 0, 0)
        note('game close posted' if h else 'gclose: no game window')
    elif kind == 'titles':              # step 109: the process's visible windows, their titles and classes
        def cbt(h, l):
            pp = wt.DWORD()
            user32.GetWindowThreadProcessId(h, ctypes.byref(pp))
            if pp.value == pid and user32.IsWindowVisible(h):
                b = ctypes.create_unicode_buffer(512)
                user32.GetWindowTextW(h, b, 512)
                c = ctypes.create_unicode_buffer(256)
                user32.GetClassNameW(h, c, 256)
                note('window "%s" class %s' % (b.value, c.value))
            return True
        user32.EnumWindows(EnumProc(cbt), 0)
        out = subprocess.run(['tasklist', '/FI', 'PID eq %d' % pid, '/FO', 'CSV', '/NH', '/V'],
                             capture_output=True, text=True).stdout.strip()
        note('tasklist: %s' % out)
    elif kind == 'clip':                # step 109: the clipboard's text into ROOT/TAG.clip.txt
        user32.OpenClipboard(0)
        kernel32.GlobalLock.restype = ctypes.c_void_p
        user32.GetClipboardData.restype = ctypes.c_void_p
        hd = user32.GetClipboardData(13)
        txt = ctypes.wstring_at(kernel32.GlobalLock(ctypes.c_void_p(hd))) if hd else ''
        if hd:
            kernel32.GlobalUnlock(ctypes.c_void_p(hd))
        user32.CloseClipboard()
        open(os.path.join(root, tag + '.clip.txt'), 'w', encoding='utf-8').write(txt)
        note('clipboard: %d characters' % len(txt))
    elif kind == 'end':
'''
a = "    elif kind == 'end':\n"
assert s.count(a) == 1
s = s.replace(a, add)
open(p, 'w', encoding='utf-8').write(s)
print('ok')
