"""apusum.py LOG... -- step 108: one line a run from its [APU-OUT] lines: intervals, underruns, dropped, the frame
rate's range, late wakeups (over 3 / 20 ms), frames over 2 / 4 frames apart, the longest gap; and the bridge's
longest swap gap."""
import re, sys, glob
for pat in sys.argv[1:]:
    for p in sorted(glob.glob(pat)):
        t = open(p, encoding="utf-8", errors="replace").read()
        iv = re.findall(r"\[APU-OUT\] [\d.]+ s: (\d+) frames of 256 \((\d+) Hz\), (\d+) underruns, (\d+) dropped", t)
        wk = re.findall(r"\[APU-OUT\] wakeups: \d+ waits for room \(asked 1 ms\), (\d+) back over 3 ms late, (\d+) over 20 ms.*?"
                        r"over 2 frames \(10.7 ms\) after the one before, (\d+) over 4 \(21.3 ms\), the longest ([\d.]+) ms", t)
        sg = [float(x) for x in re.findall(r"host swap gaps this interval: .*?longest ([\d.]+) ms", t)]
        hz = [int(x[1]) for x in iv]
        print("%-28s %2d iv, underruns %3d, dropped %d, %s-%s Hz, late>3ms %5d, >20ms %d, >4fr %d, longest %s ms; swap gap max %s" % (
            p.split("\\")[-1].split("/")[-1][:28], len(iv), sum(int(x[2]) for x in iv), sum(int(x[3]) for x in iv),
            min(hz) if hz else "-", max(hz) if hz else "-", sum(int(w[0]) for w in wk), sum(int(w[1]) for w in wk),
            sum(int(w[2]) for w in wk), max((float(w[3]) for w in wk), default=0), max(sg, default=0)))
