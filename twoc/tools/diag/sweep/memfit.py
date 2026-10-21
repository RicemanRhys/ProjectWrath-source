"""memfit.py LOG [SKIP_MIN] [--entries] -- step 99: the [MEM] lines of a run (notes 18.102), one row per quantity.

Each [MEM] line ("[MEM] +612.3 s: process private 512.3 MB (peak 600.1), working set ...; handles 640, threads 46,
...") is split into items at ';' and ',' outside parentheses; an item's first number is its value (MB, KB, GB or a
count), its name the words before it (a group's later items prefixed with the group's first word, as "guest live").
For each quantity: the first and last value, min, max, and a least-squares slope per hour over the samples after
SKIP_MIN minutes (default 5) of the run, with the change over that span; flagged GROWING when the fit's rise over the
span is more than both 2 MB (or 2 for a count, 20 KB for KB) and 5% of the mean, and the last third's mean is above
the middle third's (a plateau's last two thirds are level).

--entries LOG.cycle.txt: also the value at each level entry ("level N loading X" lines in the cycle log, matched by
time to the nearest [MEM] sample after the load): does anything rise from one entry to the next?
"""
import re, sys

UNIT = {"MB": 1.0, "GB": 1024.0, "KB": 1.0 / 1024}


def strip_paren(s):
    out, d = [], 0
    for ch in s:
        if ch == "(":
            d += 1
        elif ch == ")":
            d = max(0, d - 1)
        elif d == 0:
            out.append(ch)
    return "".join(out)


def items(text):
    res = {}
    for group in strip_paren(text).split(";"):
        first = None
        for it in group.split(","):
            m = re.match(r"\s*([A-Za-z][A-Za-z '/-]*?)\s+(-?[0-9]+(?:\.[0-9]+)?)\s*(MB|GB|KB)?\b", it)
            if not m:
                continue
            name, val, unit = m.group(1).strip(), float(m.group(2)), m.group(3)
            if first is None:
                first = name.split()[0]
            elif name.split()[0] in ("live", "largest", "non-local", "high-water"):
                name = first + " " + name       # "guest live", "VM largest free", "GPU non-local"
            if unit:
                val *= UNIT[unit]
                name += " (MB)"
            res[name] = val
    return res


def read(path):
    rows = []
    for line in open(path, "rb").read().decode("latin1").splitlines():
        m = re.match(r"\[MEM\] \+([0-9.]+) s: (.*)", line)
        if m:
            rows.append((float(m.group(1)), items(m.group(2))))
    # one sample can be printed by more than one interval line: keep the first of each time
    seen, out = set(), []
    for t, d in rows:
        if t not in seen:
            seen.add(t)
            out.append((t, d))
    return out


def fit(ts, vs):
    n = len(ts)
    mt, mv = sum(ts) / n, sum(vs) / n
    sxx = sum((t - mt) ** 2 for t in ts)
    return (sum((t - mt) * (v - mv) for t, v in zip(ts, vs)) / sxx) if sxx else 0.0


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    path = a[0]
    skip = float(a[1]) if len(a) > 1 else 5.0
    rows = read(path)
    if not rows:
        print("no [MEM] lines in", path)
        return
    t0 = rows[0][0]
    names = []
    for _, d in rows:
        for k in d:
            if k not in names:
                names.append(k)
    print("%s: %d samples, %.1f min (fit from %.0f min)" % (path, len(rows), (rows[-1][0] - t0) / 60, skip))
    print("%-44s %10s %10s %10s %10s %11s %10s  %s" % ("quantity", "first", "last", "min", "max", "per hour", "fit rise",
                                                        "verdict"))
    for k in names:
        pts = [(t, d[k]) for t, d in rows if k in d]
        fitpts = [(t, v) for t, v in pts if t - t0 >= skip * 60]
        if len(fitpts) < 4:
            continue
        ts, vs = [p[0] for p in fitpts], [p[1] for p in fitpts]
        s = fit(ts, vs)
        span = ts[-1] - ts[0]
        rise = s * span
        mean = sum(vs) / len(vs)
        third = max(1, len(vs) // 3)
        mid = sum(vs[third:2 * third]) / max(1, len(vs[third:2 * third]))
        last = sum(vs[2 * third:]) / max(1, len(vs[2 * third:]))
        floor = 2.0 if "(MB)" in k else 2.0
        counter = "started over" in k or "system commit" in k   # a count of events, or not this process's
        grow = not counter and rise > floor and rise > 0.05 * abs(mean) and last > mid
        print("%-44s %10.1f %10.1f %10.1f %10.1f %11.2f %10.2f  %s" % (
            k[:44], pts[0][1], pts[-1][1], min(v for _, v in pts), max(v for _, v in pts), s * 3600, rise,
            "GROWING" if grow else ""))
    if "--entries" in sys.argv:
        cyc = sys.argv[sys.argv.index("--entries") + 1]
        print("\nat each level entry (the first [MEM] sample 15 s or more after the load line):")
        # cycle.txt times are seconds from cycle.py's start; align by the run's own clock is not possible, so use
        # the err log's "[D3DB] host +X s" lines around each "levels\\" path instead
        log = open(path, "rb").read().decode("latin1").splitlines()
        t = 0.0
        loads = []
        for line in log:
            m = re.match(r"\[D3DB\] host \+([0-9.]+) s:", line)
            if m:
                t = float(m.group(1))
            m = re.search(r"levels\\[ac]\\(\w+)\\\w+\.nux", line)
            if m:
                if not loads or loads[-1][1] != m.group(1) or t - loads[-1][0] > 30:
                    loads.append((t, m.group(1)))
        keys = [k for k in names if any(w in k for w in ("process private", "working set", "handles", "threads",
                                                          "GPU local", "vertex cache", "textures", "guest heap live",
                                                          "contiguous", "render targets", "slots", "baked"))]
        print("%-6s %-10s " % ("entry", "level") + " ".join("%12s" % k[:12] for k in keys))
        for i, (tl, lv) in enumerate(loads):
            after = [d for ts, d in rows if ts >= tl + 15]
            if not after:
                break
            d = after[0]
            print("%-6d %-10s " % (i + 1, lv) + " ".join("%12.1f" % d.get(k, float("nan")) for k in keys))


main()
