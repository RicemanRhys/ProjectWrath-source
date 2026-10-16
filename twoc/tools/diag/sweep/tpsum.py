"""tpsum.py CSV... -- step 79: a TPROF CSV's last SECS (25, env) of game frames before the run ended: frames, game
frames a second, dt (ms between game frames), the game side's ms a frame (recording and the game), the render
thread's (replays and presents less the pace waits; on one thread the replay side's), busy, the game's wait in its
Swap (render thread), each as mean and p90; the presents a frame got (the step: 4 = 240 fps at 240 Hz) and the
adaptive step changes. One line a file. The per-frame values are those of the frame before (ia_frame measures it)."""
import csv, os, sys

SECS = float(os.environ.get("SECS", "25"))


def pct(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(p * len(v)))] if v else 0.0


def mean(v):
    return sum(v) / len(v) if v else 0.0


for path in sys.argv[1:]:
    rows = list(csv.DictReader(open(path)))
    rows = [r for r in rows if float(r["dt"]) > 0]
    if not rows:
        print(os.path.basename(path), "no rows")
        continue
    # the last SECS seconds, by the sum of dt from the end
    acc, keep = 0.0, []
    for r in reversed(rows):
        acc += float(r["dt"])
        if acc > SECS * 1000:
            break
        keep.append(r)
    keep.reverse()
    f = lambda k: [float(r[k]) for r in keep]
    dt, game, rep, busy, wait = f("dt"), f("game"), f("replay"), f("busy"), f("wait")
    pres = [int(r["presents"]) for r in keep]
    steps = [int(r["step"]) for r in keep]
    ch = sum(1 for a, b in zip(steps, steps[1:]) if a != b)
    thr = sum(int(r["thread"]) for r in keep)
    hist = {}
    for p in pres:
        hist[p] = hist.get(p, 0) + 1
    print("%-14s %s frames %4d %5.1f/s  dt %5.2f (p90 %5.2f)  game %5.2f (%5.2f)  render %5.2f (%5.2f)  busy %5.2f (%5.2f)"
          "  wait %5.2f  presents %s  step changes %d (steps %s)" %
          (os.path.basename(path).replace(".tprof.csv", ""), "thread" if thr > len(keep) / 2 else "one   ", len(keep),
           1000.0 * len(keep) / max(1.0, sum(dt)), mean(dt), pct(dt, 0.9), mean(game), pct(game, 0.9), mean(rep),
           pct(rep, 0.9), mean(busy), pct(busy, 0.9), mean(wait),
           " ".join("%d:%d" % (k, hist[k]) for k in sorted(hist)), ch, sorted(set(steps))))
