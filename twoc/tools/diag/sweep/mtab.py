"""mtab.py -- step 104: the F1 menu's tabs at a render size, by the XInput stand-in (LB / RB), grabbed.

  python mtab.py TAG EXEDIR RS [STEPS] [ENV=VAL ...]

Launches EXEDIR/crash_twoc.exe host-only with the fast title and RECOMP_DEV_OVL_RECT=1 in root/ (no ini beside a
scratch exe unless the folder has one), waits for the main menu, opens F1, grabs every tab going right with RB, then
LB once from the first tab (wrap to the last), closes with F1, and prints the [OVL] rect lines.
STEPS (default "tabs"): a ';' list of: tabs | f1 | esc | rb | lb | a | b | up | down | left | right | wait:S |
grab:NAME | key:VK (posted key down/up) | x:SCRIPT ARGS (python, args split on ',').
"""
import os, subprocess, sys, time, ctypes

HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable
WGC = os.environ.get("WGCGRAB", r"C:\Users\User\AppData\Local\Temp\claude\C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp\34018395-629f-40f9-9d4d-f55117e9c429\scratchpad\bin\wgcgrab.exe")
TITLE = "Crash TWOC - D3D bridge (host)"
tag, exedir, rs = sys.argv[1], sys.argv[2], sys.argv[3]
rest = sys.argv[4:]
steps = "tabs"
if rest and "=" not in rest[0].split(";")[0].split(":")[0]:
    steps = rest[0]
    rest = rest[1:]
extra = dict(a.split("=", 1) for a in rest)
runs = os.path.join(HERE, "runs")
os.makedirs(runs, exist_ok=True)
pad = os.path.join(runs, tag + ".pad.txt")
xi = os.path.join(runs, tag + ".xi.txt")
open(pad, "w").write("\n")
open(xi, "w").write("\n")
env = {k: v for k, v in os.environ.items() if not k.startswith("RECOMP_")}
env.update(RECOMP_VBLANK="1", RECOMP_AC97_READY="1", RECOMP_WATCHDOG_SECS=extra.pop("SECS", "240"),
           RECOMP_DEV_FAST_TITLE="1", RECOMP_DEV_INPUT_FILE=pad, RECOMP_DEV_XINPUT_FILE=xi, RECOMP_D3D_BRIDGE="1",
           RECOMP_DEV_OVL_RECT="1", RECOMP_RENDER_SIZE=rs)
env.update(extra)
exe = os.path.join(exedir, "crash_twoc.exe")
p = subprocess.Popen([exe], cwd=os.path.join(HERE, "root"), env=env,
                     stdout=open(os.path.join(runs, tag + ".out.log"), "wb"),
                     stderr=open(os.path.join(runs, tag + ".err.log"), "wb"))
u = ctypes.windll.user32

def hwnd():
    return u.FindWindowW(None, TITLE)

def post(vk, sc=0):
    h = hwnd()
    u.PostMessageW(h, 0x0100, vk, 0x00000001 | (sc << 16))
    time.sleep(0.05)
    u.PostMessageW(h, 0x0101, vk, 0xC0000001 | (sc << 16))

def xpad(line, hold=0.25, after=0.9):
    open(xi, "w").write(line + "\n")
    time.sleep(hold)
    open(xi, "w").write("\n")
    time.sleep(after)

def grab(name):
    subprocess.run([WGC, TITLE, os.path.join(runs, tag + "_" + name), "0"], capture_output=True)
    b = os.path.join(runs, tag + "_" + name + "_000.0s.bmp")
    if os.path.exists(b):
        subprocess.run([PY, os.path.join(HERE, "topng.py"), b], capture_output=True)
        os.remove(b)

t0 = time.time()
while not hwnd() and time.time() - t0 < 60:
    time.sleep(0.5)
# the main menu: the fast title opens it ~6 s after the window
log = os.path.join(runs, tag + ".err.log")
while time.time() - t0 < 40:
    try:
        if b"MAIN MENU OPEN" in open(log, "rb").read():
            break
    except OSError:
        pass
    time.sleep(0.5)
time.sleep(float(extra.get("MENUWAIT", "3")))
NAMES = ["display", "graphics", "audio", "controls", "saves", "extras", "about"]
for st in steps.split(";"):
    st = st.strip()
    if not st:
        continue
    k, _, v = st.partition(":")
    if k == "tabs":
        post(0x70, 0x3B)
        time.sleep(2.0)
        grab("t0_" + NAMES[0])
        for i in range(1, 8):
            xpad("white=255")
            time.sleep(0.6)
            grab("t%d_%s" % (i, NAMES[i % 7]))
        xpad("black=255")
        time.sleep(0.6)
        grab("lb_" + NAMES[6])
        post(0x70, 0x3B)
        time.sleep(1.0)
    elif k == "f1": post(0x70, 0x3B); time.sleep(1.5)
    elif k == "esc": post(0x1B, 0x01); time.sleep(1.0)
    elif k == "rb": xpad("white=255")
    elif k == "lb": xpad("black=255")
    elif k == "a": xpad("a=255")
    elif k == "b": xpad("b=255")
    elif k in ("up", "down", "left", "right"):
        xpad("buttons=0x%x" % {"up": 1, "down": 2, "left": 4, "right": 8}[k], hold=0.15, after=0.35)
    elif k == "wait": time.sleep(float(v))
    elif k == "grab": grab(v)
    elif k == "key": post(int(v, 0)); time.sleep(0.4)
    elif k == "x": subprocess.run([PY, os.path.join(HERE, v.split(",")[0])] + v.split(",")[1:])
time.sleep(1.0)
p.kill()
p.wait()
time.sleep(0.5)
for l in open(log, "rb").read().decode("utf-8", "replace").splitlines():
    if "[OVL]" in l and ("rect" in l or "asks" in l or "reset" in l.lower()) and "not shown" not in l:
        print(l)
print("done", tag)
