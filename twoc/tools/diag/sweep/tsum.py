"""tsum.py PREFIX -- step 95: mm.sh's runs PREFIX<round><where>_<config> from the 10-s lines alone (any build, the
profile patch not needed): the intervals after the level's load (the first 10 s after it left out), per run the
game side's and the replay side's ms a game frame ("host interpolation sub-frames"), updates a second, frames a
game frame, pairing ms, the draws' D3D11 ms and Present ms, and the baked-draws counts; then each place and config
over its rounds (mean of the runs' means, and the spread)."""
import collections, glob, os, re, statistics, sys

HERE = os.path.dirname(os.path.abspath(__file__))
pre = sys.argv[1]
WANT = {"cb": "cast_bug", "av": "avalanch", "up": "hub"}
R_SUB = re.compile(r"sub-frames: .* measured frames (\d+), ([\d.]+) ms apart, ([\d.]+) ms of it busy \(the game side "
                   r"([\d.]+), the replay side ([\d.]+)\)")
R_SW = re.compile(r"host \+([\d.]+) s: guest swaps \d+ \(\+(\d+), ([\d.]+)/s\), host presents \d+ \(\+(\d+)\)")
R_PAIR = re.compile(r"pairing ([\d.]+) ms a frame")
R_DK = re.compile(r"the draws ([\d.]+) ms a game frame, ([\d.]+) ms of it in D3D11 calls \(\d+%\); Present ([\d.]+)")
R_REC = re.compile(r"recorded (\d+) KB a frame, ([\d.]+) us a draw to record")


def one(tag):
    log = os.path.join(HERE, "runs", tag + ".err.log")
    if not os.path.exists(log):
        return None
    lines = open(log, encoding="utf-8", errors="replace").read().splitlines()
    want = WANT[re.match(re.escape(pre) + r"\d(cb|av|up)_", tag).group(1)]
    load_t, t = None, 0.0
    iv = []
    cur = {}
    for l in lines:
        m = R_SW.search(l)
        if m:
            if cur:
                iv.append(cur)
            t = float(m.group(1))
            cur = {"t": t, "ups": float(m.group(3)), "pres": int(m.group(4)), "sw": int(m.group(2))}
            continue
        if "Crashdat\\levels\\" in l and want in l and load_t is None:
            load_t = t
        if not cur:
            continue
        m = R_SUB.search(l)
        if m:
            cur.update(game=float(m.group(4)), rep=float(m.group(5)), busy=float(m.group(3)))
        m = R_PAIR.search(l)
        if m:
            cur["pair"] = float(m.group(1))
        m = R_DK.search(l)
        if m:
            cur.update(draw=float(m.group(1)), ctx=float(m.group(2)), pres_ms=float(m.group(3)))
        m = R_REC.search(l)
        if m:
            cur.update(kb=int(m.group(1)), rec=float(m.group(2)))
    if cur:
        iv.append(cur)
    if load_t is None:
        return None
    use = [i for i in iv if i["t"] > load_t + 20 and "game" in i]
    if len(use) < 2:
        return None
    use = use[-4:]
    f = lambda k: statistics.mean(i[k] for i in use if k in i) if any(k in i for i in use) else float("nan")
    return dict(n=len(use), game=f("game"), rep=f("rep"), ups=f("ups"), k=f("pres") / max(f("sw"), 1), pair=f("pair"),
                draw=f("draw"), ctx=f("ctx"), pres=f("pres_ms"), kb=f("kb"), rec=f("rec"))


runs = collections.defaultdict(list)
for p in glob.glob(os.path.join(HERE, "runs", pre + "*.err.log")):
    tag = os.path.basename(p)[:-len(".err.log")]
    m = re.match(re.escape(pre) + r"(\d)(cb|av|up)_(.*)$", tag)
    if m:
        runs[(m.group(2), m.group(3))].append(tag)
print("%-3s %-10s %2s %13s %13s %5s %5s %6s %6s %6s %6s %6s %5s" % ("at", "config", "n", "game ms", "render ms", "ups",
                                                                   "k", "pair", "draws", "d3d11", "pres", "rec KB", "us/r"))
for (w, c) in sorted(runs, key=lambda x: ("cbavup".index(x[0]), x[1])):
    rs = [x for x in (one(t) for t in sorted(runs[(w, c)])) if x]
    if not rs:
        print("%-3s %-10s  0 (no run reached its level)" % (w, c))
        continue
    g = [r["game"] for r in rs]
    p = [r["rep"] for r in rs]
    m = lambda k: statistics.mean(r[k] for r in rs)
    print("%-3s %-10s %2d %6.2f +-%4.2f %6.2f +-%4.2f %5.1f %5.2f %6.2f %6.2f %6.2f %6.2f %6.0f %5.2f" % (
        w, c, len(rs), statistics.mean(g), (max(g) - min(g)) / 2, statistics.mean(p), (max(p) - min(p)) / 2,
        m("ups"), m("k"), m("pair"), m("draw"), m("ctx"), m("pres"), m("kb"), m("rec")))
