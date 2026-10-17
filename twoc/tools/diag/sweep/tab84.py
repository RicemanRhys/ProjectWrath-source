"""tab84.py TAG [N] -- step 84: runs/TAG.gprof.csv (the game thread a frame) over its last N frames (default 1200,
less the last 30) and, if there, runs/TAG.hs.txt.json (hs84.py's samples): the frame's ms by part (game side,
mirror, recording, guest = game side - mirror), draws a frame, and each leaf function's share of the busy samples
(phases 0, 1, 7, 9) as ms a frame and us a draw, ranked, with its phase. CRT/system leaves are charged to their
exe caller ("memcpy<-ra_dup")."""
import csv, json, os, statistics, sys

HERE = os.path.dirname(os.path.abspath(__file__))
tag = sys.argv[1]
N = int(sys.argv[2]) if len(sys.argv) > 2 else 1200
rows = list(csv.DictReader(open(os.path.join(HERE, "runs", tag + ".gprof.csv"))))
rows = rows[-(N + 30):-30]


def col(k):
    return [float(r[k]) for r in rows]


def mp(v):
    v = sorted(v)
    return statistics.mean(v), v[int(0.9 * (len(v) - 1))]


game = col("game")
md, mi, ms, mo, rec, wait = col("mirror_draw"), col("mirror_imm"), col("mirror_swap"), col("mirror_other"), col("rec"), col("wait")
dr, vp = col("rec_draws"), col("vp_draws")
mirror = [a + c for a, c in zip(md, mo)]
guest = [g - m for g, m in zip(game, mirror)]
print("%s: %d frames (swaps %s-%s)" % (tag, len(rows), rows[0]["swap"], rows[-1]["swap"]))
for nm, v in (("game side", game), ("mirror (draws + other)", mirror), ("  of it draws", md), ("  of it immediate", mi),
              ("  recording (inside draws)", rec), ("guest (game side - mirror)", guest), ("wait in Swap", wait),
              ("draws recorded", dr), ("program draws", vp)):
    m, p = mp(v)
    print("  %-28s %8.3f  (p90 %.3f)" % (nm, m, p))
draws = statistics.mean(dr)
gms = statistics.mean(game)
js = os.path.join(HERE, "runs", tag + ".hs.txt.json")
if not os.path.exists(js):
    sys.exit(0)
h = json.load(open(js))
per = {int(p): c for p, c in h["per"].items()}
busy = sum(sum(c.values()) for p, c in per.items() if p in (0, 1, 7, 9))
print("samples %d, busy %d (wait %d)" % (h["samples"], busy, sum(per.get(8, {}).values())))
callers = {}
for p, nm, c in h["callers"]:
    callers[(int(p), nm)] = c
rank = []
for p, c in per.items():
    if p not in (0, 1, 7, 9):
        continue
    for nm, n in c.items():
        cc = callers.get((p, nm))
        if cc and (nm.startswith("<") or nm.lstrip("_").startswith(("mem"))):
            tot = sum(cc.values())
            rest = n
            for cn, k in cc.items():
                share = n * k / tot
                rank.append((share, p, "%s<-%s" % (nm, cn)))
            continue
        rank.append((n, p, nm))
agg = {}
for n, p, nm in rank:
    agg[(p, nm)] = agg.get((p, nm), 0) + n
PH = {0: "guest", 1: "rec", 7: "mirror", 9: "swap"}
tot_ph = {}
for (p, nm), n in agg.items():
    tot_ph[p] = tot_ph.get(p, 0) + n
for p in sorted(tot_ph):
    print("  phase %-6s %5.1f%%  %.3f ms a frame" % (PH[p], 100.0 * tot_ph[p] / busy, gms * tot_ph[p] / busy))
print("%-7s %-6s %8s %8s  %s" % ("share", "phase", "ms/frame", "us/draw", "function"))
for (p, nm), n in sorted(agg.items(), key=lambda x: -x[1])[:int(os.environ.get("TOP", "60"))]:
    ms_ = gms * n / busy
    print("%6.2f%% %-6s %8.3f %8.3f  %s" % (100.0 * n / busy, PH[p], ms_, 1000.0 * ms_ / draws, nm))
