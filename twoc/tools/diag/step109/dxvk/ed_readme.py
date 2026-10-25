"""The DXVK test folder's README after step 109: python ed_readme.py README COMMIT"""
import sys
p, commit = sys.argv[1], sys.argv[2]
b = open(p, 'rb').read()
crlf = b.count(b'\r\n') > 0
s = b.decode('utf-8').replace('\r\n', '\n')
import re
reps = [
    ('Crash TWOC - DXVK test (step 86)\n================================',
     'Project Wrath - DXVK test (step 86; renamed in step 109)\n========================================================'),
    ("crash_twoc.exe, byte for byte)", "ProjectWrath.exe, byte for byte)"),
    ("keeps its own settings (crash_twoc.ini in this folder, a copy of the package's: 16:10 at\nnative size, Match display, render thread Auto, the counter on).",
     "keeps its own settings (ProjectWrath.ini in this folder; until the game saves one, it reads the\ncrash_twoc.ini here from before, a copy of the package's: 16:10 at native size, Match display,\nrender thread Auto, the counter on)."),
    ("  Play Crash (DXVK).bat         starts the game with DXVK\n  Play Crash (DXVK, HUD).bat    the same with DXVK's own HUD",
     "  Play Project Wrath (DXVK).bat        starts the game with DXVK\n  Play Project Wrath (DXVK, HUD).bat   the same with DXVK's own HUD"),
    ("  crash_twoc.exe, xemu_apu.dll  the game (the package's exe)", "  ProjectWrath.exe, xemu_apu.dll  the game (the package's exe)"),
    ("  crash_twoc.ini                its own settings", "  crash_twoc.ini                its own settings from before (read until ProjectWrath.ini is saved)"),
    ("2.  Put it next to the LegionGoPackage folder (for example C:\\Games\\DXVK-test beside\n    C:\\Games\\LegionGoPackage).",
     "2.  Put it next to the ProjectWrath-LegionGo folder (for example C:\\Games\\DXVK-test beside\n    C:\\Games\\ProjectWrath-LegionGo)."),
    ('        D:\\Crash\\LegionGoPackage', '        D:\\Games\\ProjectWrath-LegionGo'),
    ('4.  Double-click "Play Crash (DXVK).bat".', '4.  Double-click "Play Project Wrath (DXVK).bat".'),
    ('6.  If there\'s time: "Play Crash (DXVK, HUD).bat"', '6.  If there\'s time: "Play Project Wrath (DXVK, HUD).bat"'),
    ('To compare: the package\'s own "Play Crash.bat" run', 'To compare: the package\'s own "Play Project Wrath.bat" run'),
    ('- To stop using DXVK, just use the package\'s "Play Crash.bat":', '- To stop using DXVK, just use the package\'s "Play Project Wrath.bat":'),
    ("- DXVK keeps a small shader cache in %LOCALAPPDATA%\\dxvk. The first run may hitch a few\n  times as it fills; later runs don't.",
     "- DXVK keeps a small shader cache in %LOCALAPPDATA%\\dxvk, named after the exe (a new\n  ProjectWrath.dxvk-cache since the rename). The first run may hitch a few times as it fills;\n  later runs don't."),
]
for a, c in reps:
    n = s.count(a)
    if n != 1:
        print('NOT FOUND (%d): %r' % (n, a[:70]))
        continue
    s = s.replace(a, c)
s2 = re.sub(r'\(twoc [0-9a-f]{7}, the package', '(twoc %s, the package' % commit, s)
print('commit line', 'updated' if s2 != s else 'NOT FOUND')
s = s2
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('left:', [l for l in s.splitlines() if 'Play Crash' in l or 'crash_twoc.exe' in l])
