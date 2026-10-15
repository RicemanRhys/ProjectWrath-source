"""summ.py TAG... -- one line per sweep run: the level files loaded after the hub, the play summary, CRASH lines,
the exit code and interpolation state."""
import os, re, sys, glob
HERE = os.path.dirname(os.path.abspath(__file__))
R = os.path.join(HERE, "runs")


def rd(p):
    try:
        return open(p, "rb").read().decode("latin1")
    except OSError:
        return ""


tags = sys.argv[1:]
if len(tags) == 1 and "*" in tags[0]:
    tags = sorted({os.path.basename(p)[:-8] for p in glob.glob(os.path.join(R, tags[0] + ".err.log"))})
for t in tags:
    err = rd(os.path.join(R, t + ".err.log"))
    lv = re.findall(r"levels\\([a-z])\\(\w+)\\(\w+)\.nux", err)
    after = []
    seen_hub = False
    for l in lv:
        if l[1] in ("hub", "pause", "titles", "logos", "intro", "intro2") and l[0] == "b":
            continue
        if True and (not after or after[-1] != l[1] + "/" + l[2]):
            after.append(l[1] + "/" + l[2])
    play = [l for l in (rd(os.path.join(R, t + ".play.txt")) + rd(os.path.join(R, t + ".act.txt"))).splitlines() if l.startswith("summary")]
    m = re.search(r"frames (\d+) in ([\d.]+) s \(([\d.]+)/s\), longest stall ([\d.]+) s, moved ([\d.]+) units.*crash lines (\d+), alive (\w+)", play[0]) if play else None
    crash = len(re.findall(r"\[CRASH", err))
    ex = re.findall(r"exit (-?\d+)", rd(os.path.join(R, t + ".launch.txt")))
    interp = "on" if "interpolation on" in err else ("OFF" if "interpolation off" in err else "-")
    gaps = [float(x) for x in re.findall(r"swap gaps this interval: .*longest ([\d.]+) ms", err)]
    print("%-8s loads %-40s play %s crash %d exit %s interp %s maxgap %.0f ms" % (
        t, ",".join(after)[:40] or "-",
        ("%s fr %s/s stall %ss moved %s alive %s" % (m.group(1), m.group(3), m.group(4), m.group(5), m.group(7))) if m else "NONE",
        crash, ex[-1] if ex else "?", interp, max(gaps) if gaps else 0))
