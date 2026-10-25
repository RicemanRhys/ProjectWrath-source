"""smoke104.py PACKAGE OUTDIR [SIZES] -- step 104 Part 9: the package from a scratch copy in a folder with spaces,
started by its "Play Crash.bat" (working directory C:\\, its own console) at each render size: the startup message
grabbed, F1, every tab grabbed going right with RB (an XInput stand-in pad), the [OVL] rect lines per tab, Menu
scale changed live by the D-pad (found by pressing down then right until the log says the scale changed), photo
mode and the freeze toggled (F3, F2), then the window closed; the bat's "[BAT] ... ended with code" line."""
import ctypes, os, re, shutil, subprocess, sys, time
from ctypes import wintypes

pkg, out = sys.argv[1], sys.argv[2]
sizes = sys.argv[3].split(",") if len(sys.argv) > 3 else ["1280x800", "1920x1080", "3840x2160"]
WGC = r"C:\Users\User\Desktop\Desktop Stuff\Claude Stuff\Crash TWOC decomp\twoc\tools\diag\wgcgrab.exe"
NEW = os.path.exists(os.path.join(pkg, "ProjectWrath.exe"))      # step 109: the names follow the package's exe
TITLE = "Project Wrath" if NEW else "Crash TWOC - D3D bridge (host)"
u = ctypes.windll.user32
os.makedirs(out, exist_ok=True)
copy = os.path.join(out, "pkg copy")
if os.path.exists(copy):
    shutil.rmtree(copy)
shutil.copytree(pkg, copy)
xi = os.path.join(out, "xi.txt")
log = os.path.join(copy, "ProjectWrath.err.log" if NEW else "run_twoc.err.log")
report = open(os.path.join(out, "smoke.txt"), "w")

def say(s):
    print(s, flush=True)
    report.write(s + "\n")
    report.flush()

def hwnd():
    return u.FindWindowW(None, TITLE)

def key(vk, sc):
    h = hwnd()
    u.PostMessageW(h, 0x100, vk, 1 | (sc << 16))
    time.sleep(0.05)
    u.PostMessageW(h, 0x101, vk, 0xC0000001 | (sc << 16))
    time.sleep(0.4)

def pad(line, hold=0.15, after=0.45):
    open(xi, "w").write(line + "\n")
    time.sleep(hold)
    open(xi, "w").write("\n")
    time.sleep(after)

def grab(name):
    subprocess.run([WGC, TITLE, os.path.join(out, name), "0"], capture_output=True)

def text():
    try:
        return open(log, "rb").read().decode("utf-8", "replace")
    except OSError:
        return ""

for rs in sizes:
    tag = rs
    open(xi, "w").write("\n")
    env = dict(os.environ)
    env.update(RECOMP_RENDER_SIZE=rs, RECOMP_DEV_OVL_RECT="1", RECOMP_DEV_XINPUT_FILE=xi)
    if os.path.exists(log):
        os.remove(log)
    p = subprocess.Popen(["cmd", "/c", os.path.join(copy, "Play Project Wrath.bat" if NEW else "Play Crash.bat")], cwd="C:\\", env=env,
                         creationflags=subprocess.CREATE_NEW_CONSOLE)
    t0 = time.time()
    while not hwnd() and time.time() - t0 < 60:
        time.sleep(0.2)
    time.sleep(2.5)
    grab(tag + "_startmsg")
    while "MAIN MENU OPEN" not in text() and time.time() - t0 < 90:
        time.sleep(0.5)
    time.sleep(3)
    key(0x70, 0x3B)                     # F1
    time.sleep(1.5)
    grab(tag + "_tab0")
    for i in range(1, 8):
        pad("white=255")
        time.sleep(0.5)
        grab(tag + "_tab%d" % i)
    # Menu scale: down, then right, until the log says the scale changed
    found = 0
    for n in range(1, 30):
        before = len(re.findall(r"\[OVL\] menu scale: \d+%", text()))
        pad("buttons=0x2")              # D-pad down
        pad("buttons=0x8")              # D-pad right
        time.sleep(0.3)
        if len(re.findall(r"\[OVL\] menu scale: \d+%", text())) > before:
            found = n
            break
    if found:
        pad("buttons=0x8")
        pad("buttons=0x8")
        time.sleep(1.0)
        grab(tag + "_scale_up")
        for i in range(3):
            pad("buttons=0x4")
        time.sleep(1.0)
    key(0x70, 0x3B)                     # F1 closes
    time.sleep(1.0)
    key(0x72, 0x3D)                     # F3 photo mode
    time.sleep(1.5)
    grab(tag + "_photo")
    key(0x71, 0x3C)                     # F2 freeze
    time.sleep(1.5)
    grab(tag + "_frozen")
    key(0x71, 0x3C)
    time.sleep(1.0)
    key(0x72, 0x3D)
    time.sleep(1.0)
    u.PostMessageW(hwnd(), 0x10, 0, 0)  # WM_CLOSE
    try:
        p.wait(timeout=30)
    except subprocess.TimeoutExpired:
        say("console still open after 30 s")
    time.sleep(2)
    t = text()
    rects = {}
    for l in t.splitlines():
        m = re.search(r"\[OVL\] rect: tab (\d+) at ([\d.]+),([\d.]+) size ([\d.]+) x ([\d.]+) \(", l)
        if m and "not shown" not in l:
            rects.setdefault(m.group(1), set()).add(m.group(2, 3, 4, 5))
    allr = set().union(*rects.values()) if rects else set()
    say("== %s: tabs seen %s; rectangles %s (%s)" % (rs, sorted(rects), len(allr), "the same on every tab" if len(allr) == 1 else allr))
    scales = re.findall(r"\[OVL\] menu scale: (\d+)%", t)
    say("   menu scale changed live: %s (after %d downs)" % (" -> ".join(scales) if scales else "NO", found))
    say("   startup message line: %s" % ("yes" if "host startup message" in t else "NO"))
    for l in t.splitlines():
        if "[PHOTO] photo mode" in l or "[PHOTO] freeze" in l or "[PHOTO] frozen" in l:
            say("   " + l.strip())
    bat = [l for l in t.splitlines() if "[BAT]" in l and "ended with code" in l]
    say("   " + (bat[-1].strip() if bat else "no [BAT] line"))
for f in os.listdir(out):
    if f.endswith(".bmp"):
        from PIL import Image
        im = Image.open(os.path.join(out, f))
        im.resize((im.width // 2, im.height // 2)).save(os.path.join(out, f[:-4] + ".png"))
        os.remove(os.path.join(out, f))
say("done")
