"""upst.py TAG -- step 74: from the hub spawn, ride the central lift upstairs (warp room 6's floor, the heaviest
room of steps 60-62) and stay there."""
import os, struct, subprocess, sys, time
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
HERE = os.path.dirname(os.path.abspath(__file__))
tag = sys.argv[1]
h = memsnap.attach(memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe')))


def pos():
    return struct.unpack_from("<3f", memsnap.read(h, 0x8F5120, 0x180), 0x6C)


print("start", pos(), flush=True)
r = subprocess.run([sys.executable, "steer.py", tag, "0", "0", "12"], cwd=HERE, capture_output=True, text=True).stdout
print("lift", r.strip().splitlines()[-1:] if r.strip() else "", flush=True)
t0 = time.time()
while time.time() - t0 < 30 and pos()[1] < 30:
    time.sleep(0.5)
print("upstairs" if pos()[1] >= 30 else "NOT upstairs", pos(), "after %.0f s" % (time.time() - t0), flush=True)
