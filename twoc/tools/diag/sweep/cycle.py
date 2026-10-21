"""cycle.py TAG MINUTES -- step 73 endurance: from the hub (after newgame2), for MINUTES: put a level on pad 1:1, walk
(or place) Crash onto it, play PLAYSECS (120) s, pause -> WARP ROOM -> YES back to the hub, walk the hub HUBSECS (30) s,
next level. One line per step into runs/TAG.cycle.txt."""
import os, re, struct, subprocess, sys, time
sys.path.insert(0, r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag")
import memsnap
HERE = os.path.dirname(os.path.abspath(__file__))
tag, minutes = sys.argv[1], float(sys.argv[2])
PLAY = float(os.environ.get("PLAYSECS", "120"))
HUB = float(os.environ.get("HUBSECS", "30"))
LEVELS = [7, 13, 11, 17, 4, 8, 34, 12, 18, 2, 10, 14, 3, 0, 15, 5, 20, 26, 6, 30, 1, 19, 16, 31, 33]
if os.environ.get("LEVELS"):            # step 99: e.g. LEVELS=34 enters Sea Shell each time; ENTRIES=N stops after N
    LEVELS = [int(x) for x in os.environ["LEVELS"].split(",")]
ENTRIES = int(os.environ.get("ENTRIES", "0"))
errp = os.path.join(HERE, "runs", tag + ".err.log")
pad = os.path.join(HERE, "runs", tag + ".pad.txt")
logp = os.path.join(HERE, "runs", tag + ".cycle.txt")
h = memsnap.attach(memsnap.find_pid(os.environ.get('TWOC_EXE', 'crash_twoc.exe')))
t0 = time.time()
log = open(logp, "a")


def say(*a):
    s = "%7.1f s  " % (time.time() - t0) + " ".join(str(x) for x in a)
    print(s, flush=True)
    log.write(s + "\n")
    log.flush()


def py(*a):
    return subprocess.run([sys.executable] + list(a), cwd=HERE, capture_output=True, text=True).stdout


def pos():
    return struct.unpack_from("<3f", memsnap.read(h, 0x8F5120, 0x180), 0x6C)


def room_pts(r):
    p = memsnap.u32(h, 0x19C118 + 24 * (r + 1))
    hdr = memsnap.read(h, p, 16)
    n = struct.unpack_from("<h", hdr, 2)[0]
    arr = struct.unpack_from("<I", hdr, 8)[0]
    d = memsnap.read(h, arr, 12 * n)
    return [struct.unpack_from("<3f", d, 12 * i) for i in range(n)]


def press(*k):
    subprocess.run([sys.executable, os.path.join(HERE, "ctl.py"), tag, "press"] + list(k))


def wait_log(pat, p0, secs):
    t = time.time()
    while time.time() - t < secs:
        txt = open(errp, "rb").read()[p0:].decode("latin1")
        m = re.search(pat, txt)
        if m:
            return m
        time.sleep(0.5)
    return None


k = 0
first = True
menu_at = os.path.getsize(errp)
while time.time() - t0 < minutes * 60 and (not ENTRIES or k < ENTRIES):
    txt = open(errp, "rb").read()[menu_at:].decode("latin1")
    if "MAIN MENU OPEN" in txt:         # step 99: at the title (a game over's QUIT, say): a new game, and go on
        say("at the main menu: a new game")
        r = subprocess.run([sys.executable, "newgame2.py", tag], cwd=HERE, capture_output=True, text=True,
                           env=dict(os.environ, NG_FROM=str(menu_at)))
        say("new game", (r.stdout.strip().splitlines() or [r.stderr.strip()[-200:]])[-1])
        menu_at = os.path.getsize(errp)
        first = True
        continue
    lv = LEVELS[k % len(LEVELS)]
    k += 1
    p0 = os.path.getsize(errp)
    if first:
        r = py("gopad2.py", tag, str(lv), "1:1")
        say("gopad", lv, r.strip().splitlines()[-1:] if r.strip() else "")
        first = False
    else:
        py("poke.py", "%X" % 0x19A7F0, "1", str(lv))
        pts = room_pts(1)
        px, _, pz = pts[3]
        r = py("steer.py", tag, "%.2f" % px, "%.2f" % pz, "10")
        x, y, z = pos()
        if (x - px) ** 2 + (z - pz) ** 2 > 0.5:
            fx = struct.unpack("<I", struct.pack("<f", px))[0]
            fz = struct.unpack("<I", struct.pack("<f", pz))[0]
            py("poke.py", "%X" % (0x8F5120 + 0x6C), "4", str(fx), "%X" % (0x8F5120 + 0x74), "4", str(fz))
            say("placed on pad from (%.1f %.1f)" % (x, z))
    m = wait_log(r"levels\\[ac]\\(\w+)\\\w+\.nux", p0, 60)
    say("level", lv, "loading", m.group(1) if m else "NOTHING")
    if not m:
        say("no load; walking the hub again")
        continue
    time.sleep(20)
    r = py("play.py", tag, str(PLAY), "60")
    say("play", (r.strip().splitlines() or ["?"])[0])
    p1 = os.path.getsize(errp)
    for attempt in range(3):
        press("start", "wait:4", "down", "down", "a", "wait:1", "up", "a")
        if wait_log(r"levels\\b\\hub\\hub\.nux", p1, 25):
            break
        say("no hub yet, pause again (attempt %d)" % (attempt + 1))
        press("b", "wait:1", "b", "wait:1")
    else:
        if "MAIN MENU OPEN" in open(errp, "rb").read()[menu_at:].decode("latin1"):
            continue                    # step 99: random play reached the title: a new game at the loop's top
        say("FAILED to return to the hub")
        break
    time.sleep(10)
    say("hub", "pos (%.1f %.1f %.1f)" % pos())
    time.sleep(HUB)          # idle in the hub (random play there walks onto pads and the Load/Save console)
say("end after %d levels" % k)
