"""fpsum.py CSV [FROM_SWAP TO_SWAP] -- step 75 (notes 18.78): summarise RECOMP_D3D_BRIDGE_FPROF's per-frame lines
(../step75_frame_profile.patch) and CSV.gpu (GPU timestamps; they follow the CPU's submission while it is the limit).
Frames with k=4 and k=1 apart; mean, p50, p90 of each column; the frame's wall (game window + present) and fps."""
import csv, statistics, sys

path = sys.argv[1]
lo = int(sys.argv[2]) if len(sys.argv) > 2 else 0
hi = int(sys.argv[3]) if len(sys.argv) > 3 else 1 << 30
rows = [r for r in csv.DictReader(open(path)) if r.get("post4") not in (None, "")]
rows = [r for r in rows if lo <= int(r["swap"]) <= hi and float(r["game_wall"]) > 0]


def q(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(p * len(v)))] if v else 0.0


cols = ["game_wall", "game_cpu", "mirror_draw", "mirror_other", "rec", "present_wall", "present_cpu", "ia", "begin",
        "pair", "rp1", "rp2", "rp3", "rp4", "pace1", "pace2", "pace3", "pace4", "copy1", "copy2", "copy3", "copy4",
        "pres1", "pres2", "pres3", "pres4", "post1", "post2", "post3", "post4"]
for k in sorted(set(r["k"] for r in rows)):
    rk = [r for r in rows if r["k"] == k]
    if len(rk) < 5:
        continue
    wall = [float(r["game_wall"]) + float(r["present_wall"]) for r in rk]
    print("k=%s: %d frames, swaps %s-%s, commands %.0f (draws %.0f), frame %.2f ms mean (p50 %.2f, p90 %.2f) = %.1f fps"
          % (k, len(rk), rk[0]["swap"], rk[-1]["swap"], statistics.mean(float(r["cmds"]) for r in rk),
             statistics.mean(float(r["draws"]) for r in rk), statistics.mean(wall), q(wall, .5), q(wall, .9),
             1000.0 / statistics.mean(wall)))
    for c in cols:
        v = [float(r[c]) for r in rk]
        if max(v) <= 0:
            continue
        print("  %-13s mean %7.3f  p50 %7.3f  p90 %7.3f  max %7.3f" % (c, statistics.mean(v), q(v, .5), q(v, .9), max(v)))
    # derived
    g = [float(r["game_wall"]) - float(r["mirror_draw"]) - float(r["mirror_other"]) for r in rk]
    m = [float(r["mirror_draw"]) + float(r["mirror_other"]) - float(r["rec"]) for r in rk]
    busy = [float(r["game_wall"]) + float(r["present_wall"]) - sum(float(r["pace%d" % i]) for i in range(1, 5)) for r in rk]
    pres = [sum(float(r["copy%d" % i]) + float(r["pres%d" % i]) + float(r["post%d" % i]) for i in range(1, 5)) for r in rk]
    rp = [sum(float(r["rp%d" % i]) for i in range(1, 5)) for r in rk]
    print("  derived: game alone %.2f, mirror translate %.2f, recording %.2f, ia+begin %.2f, replays %.2f, presents "
          "(copy+Present+after) %.2f, pace %.2f; busy %.2f ms (p90 %.2f)"
          % (statistics.mean(g), statistics.mean(m), statistics.mean(float(r["rec"]) for r in rk),
             statistics.mean(float(r["ia"]) + float(r["begin"]) for r in rk), statistics.mean(rp), statistics.mean(pres),
             statistics.mean(sum(float(r["pace%d" % i]) for i in range(1, 5)) for r in rk), statistics.mean(busy),
             q(busy, .9)))
try:
    gl = [l.strip().split(",") for l in open(path + ".gpu") if ":" in l and l[0].isdigit()]
except OSError:
    gl = []
gl = [x for x in gl if lo <= int(x[0]) <= hi]
if gl:
    acc = {}
    for x in gl:
        t = {}
        for p in x[1:]:
            a, b = p.split(":")
            t.setdefault(int(a), float(b))
        for s in range(1, 5):
            if s in t and 10 + s in t:
                acc.setdefault("pass%d" % s, []).append(t[10 + s] - t[s])
            if 10 + s in t and 20 + s in t:
                acc.setdefault("copy%d" % s, []).append(t[20 + s] - t[10 + s])
        if 1 in t and 24 in t:
            acc.setdefault("first pass to last copy", []).append(t[24] - t[1])
    print("GPU timestamps (%d frames): ms between them on the GPU (an upper bound on its work, waits included)" % len(gl))
    for k2, v in acc.items():
        print("  %-24s mean %7.3f  p50 %7.3f  p90 %7.3f" % (k2, statistics.mean(v), q(v, .5), q(v, .9)))
