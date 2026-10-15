"""newgame2.py TAG (step 64: also grabs the menu, and the intro at INTRO_GRABS=s,s,... seconds) -- for a launch.py run: at the main menu, NEW GAME named WOMBAT
(every area open), skip the intro, and stop once the hub has loaded."""
import os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
tag = sys.argv[1]
errp = os.path.join(HERE, "runs", tag + ".err.log")


def seen(s):
    try:
        return s.encode() in open(errp, "rb").read()
    except OSError:
        return False


def wait(s, secs):
    t0 = time.time()
    while time.time() - t0 < secs:
        if seen(s):
            return True
        time.sleep(0.5)
    return False


def press(*k):
    subprocess.run([sys.executable, os.path.join(HERE, "ctl.py"), tag, "press"] + list(k))


assert wait("MAIN MENU OPEN", 120), "no menu"
time.sleep(1)
subprocess.run([sys.executable, os.path.join(HERE, "ctl.py"), tag, "grab", tag + "_menu"])
press("start", "wait:1.5")
# name entry: cursor on DONE; up -> V. The first letter replaces the default name.
press("up", "right", "a", "up", "left", "a", "up", "right*5", "a", "up", "left*4", "a",
      "left", "a", "down*2", "right*5", "a", "wait:0.5")
subprocess.run([sys.executable, os.path.join(HERE, "ctl.py"), tag, "grab", tag + "_name"])
press("down*2", "a")
assert wait("INTRO1.wav", 120), "no intro"
t0 = time.time()
for t in [float(x) for x in os.environ.get("INTRO_GRABS", "5").split(",")]:
    time.sleep(max(0, t - (time.time() - t0)))
    subprocess.run([sys.executable, os.path.join(HERE, "ctl.py"), tag, "grab", tag + "_intro%03d" % t])
press("start")
assert wait("THEMEL.wav", 120), "no hub"
time.sleep(4)
print("hub ready")
