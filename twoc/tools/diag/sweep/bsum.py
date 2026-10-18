"""bsum.py PREFIX [N] -- step 87: mm.sh's runs PREFIX<round><where>_<config> as one table. For each place and config,
over the rounds (each run's last N game frames, default 1200, less the last 30): the game thread's ms a frame (mean,
p90), the render thread's ms a game frame (TPROF's replay column: its replays and presents less the pace waits; mean,
p90), the presents a game frame got, the draws recorded a frame, the render thread's us a draw and pass (replay ms /
(draws x presents)), game updates a second, and the baked-draw line's counts from the log's last interval."""
import collections, csv, glob, os, re, statistics, sys

HERE = os.path.dirname(os.path.abspath(__file__))
pre = sys.argv[1]
N = int(sys.argv[2]) if len(sys.argv) > 2 else 1200
WANT = {"cb": "cast_bug", "av": "avalanch", "up": "hub"}
runs = collections.defaultdict(list)
for p in glob.glob(os.path.join(HERE, "runs", pre + "*.gprof.csv")):
    tag = os.path.basename(p)[:-len(".gprof.csv")]
    m = re.match(re.escape(pre) + r"(\d)(cb|av|up)_(.*)$", tag)
    if m:
        runs[(m.group(2), m.group(3))].append(tag)


def p90(v):
    v = sorted(v)
    return v[int(0.9 * (len(v) - 1))] if v else 0.0


def one(tag):
    lv, bake = "", ""
    log = os.path.join(HERE, "runs", tag + ".err.log")
    for l in open(log, encoding="utf-8", errors="replace"):
        if "Crashdat\\levels\\" in l:
            lv = l
        if "host baked draws:" in l and "captured" in l:
            bake = l.split("draws: ", 1)[1].strip()
    if WANT[tag.split("_")[0][-2:]] not in lv:
        print("  (left out: %s ended in %s)" % (tag, lv.strip().split("\\")[-1] if lv else "no level"))
        return None
    rows = list(csv.DictReader(open(os.path.join(HERE, "runs", tag + ".gprof.csv"))))
    rows = rows[-(N + 30):-30]
    if len(rows) < 200:
        return None
    g = [float(r["game"]) for r in rows]
    dr = {int(r["swap"]): int(r["rec_draws"]) for r in rows}
    s0, s1 = int(rows[0]["swap"]), int(rows[-1]["swap"])
    tr = [r for r in csv.DictReader(open(os.path.join(HERE, "runs", tag + ".tprof.csv")))
          if s0 <= int(r["swap"]) <= s1 and r["thread"] == "1"]
    rp = [float(r["replay"]) for r in tr]
    pres = [int(r["presents"]) for r in tr]
    dts = [float(r["dt"]) for r in tr if float(r["dt"]) > 0]
    per = []
    for r in tr:
        d, k = dr.get(int(r["swap"]) - 1, 0) or dr.get(int(r["swap"]), 0), int(r["presents"])
        if d and k:
            per.append(1000.0 * float(r["replay"]) / (d * k))
    return dict(g=statistics.mean(g), g90=p90(g), rp=statistics.mean(rp), rp90=p90(rp),
                k=statistics.mean(pres), d=statistics.mean(dr.values()), us=statistics.mean(per) if per else 0.0,
                ups=1000.0 / statistics.mean(dts) if dts else 0.0, bake=bake)


print("%-4s %-28s %3s %13s %13s %5s %6s %6s %6s  %s" % ("at", "config", "n", "game ms (p90)", "render (p90)",
                                                        "k", "draws", "us/d", "ups", "baked (last interval)"))
for (w, c) in sorted(runs, key=lambda x: ("cbavup".index(x[0]), x[1])):
    rs = [x for x in (one(t) for t in sorted(runs[(w, c)])) if x]
    if not rs:
        continue
    f = lambda k: statistics.mean(r[k] for r in rs)
    print("%-4s %-28s %3d %6.2f (%5.2f) %6.2f (%5.2f) %5.2f %6.0f %6.3f %6.1f  %s" % (
        w, c, len(rs), f("g"), f("g90"), f("rp"), f("rp90"), f("k"), f("d"), f("us"), f("ups"), rs[-1]["bake"]))
