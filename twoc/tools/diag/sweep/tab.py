"""tab.py TAG... -- step 76: one column per run of the step-75 table (ms a game frame, k=4 frames only), over the
last 1000 swaps of each FPROF CSV less the final 30 (the measured stretch: 22 s at 45 fps)."""
import csv, os, statistics, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def q(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(p * len(v)))] if v else 0.0


def summ(tag):
    rows = [r for r in csv.DictReader(open(os.path.join(HERE, "runs", tag + ".fprof.csv")))
            if r.get("post4") not in (None, "")]
    last = int(rows[-1]["swap"])
    rows = [r for r in rows if last - 1000 <= int(r["swap"]) <= last - 30 and r["k"] == "4"
            and float(r["game_wall"]) > 0]
    f = lambda r, c: float(r[c])
    m = lambda fn: statistics.mean(fn(r) for r in rows)
    wall = [f(r, "game_wall") + f(r, "present_wall") for r in rows]
    busy = [w - sum(f(r, "pace%d" % i) for i in range(1, 5)) for w, r in zip(wall, rows)]
    return {
        "frames (k=4)": "%d" % len(rows),
        "commands": "%.0f" % m(lambda r: f(r, "cmds")),
        "game (guest code)": "%.2f" % m(lambda r: f(r, "game_wall") - f(r, "mirror_draw") - f(r, "mirror_other")),
        "mirror (d3d_bridge)": "%.2f" % m(lambda r: f(r, "mirror_draw") + f(r, "mirror_other") - f(r, "rec")),
        "recording": "%.2f" % m(lambda r: f(r, "rec")),
        "pairing": "%.2f" % m(lambda r: f(r, "ia") + f(r, "begin")),
        "4 replays": "%.2f (%s)" % (m(lambda r: sum(f(r, "rp%d" % i) for i in range(1, 5))),
                                    "/".join("%.2f" % m(lambda r, i=i: f(r, "rp%d" % i)) for i in range(1, 5))),
        "4 presents": "%.2f" % m(lambda r: sum(f(r, "copy%d" % i) + f(r, "pres%d" % i) + f(r, "post%d" % i)
                                              for i in range(1, 5))),
        "pace wait": "%.2f" % m(lambda r: sum(f(r, "pace%d" % i) for i in range(1, 5))),
        "frame (p90)": "%.2f (%.2f), %.1f fps" % (statistics.mean(wall), q(wall, .9), 1000 / statistics.mean(wall)),
        "busy (p90)": "%.2f (%.2f)" % (statistics.mean(busy), q(busy, .9)),
    }


tags = sys.argv[1:]
cols = [summ(t) for t in tags]
print("| | " + " | ".join(tags) + " |")
print("|---" * (len(tags) + 1) + "|")
for k in cols[0]:
    print("| %s | %s |" % (k, " | ".join(c[k] for c in cols)))
