"""mt90.py TAG [launch.py args...] -- step 90: launch.py TAG, time from the launch to "[GAME] MAIN MENU OPEN" in
runs/TAG.err.log, then end the game. Prints the seconds and the log's fast-title lines."""
import os, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
tag = sys.argv[1]
log = os.path.join(HERE, 'runs', tag + '.err.log')
if os.path.exists(log):
    os.remove(log)
t0 = time.time()
p = subprocess.Popen([sys.executable, os.path.join(HERE, 'launch.py'), tag, '90'] + sys.argv[2:],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
t = None
while time.time() - t0 < 85:
    time.sleep(0.1)
    try:
        if 'MAIN MENU OPEN' in open(log, encoding='latin1').read():
            t = time.time() - t0
            break
    except OSError:
        pass
time.sleep(2)
subprocess.run(['taskkill', '/F', '/IM', 'crash_twoc.exe'], capture_output=True)
p.wait()
txt = open(log, encoding='latin1').read() if os.path.exists(log) else ''
lines = [l for l in txt.splitlines() if re.search(r'fast_title|FAST_TITLE|main menu opened', l)]
print('%s: menu %s s after the launch; %s' % (tag, '%.1f' % t if t else 'not open', ' | '.join(lines[:3])), flush=True)
