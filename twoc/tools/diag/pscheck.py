"""Check the bridge's pixel-shader probes against a CPU evaluation (notes 18.35).

  python pscheck.py TAG.err.log [--tol=3] [--decode]

With RECOMP_D3D_BRIDGE_PSPROBE=1 (drive.py --psprobe) the bridge draws one
pixel with each distinct pixel shader the game uses, through xbox_d3d8's
combiner translator, with fixed inputs, and logs a line:

  [D3DB] ps probe N: result AARRGGBB; gaps G; v0 .. v1 .. t t0 t1 t2 t3; token ..; def <57 dwords> tm <modes>

The def is D3D__RenderState[0..56] (the XDK D3DPIXELSHADERDEF layout) and tm
RS[117] (PSTextureModes). This script evaluates the same def on the CPU with
the NV2A register-combiner rules and compares, channel by channel, within
--tol (8-bit steps). --decode prints each shader as combiner stages.

The rules (NV_register_combiners, as xemu implements them):
  - registers: ZERO 0, C0 1, C1 2, FOG 3, V0 4, V1 5, T0-T3 8-11, R0 12, R1 13,
    V1R0_SUM 14 and EF_PROD 15 (final combiner only). R0.a starts as T0.a,
    R0.rgb and R1 as 0. FOG is 0 here (the probe turns fog off).
  - an input byte: register [3:0], [4] alpha (in the alpha portion and for G:
    alpha when set, else blue), mapping [7:5].
  - PS_COMBINERINPUTS: A [31:24], B, C, D [7:0]. PS_COMBINEROUTPUTS: CD dst
    [3:0], AB dst [7:4], sum dst [11:8], CD dot 12, AB dot 13, mux 14, output
    mapping [17:15] (BIAS 1, x2 2, x2 bias 3, x4 4, /2 6).
  - mux: CD when R0.a >= 0.5 (MSB mode), else AB. Stage outputs clamp to
    [-1, 1]. PSCOMBINERCOUNT: count [7:0], MUX_MSB bit 8, UNIQUE_C0 bit 12,
    UNIQUE_C1 bit 16 (otherwise every stage reads stage 0's constant).
  - final combiner (unless ABCD and EFG are both 0, which outputs R0):
    rgb = A*B + (1-A)*C + D, alpha = G; its constants are RS 43/44; E*F is
    EF_PROD and V1+R0 is V1R0_SUM (COMPLEMENT flags 0x40/0x20, CLAMP 0x80).
  - PSTextureModes: 5 bits per stage; NONE 0 reads 0, PROJECT2D/3D and
    CUBEMAP sample (the probe's 1x1 textures are constant).
"""
import re
import sys

REG = {0: "zero", 1: "c0", 2: "c1", 3: "fog", 4: "v0", 5: "v1", 8: "t0", 9: "t1", 10: "t2", 11: "t3",
       12: "r0", 13: "r1", 14: "v1r0", 15: "ef"}
MAPS = ["", "1-", "expand", "-expand", "halfbias", "-halfbias", "signed", "-signed"]
OUTMAP = {0: "", 1: " bias", 2: " x2", 3: " x2 bias", 4: " x4", 6: " /2"}


def argb(v):
    return [((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0, ((v >> 24) & 255) / 255.0]


def clamp(x, lo, hi):
    return lo if x < lo else hi if x > hi else x


def mapping(m, x):
    if m == 0:
        return max(x, 0.0)
    if m == 1:
        return 1.0 - clamp(x, 0.0, 1.0)
    if m == 2:
        return 2.0 * max(x, 0.0) - 1.0
    if m == 3:
        return 1.0 - 2.0 * max(x, 0.0)
    if m == 4:
        return max(x, 0.0) - 0.5
    if m == 5:
        return 0.5 - max(x, 0.0)
    if m == 6:
        return x
    return -x


def outmap(m, x):
    return {0: x, 1: x - 0.5, 2: x * 2, 3: (x - 0.5) * 2, 4: x * 4, 6: x / 2}.get(m, x)


def read(regs, b, portion):
    """portion 'rgb' -> 3 values, 'a' -> 1 value."""
    v = regs[b & 15]
    m = b >> 5
    if portion == "rgb":
        xs = [v[3]] * 3 if b & 0x10 else v[:3]
    else:
        xs = [v[3]] if b & 0x10 else [v[2]]
    return [mapping(m, x) for x in xs]


def inputs4(v):
    return [(v >> 24) & 255, (v >> 16) & 255, (v >> 8) & 255, v & 255]


def evaluate(d, tm, v0, v1, tex):
    count = d[53]
    n = max(1, min(8, count & 255))
    regs = {r: [0.0] * 4 for r in range(16)}
    regs[4], regs[5] = argb(v0), argb(v1)
    for s in range(4):
        mode = (tm >> (5 * s)) & 31
        regs[8 + s] = argb(tex[s]) if mode else [0.0] * 4
    regs[12] = [0.0, 0.0, 0.0, regs[8][3]]
    for i in range(n):
        regs[1] = argb(d[10 + (i if count & 0x1000 else 0)])
        regs[2] = argb(d[18 + (i if count & 0x10000 else 0)])
        writes, blue_to_alpha = [], []
        for portion, inw, outw in (("rgb", d[34 + i], d[45 + i]), ("a", d[0 + i], d[26 + i])):
            a, b, c, dd = [read(regs, x, portion) for x in inputs4(inw)]
            cd_dst, ab_dst, sum_dst = outw & 15, (outw >> 4) & 15, (outw >> 8) & 15
            if portion == "rgb" and (outw >> 13) & 1:
                ab = [sum(x * y for x, y in zip(a, b))] * 3
            else:
                ab = [x * y for x, y in zip(a, b)]
            if portion == "rgb" and (outw >> 12) & 1:
                cd = [sum(x * y for x, y in zip(c, dd))] * 3
            else:
                cd = [x * y for x, y in zip(c, dd)]
            if (outw >> 14) & 1:
                sm = cd if regs[12][3] >= 0.5 else ab
            else:
                sm = [x + y for x, y in zip(ab, cd)]
            om = (outw >> 15) & 7
            for dst, val in ((ab_dst, ab), (cd_dst, cd), (sum_dst, sm)):
                if dst:
                    writes.append((dst, portion, [clamp(outmap(om, x), -1.0, 1.0) for x in val]))
            if portion == "rgb":        # AB/CD blue to alpha: the product's blue into its destination's alpha
                for bit, dst, val in ((19, ab_dst, ab), (18, cd_dst, cd)):
                    if (outw >> bit) & 1 and dst:
                        blue_to_alpha.append((dst, clamp(outmap(om, val[2]), -1.0, 1.0)))
        for dst, portion, val in writes:
            if portion == "rgb":
                regs[dst][:3] = val
            else:
                regs[dst][3] = val[0]
        for dst, val in blue_to_alpha:
            regs[dst][3] = val
    abcd, efg = d[8], d[9]
    if not abcd and not efg:
        return [clamp(x, 0.0, 1.0) for x in regs[12]]
    regs[1], regs[2] = argb(d[43]), argb(d[44])
    e = read(regs, (efg >> 24) & 255, "rgb")
    f = read(regs, (efg >> 16) & 255, "rgb")
    s1 = [1 - clamp(x, 0, 1) if efg & 0x40 else x for x in regs[5][:3]]
    s2 = [1 - clamp(x, 0, 1) if efg & 0x20 else x for x in regs[12][:3]]
    vs = [x + y for x, y in zip(s1, s2)]
    if efg & 0x80:
        vs = [clamp(x, 0, 1) for x in vs]
    regs[14] = vs + [0.0]
    regs[15] = [x * y for x, y in zip(e, f)] + [0.0]
    a, b, c, dd = [read(regs, x, "rgb") for x in inputs4(abcd)]
    rgb = [clamp(aa * bb + (1 - aa) * cc + ddd, 0.0, 1.0) for aa, bb, cc, ddd in zip(a, b, c, dd)]
    g = read(regs, (efg >> 8) & 255, "a")[0]
    return rgb + [clamp(g, 0.0, 1.0)]


def name_in(b, portion):
    r = REG.get(b & 15, "reg%d" % (b & 15))
    ch = ".a" if b & 0x10 else (".b" if portion == "a" else "")
    m = MAPS[b >> 5]
    return ("%s(%s%s)" % (m, r, ch)) if m else r + ch


def decode(d, tm):
    count = d[53]
    n = max(1, min(8, count & 255))
    out = ["  %d stage(s), count %08X, texture modes %s" % (n, count, [(tm >> (5 * s)) & 31 for s in range(4)])]
    for i in range(n):
        for portion, inw, outw in (("rgb", d[34 + i], d[45 + i]), ("a", d[0 + i], d[26 + i])):
            a, b, c, dd = [name_in(x, portion) for x in inputs4(inw)]
            cd_dst, ab_dst, sum_dst = outw & 15, (outw >> 4) & 15, (outw >> 8) & 15
            ab = ("%s.%s" if (outw >> 13) & 1 else "%s*%s") % (a, b)
            cd = ("%s.%s" if (outw >> 12) & 1 else "%s*%s") % (c, dd)
            op = "mux" if (outw >> 14) & 1 else "+"
            parts = []
            if ab_dst:
                parts.append("%s=%s" % (REG.get(ab_dst), ab))
            if cd_dst:
                parts.append("%s=%s" % (REG.get(cd_dst), cd))
            if sum_dst:
                parts.append("%s=%s %s %s" % (REG.get(sum_dst), ab, op, cd))
            flags = OUTMAP.get((outw >> 15) & 7, " map?") + (" blue>alpha" if outw & (3 << 18) else "")
            out.append("  s%d %-3s %s%s" % (i, portion, "; ".join(parts) or "(nothing)", flags))
    abcd, efg = d[8], d[9]
    if not abcd and not efg:
        out.append("  final: none (r0)")
    else:
        a, b, c, dd = [name_in(x, "rgb") for x in inputs4(abcd)]
        out.append("  final: rgb = %s*%s + (1-%s)*%s + %s; a = %s; E*F = %s*%s; flags %02X" %
                   (a, b, a, c, dd, name_in((efg >> 8) & 255, "a"), name_in((efg >> 24) & 255, "rgb"),
                    name_in((efg >> 16) & 255, "rgb"), efg & 255))
    return "\n".join(out)


def main():
    log = sys.argv[1]
    opts = dict(a[2:].split("=", 1) for a in sys.argv[2:] if "=" in a)
    tol = int(opts.get("tol", "3"))
    rx = re.compile(r"\[D3DB\] ps probe (\d+\.\d+): (result|FAILED) ([0-9A-F]{8}); gaps ([0-9A-F]+); v0 ([0-9A-F]{8}) "
                    r"v1 ([0-9A-F]{8}) t ([0-9A-F]{8}) ([0-9A-F]{8}) ([0-9A-F]{8}) ([0-9A-F]{8}); token ([0-9A-F]{8}); "
                    r"def ((?:[0-9A-F]{8} ?){57}) tm ([0-9A-F]{8})")
    total = passed = 0
    for line in open(log, encoding="latin1"):
        m = rx.search(line)
        if not m:
            continue
        k, status, res = m.group(1), m.group(2), int(m.group(3), 16)
        gaps = int(m.group(4), 16)
        v0, v1 = int(m.group(5), 16), int(m.group(6), 16)
        tex = [int(m.group(i), 16) for i in range(7, 11)]
        d = [int(x, 16) for x in m.group(12).split()]
        tm = int(m.group(13), 16)
        ref = evaluate(d, tm, v0, v1, tex)
        ref8 = [int(round(x * 255)) for x in ref]
        got = argb(res)
        got8 = [int(round(x * 255)) for x in got]
        diff = max(abs(a - b) for a, b in zip(ref8, got8))
        ok = status == "result" and diff <= tol
        total += 1
        passed += ok
        print("probe %s: %s  host %s  reference %s  (RGBA; max diff %d)%s token %s" %
              (k, "ok  " if ok else "DIFF", got8, ref8, diff, "; gaps %X" % gaps if gaps else "", m.group(11)))
        if ("--decode" in sys.argv and k.endswith(".0")) or not ok:
            print(decode(d, tm))
    print("%d/%d probes within %d" % (passed, total, tol))


if __name__ == "__main__":
    main()
