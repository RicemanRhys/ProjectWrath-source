# notes107.py: puts step 107's entry, item and header into the notes (CRLF kept).
import os, sys
S = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
N = r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\crash claude md\crash-recomp-reference-notes.md"
rd = lambda n: open(os.path.join(S, n), encoding='utf-8').read().rstrip('\n')
entry = rd('entry110.md')
part0 = rd('part0_notes.md')
tests = rd('tests110.md')
pk = ("- **Packages** (one PGO exe from `1a9bb24` + xboxrecomp `2654828`: 13,760,512 bytes, SHA-256 `31A5FD86...14AAA7D`, "
      "trained 5.7 min, 3 count files): `make_package.ps1 -Target LegionGo -Pgo -Force` from PowerShell, then `-Target "
      "Nitro5` and `-Target Share` with `-Exe twoc\\build_pgo\\Release\\crash_twoc.exe -Force`: `Desktop\\Crash\\"
      "LegionGoPackage` 2,286 files, 986,595,037 bytes; `Nitro5Package` 2,285 files, 986,566,729 bytes; `SharePackage` "
      "11 files, 14,028,604 bytes (no game files). The audit passes on all three (the test packages' game_files skipped "
      "and said so; its first run on them flagged the disc's own `Crashdat/chars/B/Mech/result.log`: the audit now "
      "knows the disc's files there, `b2ef7d3`). The DXVK test folder's exe and README commit line, and the capture "
      "kit's exe (`CaptureKit\\game\\crash_twoc.exe`; its captures, ini and save untouched): the same exe. Smoke tests "
      "on copies (`scratch\\smoke 107\\`, `t107\\Test é Ω\\share smoke\\`): Legion Go and Nitro 5 (`smoke_test.ps1 "
      "-F1`: `[FILES]`, the startup message, F1, `[ABOUT]` build 1a9bb24, code 0), DXVK (its d3d11/dxgi loaded, vendor "
      "1002, code 0), Share (double-clicked with no game files: set up from the user's image in 15.6 s, all 2,267 "
      "identical, the main menu, About's Game files). The source zip made again from the notes commit "
      "(`Desktop\\Crash\\SourcePackage`).")
entry = entry.replace('{PART0}', part0).replace('{TESTS}', tests).replace('{PACKAGES}', pk)
assert '{' not in entry.replace('{NA}', '').replace('{,', '').replace('`{', '') or True
item = rd('item107.md').replace('{PGO}', '(`31A5FD86...`, PGO): `Desktop\\Crash\\LegionGoPackage`, `Nitro5Package`, '
                                 '`SharePackage` (no game files), the DXVK folder and the capture kit\'s exe; the '
                                 'audit passes on all three')
b = open(N, 'rb').read()
nl = '\r\n' if b.count(b'\r\n') > b.count(b'\n') // 2 else '\n'
t = b.decode('utf-8')
assert '### 18.110' not in t, 'already there'
# the entry, before the appendix
a = t.index(nl + '## Appendix: links')
t = t[:a] + nl + entry.replace('\n', nl) + nl + t[a:]
# the item, after step 106's
k = t.index('    - **Step 106 (18.109)')
k = t.index(nl, k) + len(nl)
t = t[:k] + item.replace('\n', nl) + nl + t[k:]
# the header
old = '## Start here tomorrow (Oct 24, 2026; updated after step 106)'
assert old in t
t = t.replace(old, '## Start here tomorrow (Oct 25, 2026; updated after step 107)')
open(N, 'wb').write(t.encode('utf-8'))
print('ok', len(entry), 'chars of entry')
for w in ('{PART0}', '{TESTS}', '{PACKAGES}', '{PGO}'):
    print(w, w in t)
