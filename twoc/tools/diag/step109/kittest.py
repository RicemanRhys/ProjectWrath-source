"""kittest.py DIR: a replica of the capture kit (kit\\run.cmd from the user's kit, game\\ with the kit's ProjectWrath.exe and
DLL, a stand-in ini, game_files as prep109 makes them, an empty captures\\) run like its numbered launchers; waits for the
main menu in the capture's run.err.log, closes the game window, prints the capture folder's files and the [KIT] line."""
import ctypes, os, shutil, subprocess, sys, time, glob
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
KIT = r"C:\Users\User\Desktop\Crash\CaptureKit"
d = os.path.abspath(sys.argv[1])
here = os.path.dirname(os.path.abspath(__file__))
os.makedirs(os.path.join(d, 'kit'), exist_ok=True)
os.makedirs(os.path.join(d, 'captures'), exist_ok=True)
shutil.copy(os.path.join(KIT, 'kit', 'run.cmd'), os.path.join(d, 'kit', 'run.cmd'))
subprocess.run([sys.executable, os.path.join(here, 'prep109.py'), os.path.join(d, 'game'),
                os.path.join(KIT, 'game', 'ProjectWrath.exe'), '--gf',
                '--oldini', os.path.join(os.path.dirname(here), 'fx', 'old_crash_twoc.ini')], check=True)
open(os.path.join(d, '1 test.cmd'), 'w').write('@echo off\r\ncall "%~dp0kit\\run.cmd" 1_test\r\n')
env = {k: v for k, v in os.environ.items() if not k.startswith('RECOMP_')}
p = subprocess.Popen(['cmd', '/c', os.path.join(d, '1 test.cmd')], cwd='C:\\', env=env, stdin=subprocess.DEVNULL,
                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
u = ctypes.windll.user32
t0 = time.time()
log = None
while time.time() - t0 < 150:
    caps = glob.glob(os.path.join(d, 'captures', '1_test_*'))
    if caps and os.path.exists(os.path.join(caps[0], 'run.err.log')):
        log = os.path.join(caps[0], 'run.err.log')
        if b'MAIN MENU OPEN' in open(log, 'rb').read():
            break
    time.sleep(1)
print('main menu after %.0f s' % (time.time() - t0) if log else 'NO LOG')
time.sleep(3)
h = u.FindWindowW(None, 'Project Wrath')
print('window "Project Wrath":', bool(h))
u.PostMessageW(h, 0x10, 0, 0)
out = p.communicate(timeout=60)[0].decode('utf-8', 'replace')
print(out.strip())
cap = os.path.dirname(log)
print('capture folder:', sorted(os.listdir(cap)))
print([l for l in open(log, 'rb').read().decode('utf-8', 'replace').splitlines() if l.startswith(('[KIT]', '[CFG] ProjectWrath'))])
