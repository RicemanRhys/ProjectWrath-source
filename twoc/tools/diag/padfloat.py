"""padfloat.py LOG... -- the hub pads' pieces (program F1CEC538, 77 vertices) against their floor rims
(same program, 272 vertices), per swap of a motion log. A piece whose projected origin has no rim within
--tol NDC at a similar depth (w within 30%) is "floating". Prints counts and the frames."""
import sys

tol = 0.15
logs = [a for a in sys.argv[1:] if not a.startswith("--")]
for a in sys.argv[1:]:
    if a.startswith("--tol="):
        tol = float(a[6:])


def proj(m, p):
    c = [sum(m[4 * r + i] * p[i] for i in range(4)) for r in range(4)]
    return (c[0] / c[3], c[1] / c[3], c[3]) if abs(c[3]) > 1e-6 else None


for path in logs:
    pieces, rims = [], []
    frames = 0
    float_frames = []
    ws_p, ws_r = [], []
    frame = None
    n_piece = 0
    for line in open(path):
        f = line.split()
        if not f:
            continue
        if f[0] == "S":
            frames += 1
            fl = 0
            for p in pieces:
                if not p or p[2] <= 0:
                    continue
                on_screen = abs(p[0]) < 1.2 and abs(p[1]) < 1.2
                if not on_screen:
                    continue
                n_piece += 1
                ok = any(r and r[2] > 0 and abs(r[0] - p[0]) < tol and abs(r[1] - p[1]) < tol
                         and abs(r[2] - p[2]) < 0.3 * r[2] for r in rims)
                if not ok:
                    fl += 1
                ws_p.append(p[2])
            for r in rims:
                if r and r[2] > 0:
                    ws_r.append(r[2])
            if fl:
                float_frames.append((int(f[2]), fl, min(p[2] for p in pieces if p)))
            pieces, rims = [], []
        elif f[0] == "V" and len(f) >= 39 and f[2] == "F1CEC538":
            m = [float(x) for x in f[11:27]]
            o = proj(m, (0, 0, 0, 1))
            if f[4] == "77":
                pieces.append(o)
            elif f[4] == "272":
                rims.append(o)
    print(path.split("\\")[-1].split("/")[-1], "swaps", frames, "piece draws on screen", n_piece,
          "frames with a floating piece", len(float_frames))
    if ws_p:
        print("  piece w range %.1f..%.1f, rim w range %.1f..%.1f" % (min(ws_p), max(ws_p), min(ws_r), max(ws_r)))
    if float_frames:
        print("  game frames %d..%d, first: %s" % (float_frames[0][0], float_frames[-1][0],
              " ".join("%d(%d,w%.1f)" % x for x in float_frames[:8])))
