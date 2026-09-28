"""Read a RECOMP_D3D_BRIDGE_MOTIONLOG file (step 42, notes 18.44).

  python motionlog.py LOG [--d=NDC] [--dff=UNITS] [--track=PROGRAM:NV] [--frames=A-B] [--callers]

The log has one S line per guest swap (swap, game frame counter, time) and one line per mirrored draw:
  V ord program lo nv consthash verthash v0(4) c96-c99(16) c124-c126(12) @drawcaller c0writer<chain<chain<chain
  F ord fvf lo nv tex0 world_translation(3) verthash vertex_lo_position(3)
Logging runs only while LOG.on exists (drive.py-style scripts touch it around the part to watch).

What it prints:
  - excursions of fixed-function draws: the world translation leaves its path by more than --dff and
    comes back within 3 swaps (key: FVF, vertex count, texture, vertex hash, order in the swap);
  - excursions of program draws in screen space: the model origin and the first bone's translation
    projected through c96-c99 (every program seen in the hub reads its transform there; the skinned
    ones their first bone at c124) leave by more than --d NDC and come back within 3 swaps
    (key: program, vertex count, vertex hash, order in the swap). Neighbouring instances of one mesh
    can trade places in the draw order, which shows up as a run of small excursions at the same y;
  - --track: every swap's anchors for one program and vertex count;
  - --frames: every program draw on screen in those game frames;
  - --callers: draws per program by the draw's caller and the c0 writer's call chain.
"""
import collections, math, sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
opt = dict(a[2:].split("=", 1) if "=" in a else (a[2:], "1") for a in sys.argv[1:] if a.startswith("--"))
path = args[0]
D = float(opt.get("d", 0.1))
DFF = float(opt.get("dff", 0.5))


def proj(m, p):
    c = [sum(m[4 * r + i] * p[i] for i in range(4)) for r in range(4)]
    return (c[0] / c[3], c[1] / c[3], c[3]) if abs(c[3]) > 1e-6 else None


swaps, curv, curf, seen = [], {}, {}, collections.Counter()
callers = collections.Counter()
for line in open(path):
    f = line.split()
    if not f:
        continue
    if f[0] == "S":
        swaps.append((int(f[1]), int(f[2]), curv, curf))
        curv, curf, seen = {}, {}, collections.Counter()
    elif f[0] == "V" and len(f) >= 39:
        m = [float(x) for x in f[11:27]]
        b = [float(x) for x in f[27:39]]
        base = ("V", f[2], f[4], f[6])
        curv[base + (seen[base],)] = (proj(m, (0, 0, 0, 1)), proj(m, (b[3], b[7], b[11], 1)))
        seen[base] += 1
        if len(f) >= 41:
            callers[(f[2], f[39], f[40])] += 1
    elif f[0] == "F":
        base = ("F", f[2], f[4], f[5], f[9])
        curf[base + (seen[base],)] = tuple(float(x) for x in f[6:9])
        seen[base] += 1

print("swaps %d (game frames %d..%d)" % (len(swaps), swaps[0][1], swaps[-1][1]) if swaps else "no swaps")


def excursions(get, keys, d):
    out = []
    for k in keys:
        for i in range(3, len(swaps) - 3):
            v = get(swaps[i], k)
            if v is None:
                continue
            for w in (1, 2, 3):
                a, c = get(swaps[i - w], k), get(swaps[i + w], k)
                if a is None or c is None:
                    continue
                if math.dist(a, c) < d / 4 and math.dist(v, a) > d and math.dist(v, c) > d:
                    out.append((swaps[i][1], w, k, a, v, c))
                    break
    return out


fk = [k for k, n in collections.Counter(k for s in swaps for k in s[3]).items() if n >= 10]
ff = excursions(lambda s, k: s[3].get(k), fk, DFF)
print("fixed-function meshes seen in >= 10 swaps: %d; world-translation excursions > %g: %d" % (len(fk), DFF, len(ff)))
for fr, w, k, a, v, c in ff[:30]:
    print("  frame %d (+-%d) %s: %s -> %s -> %s" % (fr, w, " ".join(map(str, k)), a, v, c))

vk = [k for k, n in collections.Counter(k for s in swaps for k in s[2]).items() if n >= 10]
for which, name in ((0, "origin"), (1, "bone0")):
    def get(s, k, which=which):
        v = s[2].get(k)
        return v[which][:2] if v and v[which] else None
    ex = excursions(get, vk, D)
    print("program draws seen in >= 10 swaps: %d; %s excursions > %g NDC: %d" % (len(vk), name, D, len(ex)))
    for fr, w, k, a, v, c in ex[:30]:
        print("  frame %d (+-%d) %s: (%.2f %.2f) -> (%.2f %.2f) -> (%.2f %.2f)" % (fr, w, " ".join(map(str, k)), *a, *v, *c))

if "track" in opt:
    prog, nv = opt["track"].split(":")
    for s in swaps:
        row = ["%s(%.2f,%.2f w%.1f)" % (k[3][:4], *v[0]) for k, v in s[2].items() if k[1] == prog and k[2] == nv and v[0]]
        if row:
            print(s[1], len(row), " ".join(row))
if "frames" in opt:
    a, b = (int(x) for x in opt["frames"].split("-"))
    for s in swaps:
        if a <= s[1] <= b:
            for k, v in s[2].items():
                if v[0] and abs(v[0][0]) < 1.2 and abs(v[0][1]) < 1.2 and v[0][2] > 0:
                    print(s[1], " ".join(map(str, k[1:])), "origin (%.2f %.2f) w %.2f" % v[0])
if "callers" in opt:
    for (prog, drawn, chain), n in callers.most_common(40):
        print("%7d %s drawn %s, c0 set %s" % (n, prog, drawn, chain))
