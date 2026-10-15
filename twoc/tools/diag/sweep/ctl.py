"""Drive a launch.py run.

  python ctl.py TAG press KEY [KEY ...]     tap each key (a b x y start back up down left right) 120 ms, 250 ms apart
                                            KEY*N repeats; "wait:S" sleeps S seconds
  python ctl.py TAG grab NAME               bridge-window screenshot -> runs/NAME_*.png
  python ctl.py TAG heap [N]                last N [HEAPSTAT] lines (default 1)
  python ctl.py TAG log [PATTERN]           new [PATH]/[GAME]/CRASH lines (or PATTERN) since the last 'log'
"""
import os, re, subprocess, sys, time, glob

HERE = os.path.dirname(os.path.abspath(__file__))
WGC = os.environ.get("WGCGRAB", r"C:\Users\User\AppData\Local\Temp\claude\C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp\34018395-629f-40f9-9d4d-f55117e9c429\scratchpad\bin\wgcgrab.exe")  # wgcgrab.exe (../wgcgrab_build.bat)
tag, cmd = sys.argv[1], sys.argv[2]
out = os.path.join(HERE, "runs")
pad = os.path.join(out, tag + ".pad.txt")
errp = os.path.join(out, tag + ".err.log")
KEYS = {"up": "buttons=0x1", "down": "buttons=0x2", "left": "buttons=0x4", "right": "buttons=0x8",
        "start": "buttons=0x10", "back": "buttons=0x20", "a": "a=255", "b": "b=255", "x": "x=255", "y": "y=255",
        "lt": "lt=255", "rt": "rt=255", "white": "white=255", "black": "black=255"}

if cmd == "press":
    for k in sys.argv[3:]:
        if k.startswith("wait:"):
            time.sleep(float(k[5:]))
            continue
        n = 1
        if "*" in k:
            k, n = k.split("*")
            n = int(n)
        for _ in range(n):
            open(pad, "w").write(KEYS[k] + "\n")
            time.sleep(0.12)
            open(pad, "w").write("\n")
            time.sleep(0.25)
elif cmd == "grab":
    name = sys.argv[3]
    subprocess.run([WGC, "Crash TWOC - D3D bridge (host)", os.path.join(out, name), "0"], timeout=60)
    for b in glob.glob(os.path.join(out, name + "_*.bmp")):
        subprocess.run([sys.executable, os.path.join(HERE, "topng.py"), b])
        print(b[:-4] + ".png")
elif cmd == "heap":
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    lines = [l for l in open(errp, "rb").read().decode("latin1").splitlines() if "[HEAPSTAT]" in l]
    for l in lines[-n:]:
        print(l.strip())
elif cmd == "log":
    pat = sys.argv[3] if len(sys.argv) > 3 else r"\[PATH\].*levels|\[GAME\]|CRASH\]|\[CRASH|out of memory"
    mark = os.path.join(out, tag + ".logpos")
    pos = int(open(mark).read()) if os.path.exists(mark) else 0
    with open(errp, "rb") as f:
        f.seek(pos)
        data = f.read()
    open(mark, "w").write(str(pos + len(data)))
    for l in data.decode("latin1").splitlines():
        if re.search(pat, l):
            print(l.strip())
