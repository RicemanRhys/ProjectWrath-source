"""rs91chk.py TAG -- each rs91.py step against the log: the picture drawn and shown after the step's trigger
(the "host picture A -> B" lines; none: unchanged) and the grab's size, against the expected sizes."""
import os, re, struct, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
runs = os.path.join(HERE, "runs")
tag = sys.argv[1]
steps = [l.split() for l in open(os.path.join(runs, tag + ".steps.txt"))]
log = open(os.path.join(runs, tag + ".err.log"), "rb").read().decode("utf-8", "replace").splitlines()

drawn = shown = None
for l in log:
    m = re.search(r"host device (\d+)x(\d+)", l)
    if m and drawn is None:
        drawn = shown = (int(m.group(1)), int(m.group(2)))
segs, cur = [], None
fails = []
for l in log:
    if "trigger file: settings changed" in l or "host sub-frame dumps: guest swaps" in l:   # the latter: a trigger read empty (lost)
        if cur is not None:
            segs.append(cur)
        cur = []
    elif cur is not None and ("host picture" in l or "not resized" in l):
        cur.append(l)
if cur is not None:
    segs.append(cur)


def png_size(path):
    b = open(path, "rb").read(32)
    return struct.unpack(">II", b[16:24])


ok = bad = 0
grabs = {f.split("_000")[0][len(tag) + 1:]: f for f in os.listdir(runs) if f.startswith(tag + "_") and f.endswith(".png")}
for i, st in enumerate(steps):
    name, asp, size, pct, exp, exps = st
    for l in segs[i] if i < len(segs) else []:
        m = re.search(r"host picture (\d+)x(\d+) -> (\d+)x(\d+)(?:.*shown at (\d+)x(\d+))?", l)
        if m:
            drawn = (int(m.group(3)), int(m.group(4)))
            shown = (int(m.group(5)), int(m.group(6))) if m.group(5) else drawn
        if "not resized" in l:
            fails.append((name, l.strip()))
    ew, eh = map(int, exp.split("x"))
    sw, sh = map(int, exps.split("x"))
    g = grabs.get(name)
    gs = "%dx%d" % png_size(os.path.join(runs, g)) if g else "no grab"
    good = drawn == (sw, sh) and (shown == (ew, eh) or shown == drawn == (sw, sh) and pct == "100")
    ok += good
    bad += not good
    print("%-34s %-5s %-9s %3s%%  expected drawn %-11s shown %-10s log drawn %-11s shown %-10s grab %-10s %s" % (
        name, asp, size, pct, exps, exp, "%dx%d" % drawn, "%dx%d" % shown if shown else "-", gs, "ok" if good else "DIFFERS"))
print("%d ok, %d differ; %d trigger segments for %d steps" % (ok, bad, len(segs), len(steps)))
for f in fails:
    print("not resized:", f)
