"""Read the bridge's draw log (step 49, notes 18.52).

  python drawlog.py ERRLOG SWAP [--blended] [--program=HASH] [--texture=HEADER]

One line per draw of guest swap SWAP: number, path, primitive, index or
vertex count, vertex program (its hash, from the RECOMP_D3D_BRIDGE_VSDUMP
per-draw dump), FVF or program handle, texture 0, blend, alpha test, depth,
stencil, cull, colour write and, for untransformed fixed-function draws, the
screen box. Clears in the window are listed where they fall. --blended keeps
the draws with blending on.

The log comes from RECOMP_D3D_BRIDGE_DRAWLOG=A-B (drive.py --drawlog) or,
for a scene reached by hand, RECOMP_D3D_BRIDGE_DRAWLOG_TRIGGER=FILE: write a
swap count into FILE while the game runs and the next swaps are logged.
Add RECOMP_D3D_BRIDGE_VSDUMP=1 for program hashes and constants.

As a module: parse(path, swaps) returns the draws as dicts (swap, no, path,
prim, n, fvf, outcome, tex0, fmt, w, h, data, tex1, target, lines, vs, dump),
where `lines` are the record's [D3DB] drawlog lines and `dump` the program's
per-draw vsdump lines by kind (code, attr, const, vertex, ...).
"""
import re
import sys

HDR = re.compile(r"\[D3DB\] drawlog swap (\d+) #(\d+) (\S+) prim (\d+) n (\d+) fvf (\S+) (.*?); tex0 (\S+) "
                 r"\(fmt (\S+), (\d+)x(\d+), data (\S+)\) tex1 (\S+); target (\S+);")
CLEAR = re.compile(r"\[D3DB\] drawlog swap (\d+) clear before #(\d+): (.*)")
VSDUMP = re.compile(r"\[D3DB\] vsdump (\w{8})\.(\d+) (\w+) ?(.*)")


def parse(path, swaps=None, clears=None):
    """The draws of `swaps` (all if None); a vsdump for draw N precedes N's
    header. Clears go to the `clears` list if one is given."""
    recs, cur, pend = [], None, {}
    for line in open(path, "rb"):
        line = line.decode("latin1").rstrip("\r\n")
        m = HDR.match(line)
        if m:
            s = int(m.group(1))
            if swaps and s not in swaps:
                cur = None
                continue
            cur = dict(swap=s, no=int(m.group(2)), path=m.group(3), prim=int(m.group(4)), n=int(m.group(5)),
                       fvf=m.group(6), outcome=m.group(7), tex0=m.group(8), fmt=m.group(9),
                       w=int(m.group(10)), h=int(m.group(11)), data=m.group(12), tex1=m.group(13),
                       target=m.group(14), lines=[line], vs=None, dump={})
            p = pend.pop(int(m.group(2)), None)
            if p:
                cur["vs"], cur["dump"] = p
            pend.clear()
            recs.append(cur)
            continue
        m = CLEAR.match(line)
        if m:
            if clears is not None and (not swaps or int(m.group(1)) in swaps):
                clears.append((int(m.group(1)), int(m.group(2)), m.group(3)))
            continue
        m = VSDUMP.match(line)
        if m:
            p = pend.setdefault(int(m.group(2)), [m.group(1), {}])
            p[1].setdefault(m.group(3), []).append(m.group(4))
            continue
        if cur is not None and line.startswith("[D3DB] drawlog   "):
            cur["lines"].append(line)
    return recs


def summary(r):
    L = r["lines"]
    bl = re.search(r"blend (\d+) (\S+) (\S+) op \S+ atest (\d+) (\S+) ref (\S+) z (\S+);", L[1]) if len(L) > 1 else None
    st = [l for l in L if "zfunc" in l]
    stz = re.search(r"zfunc (\S+); stencil (\d+) func (\S+) ref (\S+).*ops (\S+ \S+ \S+);.*cull (\S+); colour write (\S+)",
                    st[0]) if st else None
    box = re.search(r"box \(([-\d.e+]+), ([-\d.e+]+)\)-\(([-\d.e+]+), ([-\d.e+]+)\)", L[0])
    b = "" if not box or float(box.group(1)) > 1e20 else " (%.0f,%.0f)-(%.0f,%.0f)" % tuple(
        float(x) for x in box.groups())
    blend = "blend %s %s %s atest %s z %s" % bl.group(1, 2, 3, 4, 7) if bl else "blend ?"
    sten = "zf %s stencil %s func %s ref %s ops %s cull %s cw %s" % stz.groups() if stz else ""
    return "#%-3d %-9s p%d n%-5d vs %-8s fvf %-8s tex0 %s %sx%s fmt %s | %s | %s%s" % (
        r["no"], r["path"], r["prim"], r["n"], r["vs"] or "-", r["fvf"], r["tex0"], r["w"], r["h"], r["fmt"],
        blend, sten, b)


def main():
    path, swap = sys.argv[1], int(sys.argv[2])
    opts = sys.argv[3:]
    clears = []
    recs = parse(path, {swap}, clears)
    prog = next((o.split("=", 1)[1].upper() for o in opts if o.startswith("--program=")), None)
    texf = next((o.split("=", 1)[1].upper() for o in opts if o.startswith("--texture=")), None)
    ci = 0
    for r in recs:
        while ci < len(clears) and clears[ci][1] <= r["no"]:
            print("     clear: %s" % clears[ci][2])
            ci += 1
        if "--blended" in opts and " blend 1 " not in " " + r["lines"][1][17:] if len(r["lines"]) > 1 else False:
            continue
        if prog and r["vs"] != prog:
            continue
        if texf and r["tex0"].upper() != texf:
            continue
        print(summary(r))
    for c in clears[ci:]:
        print("     clear: %s" % c[2])


if __name__ == "__main__":
    main()
