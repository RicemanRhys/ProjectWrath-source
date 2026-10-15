"""adsum.py TAG... -- step 74: per run, the level, play fps, and after the level load: game frames by sub-frames
allowed, changes, mean ms apart/busy."""
import glob, os, re, sys
R = os.path.join(os.path.dirname(os.path.abspath(__file__)), "runs")
tags = sys.argv[1:]
if len(tags) == 1 and "*" in tags[0]:
    tags = sorted({os.path.basename(p)[:-8] for p in glob.glob(os.path.join(R, tags[0] + ".err.log"))})
for t in tags:
    e = open(os.path.join(R, t + ".err.log"), "rb").read().decode("latin1")
    lv = [m for m in re.findall(r"levels\\[a-z]\\(\w+)\\\w+\.nux", e) if m not in ("hub", "pause", "titles", "logos", "intro", "intro2", "gameover")]
    name = lv[0] if lv else "-"
    i = e.find("\\" + name + "\\") if lv else 0
    tail = e[i:]
    at = [0, 0, 0, 0, 0]
    ch = 0; nf = 0; dt = 0.0; bu = 0.0
    for m in re.finditer(r"allowed 4: (\d+), 3: (\d+), 2: (\d+), 1: (\d+); changes (\d+); measured frames (\d+), ([\d.]+) ms apart, ([\d.]+) ms", tail):
        at[4] += int(m.group(1)); at[2] += int(m.group(3)); at[1] += int(m.group(4)); ch += int(m.group(5))
        n = int(m.group(6)); nf += n; dt += n * float(m.group(7)); bu += n * float(m.group(8))
    chl = re.findall(r"(\d) frames each game frame from guest swap \d+ \(was (\d)\)", tail)
    play = ""
    try:
        pl = open(os.path.join(R, t + ".play.txt")).read()
        m = re.search(r"\(([\d.]+)/s\)", pl)
        play = m.group(1) if m else ""
    except OSError:
        pass
    print("%-6s %-10s play %5s/s  frames at 4:%5d 2:%5d 1:%5d  changes %d %s  %.2f ms apart, %.2f busy" % (
        t, name, play, at[4], at[2], at[1], ch, " ".join(b + ">" + a for a, b in chl)[:60], dt / nf if nf else 0, bu / nf if nf else 0))
