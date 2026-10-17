"""fsum.py PREFIX [N] -- step 84: fin.sh's runs PREFIX<round><where>_<config> as one table: for each place and
config, over the rounds (each run's last N game frames, default 1200, less the last 30): the game thread's ms a frame
(mean, p90), its mirror, recording and guest parts, the render thread's ms (TPROF's replay column, the same frames'
span), game updates a second, and thr.py's per-thread CPU when there (the NV2A ack thread's, the process's)."""
import collections, csv, glob, os, re, statistics, sys

HERE = os.path.dirname(os.path.abspath(__file__))
pre = sys.argv[1]
N = int(sys.argv[2]) if len(sys.argv) > 2 else 1200
runs = collections.defaultdict(list)
for p in glob.glob(os.path.join(HERE, "runs", pre + "*.gprof.csv")):
    tag = os.path.basename(p)[:-len(".gprof.csv")]
    m = re.match(re.escape(pre) + r"(\d)(cb|av|up)_(.*)$", tag)
    if m:
        runs[(m.group(2), m.group(3))].append(tag)


def p90(v):
    v = sorted(v)
    return v[int(0.9 * (len(v) - 1))] if v else 0.0


WANT = {"cb": "cast_bug", "av": "avalanch", "up": "hub"}


def one(tag):
    lv = ""
    for l in open(os.path.join(HERE, "runs", tag + ".err.log"), encoding="utf-8", errors="replace"):
        if "Crashdat\\levels\\" in l:
            lv = l
    if WANT[tag.split("_")[0][-2:]] not in lv:
        print("  (left out: %s ended in %s)" % (tag, lv.strip().split("\\")[-1] if lv else "no level"))
        return None
    rows = list(csv.DictReader(open(os.path.join(HERE, "runs", tag + ".gprof.csv"))))
    rows = rows[-(N + 30):-30]
    if len(rows) < 200:
        return None
    g = [float(r["game"]) for r in rows]
    mir = [float(r["mirror_draw"]) + float(r["mirror_other"]) for r in rows]
    rec = [float(r["rec"]) for r in rows]
    s0, s1 = int(rows[0]["swap"]), int(rows[-1]["swap"])
    tp = os.path.join(HERE, "runs", tag + ".tprof.csv")
    rp, ups = [], 0.0
    if os.path.exists(tp):
        tr = [r for r in csv.DictReader(open(tp)) if s0 <= int(r["swap"]) <= s1 and r["thread"] == "1"]
        rp = [float(r["replay"]) for r in tr]
        dts = [float(r["dt"]) for r in tr if float(r["dt"]) > 0]
        ups = 1000.0 / statistics.mean(dts) if dts else 0.0
    thr = os.path.join(HERE, "runs", tag + ".thr.txt")
    ack = allc = None
    if os.path.exists(thr):
        for l in open(thr):
            if "nv2a_ack_thread" in l and ack is None:
                ack = float(l.split()[2].rstrip("%"))
            if l.startswith("all threads"):
                allc = float(l.split()[2].rstrip("%"))
    return dict(g=statistics.mean(g), g90=p90(g), mir=statistics.mean(mir), rec=statistics.mean(rec),
                guest=statistics.mean(g) - statistics.mean(mir), rp=statistics.mean(rp) if rp else 0.0,
                rp90=p90(rp), ups=ups, ack=ack, all=allc)


print("%-4s %-16s %-3s %15s %7s %7s %7s %15s %6s %s" % ("at", "config", "n", "game (p90)", "mirror", "rec",
                                                         "guest", "render (p90)", "upd/s", "cpu: ack, all"))
for (w, c) in sorted(runs, key=lambda k: ("cb av up".split().index(k[0]), k[1])):
    rs = [x for x in (one(t) for t in sorted(runs[(w, c)])) if x]
    if not rs:
        print(w, c, "no data")
        continue
    a = lambda k: statistics.mean(r[k] for r in rs)
    cpu = ""
    if any(r["ack"] is not None for r in rs):
        cpu = "%.0f%%, %.0f%%" % (statistics.mean(r["ack"] for r in rs if r["ack"] is not None),
                                 statistics.mean(r["all"] for r in rs if r["all"] is not None))
    print("%-4s %-16s %-3d %7.2f (%5.2f) %7.2f %7.2f %7.2f %7.2f (%5.2f) %6.1f %s" % (
        w, c, len(rs), a("g"), a("g90"), a("mir"), a("rec"), a("guest"), a("rp"), a("rp90"), a("ups"), cpu))
