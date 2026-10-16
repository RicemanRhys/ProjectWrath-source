"""iv82.py TAG... -- step 82: per run, the adaptive lines (drops, rises, restarts) and one row per 10-s interval
from the level load on: guest swaps a second, allowed steps, changes, ms apart, busy, game side, replay side,
frames shown at 1 refresh."""
import glob, os, re, sys
R = os.path.join(os.path.dirname(os.path.abspath(__file__)), "runs")
tags = sys.argv[1:]
if len(tags) == 1 and "*" in tags[0]:
    tags = sorted({os.path.basename(p)[:-8] for p in glob.glob(os.path.join(R, tags[0] + ".err.log"))})
for t in tags:
    lines = open(os.path.join(R, t + ".err.log"), "rb").read().decode("latin1").splitlines()
    print("==", t)
    for l in lines:
        if re.search(r"frame rate plan:|test aid|render thread: on from|render thread: off|adaptive rate starts again|"
                     r"host interpolation: [\d.]+ fps, |back to the plan|CRASH|frame slots", l):
            print("  " + l[:230])
    rows = []
    cur = {}
    lvl = False
    for l in lines:
        if re.search(r"levels\\[a-z]\\(?!hub|pause|titles|logos|intro|gameover)\w+\\", l):
            lvl = True
        m = re.match(r"\[D3DB\] host \+([\d.]+) s: guest swaps \d+ \(\+\d+, ([\d.]+)/s\)", l)
        if m:
            cur = {"t": float(m.group(1)), "sps": float(m.group(2)), "lvl": lvl}
            rows.append(cur)
        m = re.search(r"interpolation sub-frames: game frames allowed (.*); changes (\d+); measured frames (\d+), ([\d.]+) ms apart, "
                      r"([\d.]+) ms of it busy \(the game side ([\d.]+), the replay side ([\d.]+)\)", l)
        if m and cur:
            al = re.findall(r"([\d.]+) fps: (\d+)", m.group(1))
            cur.update(al=" ".join("%s:%s" % (int(float(a) + 0.5), b) for a, b in al if b != "0"), ch=int(m.group(2)),
                       n=int(m.group(3)), dt=float(m.group(4)), busy=float(m.group(5)), g=float(m.group(6)),
                       r=float(m.group(7)))
        m = re.search(r"frames shown this interval, by refreshes since the last shown: 1 (\d+), .*unmeasured (\d+)", l)
        if m and cur:
            cur["s1"] = int(m.group(1)); cur["um"] = int(m.group(2))
    for c in rows:
        if not c["lvl"] or "n" not in c:
            continue
        print("  +%5.0f s %5.1f/s  %-22s ch %d  n %4d  %6.2f apart  busy %6.2f  game %6.2f  replay %6.2f  shown@1 %s" % (
            c["t"], c["sps"], c.get("al", ""), c["ch"], c["n"], c["dt"], c["busy"], c["g"], c["r"], c.get("s1", "-")))
