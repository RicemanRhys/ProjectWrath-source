"""prepla.py LA [--tree OUT]: a stand-in %LOCALAPPDATA% at LA with an old CrashTWOC data folder (step 109's data-folder
migration test): CrashTWOC\\game_files (the disc's files copied from the repo's game_files by robocopy, no saves of
the user's; a stand-in save UDATA\\56550003\\4D5C926D6357\\crash.sav of random bytes), CrashTWOC\\crash_twoc.ini (a
stand-in), logs\\, run_twoc.err.log and crash_twoc.muted (which the migration leaves behind).
--tree OUT: instead, writes every file of LA\\CrashTWOC with its size and SHA-256 to OUT (before/after)."""
import hashlib, os, subprocess, sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
NA = 'Test \u00e9 \u03a9'
REPO = r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp"
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
a = sys.argv[1:]
la = os.path.abspath(a[0].replace('{NA}', NA))
old = os.path.join(la, 'CrashTWOC')
if '--tree' in a:
    out = open(a[a.index('--tree') + 1], 'w', encoding='utf-8')
    n = 0
    for dp, dn, fn in os.walk(old):
        dn.sort()
        for f in sorted(fn):
            p = os.path.join(dp, f)
            out.write('%s\t%d\t%s\n' % (os.path.relpath(p, old), os.path.getsize(p),
                                        hashlib.sha256(open(p, 'rb').read()).hexdigest()))
            n += 1
    print('tree: %d files' % n)
    sys.exit(0)
gf = os.path.join(old, 'game_files')
os.makedirs(gf, exist_ok=True)
r = subprocess.run(['robocopy', os.path.join(REPO, 'game_files'), gf, '/E', '/XD',
                    os.path.join(REPO, 'game_files', 'UDATA'), os.path.join(REPO, 'game_files', 'UDATA_backups'),
                    os.path.join(REPO, 'game_files', 'TDATA'), '/XF', 'crash_analysis.json', '/NFL', '/NDL', '/NJH',
                    '/NJS', '/NP'], capture_output=True, text=True)
assert r.returncode < 8, r.stdout
sv = os.path.join(gf, 'UDATA', '56550003', '4D5C926D6357')
os.makedirs(sv, exist_ok=True)
open(os.path.join(sv, 'crash.sav'), 'wb').write(os.urandom(12288))
open(os.path.join(old, 'crash_twoc.ini'), 'wb').write(open(os.path.join(HERE, 'fx', 'old_crash_twoc.ini'), 'rb').read())
os.makedirs(os.path.join(old, 'logs'), exist_ok=True)
open(os.path.join(old, 'logs', 'run_2026-10-01_10-00-00.err.log'), 'w').write('an old log\n')
open(os.path.join(old, 'run_twoc.err.log'), 'w').write('the last old log\n')
open(os.path.join(old, 'crash_twoc.muted'), 'w').write('')
print('made', old)
