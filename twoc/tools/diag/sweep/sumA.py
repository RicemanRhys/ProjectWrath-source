"""sumA.py TAG... (or "a*") -- step 99: one line per sweep run.

The level loaded after the hub (its .ter path), then over the 10-s intervals from 20 s after that load to the run's
end: game updates a second (min / median), presents a second, frames shown one refresh apart (% of those measured),
the game side's and the replay side's ms a game frame (the adaptive line's, when it runs), swap gaps over 25 ms and
the longest, then over the whole log: [CRASH], the render thread watchdog, THREADCHECK and frame-slot lines, the exit
(launch.txt), play.py's summary (frames, stall, distance), and the last [MEM] line's private MB and vertex cache.
"""
import glob, os, re, statistics, sys

HERE = os.path.dirname(os.path.abspath(__file__))
R = os.path.join(HERE, "runs")


def rd(p):
    try:
        return open(p, "rb").read().decode("latin1")
    except OSError:
        return ""


def one(t):
    err = rd(os.path.join(R, t + ".err.log"))
    lines = err.splitlines()
    level, load_t, t_now = "-", None, 0.0
    first, first_t = None, None         # the run's level: the first after the hub (random play may leave it)
    ivs, cur = [], None
    for l in lines:
        m = re.match(r"\[D3DB\] host \+([\d.]+) s: guest swaps \d+ \(\+\d+, ([\d.]+)/s\), host presents \d+ \(\+(\d+)\)", l)
        if m:
            t_now = float(m.group(1))
            if cur:
                ivs.append(cur)
            cur = dict(t=t_now, ups=float(m.group(2)), pr=int(m.group(3)) / 10.0, lvl=level, lt=load_t)
            continue
        if "[PATH]" in l and re.search(r"Crashdat\\levels\\b\\hub\\hub\.nux", l):
            level = "-"                 # back in the hub: the next level's first folder names it
        m = re.search(r"Crashdat\\levels\\[ac]\\(\w+)\\", l)
        if m and "[PATH]" in l and level == "-":     # a level's shared folders (castle, castle_c) keep the first
            level, load_t = m.group(1), t_now
            if first is None:
                first, first_t = level, load_t
        if cur is None:
            continue
        m = re.search(r"swap gaps this interval: over 25 ms (\d+), over 50 ms (\d+), over 100 ms (\d+), longest ([\d.]+)", l)
        if m:
            cur["g25"], cur["glong"] = int(m.group(1)), float(m.group(4))
        m = re.search(r"frames shown this interval, by refreshes since the last shown: 1 (\d+), 2 (\d+), 3 (\d+), 4 (\d+), "
                      r"5 (\d+), 6 (\d+), 7 (\d+), more (\d+)", l)
        if m:
            g = list(map(int, m.groups()))
            cur["s1"], cur["sall"] = g[0], sum(g)
        m = re.search(r"measured frames (\d+), [\d.]+ ms apart, [\d.]+ ms of it busy \(the game side ([\d.]+), the replay "
                      r"side ([\d.]+)\)", l)
        if m and int(m.group(1)):
            cur["gs"], cur["rs"] = float(m.group(2)), float(m.group(3))
    if cur:
        ivs.append(cur)
    play = [v for v in ivs if v["lvl"] == first and v["lt"] == first_t and v["t"] - v["lt"] >= 30]
    s = "%-10s %-9s" % (t, first or "-")
    if play:
        # intervals with a swap gap over 100 ms hold a load (random play may walk into a warp or back to the hub)
        clean = [v["ups"] for v in play if v.get("glong", 0) < 100] or [v["ups"] for v in play]
        ups = [v["ups"] for v in play]
        s1 = sum(v.get("s1", 0) for v in play)
        sa = sum(v.get("sall", 0) for v in play)
        gs = [v["gs"] for v in play if "gs" in v]
        rs = [v["rs"] for v in play if "rs" in v]
        s += " ups %4.1f/%4.1f (%d loads) pr %5.1f shown1 %5.1f%% g %4s r %4s gaps25 %3d long %6.1f" % (
            min(clean), statistics.median(ups), len(ups) - len(clean) if len(clean) < len(ups) else 0, statistics.median(v["pr"] for v in play), 100.0 * s1 / sa if sa else 0,
            "%.1f" % statistics.mean(gs) if gs else "-", "%.1f" % statistics.mean(rs) if rs else "-",
            sum(v.get("g25", 0) for v in play), max(v.get("glong", 0) for v in play))
    else:
        s += " (no full interval in the level)"
    crash = err.count("[CRASH]")
    wd = len(re.findall(r"render thread watchdog: the", err))
    tc = len(re.findall(r"THREADCHECK: .*ending the process", err))
    fs = err.count("host frame slots:")
    lt = rd(os.path.join(R, t + ".launch.txt")).strip().splitlines()
    pl = re.search(r"frames (\d+) in ([\d.]+) s \(([\d.]+)/s\), longest stall ([\d.]+) s, moved ([\d.]+)",
                   rd(os.path.join(R, t + ".play.txt")) + rd(os.path.join(R, t + ".act.txt")))
    mem = [l for l in lines if l.startswith("[MEM]")]
    mm = re.search(r"process private ([\d.]+) MB.*vertex cache ([\d.]+) MB", mem[-1]) if mem else None
    s += " | CRASH %d wd %d tc %d slots %d | %s | play %s | mem %s" % (
        crash, wd, tc, fs, lt[-1] if lt else "?", "%s/s stall %ss moved %s" % (pl.group(3), pl.group(4), pl.group(5))
        if pl else "-", "%s MB, vc %s" % mm.groups() if mm else "-")
    print(s)


tags = sys.argv[1:]
if len(tags) == 1 and "*" in tags[0]:
    tags = sorted({os.path.basename(p)[:-8] for p in glob.glob(os.path.join(R, tags[0] + ".err.log"))})
for t in tags:
    one(t)
