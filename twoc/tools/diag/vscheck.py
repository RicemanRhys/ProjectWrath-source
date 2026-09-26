"""Decode and evaluate the game's NV2A vertex programs (S5, notes 18.38).

  python vscheck.py ERRLOG [--eval] [--probe] [--quiet]

Reads the [D3DB] vsdump blocks that RECOMP_D3D_BRIDGE_VSDUMP=1 (drive.py
--vsdump) writes, one per distinct program: microcode, declaration, the
whole constant shadow, the viewport, and the first vertices as fetched.

For each program it prints a disassembly, the registers and constants it
reads and the outputs it writes. --eval runs the program on the CPU on the
dumped vertices (float32, NV2A rules as xemu implements them) and prints
the outputs. --probe also reads the [D3DB] vsprobe lines, where the host
drew the same vertices with the translated shader and read back where each
one landed (d3d_bridge.c vs_probe_once), and compares positions and colours.

The decoder is independent of xbox_d3d8's d3d8_vsh.c: field positions are
xemu's (hw/xbox/nv2a vsh.c): dword 0 unused, then

  dword 1: ILU 25-27, MAC 21-24, CONST 13-20, V 9-12, A: neg 8, swizzle 6-7/4-5/2-3/0-1
  dword 2: A: R 28-31, mux 26-27; B: neg 25, swizzle 23-24/21-22/19-20/17-18, R 13-16,
           mux 11-12; C: neg 10, swizzle 8-9/6-7/4-5/2-3, R high 0-1
  dword 3: C: R low 30-31, mux 28-29; MAC mask 24-27, R 20-23, ILU mask 16-19,
           O mask 12-15, ORB 11, address 3-10, O mux 2, A0X 1, FINAL 0

mux: 1 temp, 2 input, 3 constant. Masks: bit 3 = x .. bit 0 = w. When MAC
and ILU both run, the ILU's temp destination is R1. R12 is oPos.
"""
import math
import re
import sys

import numpy as np

F = np.float32
MAC = ["NOP", "MOV", "MUL", "ADD", "MAD", "DP3", "DPH", "DP4", "DST", "MIN", "MAX", "SLT", "SGE", "ARL"]
ILU = ["NOP", "MOV", "RCP", "RCC", "RSQ", "EXP", "LOG", "LIT"]
OUTS = {0: "oPos", 3: "oD0", 4: "oD1", 5: "oFog", 6: "oPts", 7: "oB0", 8: "oB1",
        9: "oT0", 10: "oT1", 11: "oT2", 12: "oT3"}
SWZ = "xyzw"


def fld(w, word, start, n):
    return (w[word] >> start) & ((1 << n) - 1)


def decode(w):
    """One instruction (4 dwords) -> dict."""
    d = dict(ilu=fld(w, 1, 25, 3), mac=fld(w, 1, 21, 4), const=fld(w, 1, 13, 8), v=fld(w, 1, 9, 4))
    d["src"] = [
        dict(neg=fld(w, 1, 8, 1), swz=[fld(w, 1, 6, 2), fld(w, 1, 4, 2), fld(w, 1, 2, 2), fld(w, 1, 0, 2)],
             r=fld(w, 2, 28, 4), mux=fld(w, 2, 26, 2)),
        dict(neg=fld(w, 2, 25, 1), swz=[fld(w, 2, 23, 2), fld(w, 2, 21, 2), fld(w, 2, 19, 2), fld(w, 2, 17, 2)],
             r=fld(w, 2, 13, 4), mux=fld(w, 2, 11, 2)),
        dict(neg=fld(w, 2, 10, 1), swz=[fld(w, 2, 8, 2), fld(w, 2, 6, 2), fld(w, 2, 4, 2), fld(w, 2, 2, 2)],
             r=(fld(w, 2, 0, 2) << 2) | fld(w, 3, 30, 2), mux=fld(w, 3, 28, 2)),
    ]
    d.update(mac_mask=fld(w, 3, 24, 4), r=fld(w, 3, 20, 4), ilu_mask=fld(w, 3, 16, 4),
             o_mask=fld(w, 3, 12, 4), orb=fld(w, 3, 11, 1), addr=fld(w, 3, 3, 8), omux=fld(w, 3, 2, 1),
             a0x=fld(w, 3, 1, 1), final=fld(w, 3, 0, 1))
    return d


def mask_s(m):
    return "".join(c for i, c in enumerate("xyzw") if m & (8 >> i))


def src_s(d, i):
    s = d["src"][i]
    if s["mux"] == 1:
        name = "R%d" % s["r"] if s["r"] != 12 else "oPos"
    elif s["mux"] == 2:
        name = "v%d" % d["v"]
    elif s["mux"] == 3:
        name = "c[%s%d]" % ("a0+" if d["a0x"] else "", d["const"])
    else:
        name = "?"
    sw = "".join(SWZ[k] for k in s["swz"])
    return ("-" if s["neg"] else "") + name + ("" if sw == "xyzw" else "." + sw)


def mac_srcs(op):
    return {"MOV": [0], "MUL": [0, 1], "ADD": [0, 2], "MAD": [0, 1, 2], "DP3": [0, 1], "DPH": [0, 1], "DP4": [0, 1],
            "DST": [0, 1], "MIN": [0, 1], "MAX": [0, 1], "SLT": [0, 1], "SGE": [0, 1], "ARL": [0]}.get(op, [])


def out_name(d):
    if d["orb"] == 0:
        return "c[%s%d]" % ("a0+" if d["a0x"] else "", d["addr"])
    return OUTS.get(d["addr"] & 0xF, "o?%d" % d["addr"])


def disasm(d):
    parts = []
    mac, ilu = MAC[d["mac"]] if d["mac"] < len(MAC) else "MAC?%d" % d["mac"], ILU[d["ilu"]]
    if mac != "NOP":
        dst = []
        if d["mac_mask"] and mac != "ARL":
            dst.append("R%d.%s" % (d["r"], mask_s(d["mac_mask"])))
        if d["o_mask"] and d["omux"] == 0:
            dst.append("%s.%s" % (out_name(d), mask_s(d["o_mask"])))
        if mac == "ARL":
            dst = ["a0.x"]
        parts.append("%s %s, %s" % (mac, "+".join(dst) or "-", ", ".join(src_s(d, i) for i in mac_srcs(mac))))
    if ilu != "NOP":
        dst = []
        if d["ilu_mask"]:
            dst.append("R%d.%s" % (1 if mac != "NOP" else d["r"], mask_s(d["ilu_mask"])))
        if d["o_mask"] and d["omux"] == 1:
            dst.append("%s.%s" % (out_name(d), mask_s(d["o_mask"])))
        parts.append("%s %s, %s" % (ilu, "+".join(dst) or "-", src_s(d, 2)))
    return " : ".join(parts) + (" [final]" if d["final"] else "")


def mul(a, b):
    """Products with 0 * anything = 0 (so 0 * inf isn't NaN), as the host GPU's
    later MIN/MAX would also absorb; an assumption about the NV2A, used only
    where a program multiplies by an RSQ/RCP of 0."""
    with np.errstate(all="ignore"):
        r = a * b
    r[(a == 0) | (b == 0)] = 0
    return r


class Vm:
    """One vertex through a program, float32."""

    def __init__(self, consts):
        self.c = np.array(consts, dtype=F).reshape(192, 4).copy()

    def run(self, prog, v):
        R = np.zeros((13, 4), dtype=F)
        R[12] = F(0), F(0), F(0), F(1)
        out = {n: np.array([0, 0, 0, 1], dtype=F) for n in OUTS.values()}
        out["oPos"] = R[12]
        a0 = 0
        for d in prog:
            def rd(i):
                s = d["src"][i]
                if s["mux"] == 1:
                    val = R[s["r"]]
                elif s["mux"] == 2:
                    val = v.get(d["v"], np.array([0, 0, 0, 1], dtype=F))
                elif s["mux"] == 3:
                    k = d["const"] + (a0 if d["a0x"] else 0)
                    val = self.c[k] if 0 <= k < 192 else np.zeros(4, dtype=F)
                else:
                    val = np.zeros(4, dtype=F)
                val = np.array([val[k] for k in s["swz"]], dtype=F)
                return -val if s["neg"] else val

            mac, ilu = MAC[d["mac"]] if d["mac"] < len(MAC) else "NOP", ILU[d["ilu"]]
            mres = ires = None
            if mac != "NOP":
                a = rd(0)
                b = rd(1) if 1 in mac_srcs(mac) else None
                c = rd(2) if 2 in mac_srcs(mac) else None
                if mac == "MOV": mres = a
                elif mac == "MUL": mres = mul(a, b)
                elif mac == "ADD": mres = a + c
                elif mac == "MAD": mres = mul(a, b) + c
                elif mac == "DP3": mres = np.full(4, F(mul(a, b)[:3].sum(dtype=F)), dtype=F)
                elif mac == "DPH": mres = np.full(4, F(mul(a, b)[:3].sum(dtype=F) + b[3]), dtype=F)
                elif mac == "DP4": mres = np.full(4, F(mul(a, b).sum(dtype=F)), dtype=F)
                elif mac == "DST": mres = np.array([1, a[1] * b[1], a[2], b[3]], dtype=F)
                elif mac == "MIN": mres = np.fmin(a, b)
                elif mac == "MAX": mres = np.fmax(a, b)
                elif mac == "SLT": mres = (a < b).astype(F)
                elif mac == "SGE": mres = (a >= b).astype(F)
                elif mac == "ARL": a0 = int(math.floor(float(a[0])))
            if ilu != "NOP":
                x = rd(2)
                s = float(x[0])
                with np.errstate(all="ignore"):
                    if ilu == "MOV": ires = x
                    elif ilu == "RCP": ires = np.full(4, F(1.0) / F(s) if s != 0 else F(np.inf), dtype=F)
                    elif ilu == "RCC":
                        r = 1.0 / s if s != 0 else 1.884467e19
                        r = min(max(r, 5.42101e-20), 1.884467e19) if r > 0 else max(min(r, -5.42101e-20), -1.884467e19)
                        ires = np.full(4, F(r), dtype=F)
                    elif ilu == "RSQ": ires = np.full(4, F(1.0 / math.sqrt(abs(s))) if s != 0 else F(np.inf), dtype=F)
                    elif ilu == "EXP":
                        fl = math.floor(s)
                        ires = np.array([2.0 ** fl, s - fl, 2.0 ** s, 1.0], dtype=F)
                    elif ilu == "LOG":
                        t = abs(s)
                        if t == 0:
                            ires = np.array([-np.inf, 1, -np.inf, 1], dtype=F)
                        else:
                            e = math.floor(math.log2(t))
                            ires = np.array([e, t / 2.0 ** e, math.log2(t), 1], dtype=F)
                    elif ilu == "LIT":
                        t = np.array([1, 0, 0, 1], dtype=F)
                        if x[0] > 0:
                            t[1] = x[0]
                            if x[1] > 0:
                                t[2] = F(float(x[1]) ** float(x[3]))
                        ires = t
            # writes, after both units have read
            if mres is not None and d["mac_mask"]:
                for k in range(4):
                    if d["mac_mask"] & (8 >> k):
                        R[d["r"]][k] = mres[k]
            if ires is not None and d["ilu_mask"]:
                rr = 1 if mac != "NOP" else d["r"]
                for k in range(4):
                    if d["ilu_mask"] & (8 >> k):
                        R[rr][k] = ires[k]
            if d["o_mask"]:
                res = ires if d["omux"] == 1 else mres
                if res is not None:
                    if d["orb"] == 0:
                        k = d["addr"] + (a0 if d["a0x"] else 0)
                        for j in range(4):
                            if d["o_mask"] & (8 >> j) and 0 <= k < 192:
                                self.c[k][j] = res[j]
                    else:
                        name = OUTS.get(d["addr"] & 0xF)
                        tgt = R[12] if name == "oPos" else out.get(name)
                        if tgt is not None:
                            for j in range(4):
                                if d["o_mask"] & (8 >> j):
                                    tgt[j] = res[j]
            if d["final"]:
                break
        out["oPos"] = R[12].copy()
        return out


def parse(path):
    progs = {}
    probes = []
    for line in open(path, errors="replace"):
        m = re.match(r"\[D3DB\] vsdump ([0-9A-F]{8}(?:\.\d+)?) (\w+)(.*)", line)
        if m:
            h, kind, rest = m.group(1), m.group(2), m.group(3)
            p = progs.setdefault(h, dict(hash=h, attrs=[], verts=[]))
            if kind == "path":
                p["head"] = line.split("vsdump %s " % h, 1)[1].strip()
            elif kind == "code":
                p["code"] = [int(x, 16) for x in rest.split()]
            elif kind == "const":
                p["const"] = np.array([int(x, 16) for x in rest.split()], dtype=np.uint32).view(F)
            elif kind == "attr":
                p["attrs"].append(rest.strip())
            elif kind == "viewport":
                p["viewport"] = rest.split()
            elif kind == "vertex":
                vm = {}
                for a in re.findall(r"v(\d+)=([^ ]+)", rest):
                    if a[1] != "?":
                        vm[int(a[0])] = np.array([float(x) for x in a[1].split(",")], dtype=F)
                p["verts"].append(vm)
        m = re.match(r"\[D3DB\] vsprobe ([0-9A-F]{8}) vertex (\d+): (.*)", line)
        if m:
            probes.append((m.group(1), int(m.group(2)), m.group(3)))
    return progs, probes


def main():
    path = sys.argv[1]
    flags = set(sys.argv[2:])
    progs, probes = parse(path)
    for h, p in progs.items():
        if "code" not in p:
            continue
        code = p["code"]
        prog = [decode(code[i:i + 4]) for i in range(0, len(code), 4)]
        ins, cs, outs = set(), set(), set()
        for d in prog:
            used = mac_srcs(MAC[d["mac"]] if d["mac"] < len(MAC) else "NOP") + ([2] if d["ilu"] else [])
            for i in used:
                s = d["src"][i]
                if s["mux"] == 2:
                    ins.add(d["v"])
                elif s["mux"] == 3:
                    cs.add(("a0+" if d["a0x"] else "") + str(d["const"]))
            if d["o_mask"]:
                outs.add(out_name(d))
        print("== program %s: %s" % (h, p.get("head", "")))
        print("   reads v%s; constants %s; writes %s" % (sorted(ins), " ".join(sorted(cs, key=lambda x: int(x.split("+")[-1]))),
                                                        " ".join(sorted(outs))))
        for a in p["attrs"]:
            print("   attr " + a)
        if "--quiet" not in flags:
            for i, d in enumerate(prog):
                print("   %3d  %s" % (i, disasm(d)))
        if "--eval" in flags or "--probe" in flags:
            vp = [int(x) for x in p["viewport"][:4]]
            c = p["const"].reshape(192, 4)
            for k, v in enumerate(p["verts"]):
                o = Vm(p["const"]).run(prog, v)
                pos = o["oPos"]
                print("   vertex %d: oPos (%.4f, %.4f, %.6g, %.6g) oD0 %s oT0 %s" % (
                    k, pos[0], pos[1], pos[2], pos[3], np.round(o["oD0"], 4), np.round(o["oT0"], 4)))
                for ph, pk, rest in probes:
                    if ph == h and pk == k:
                        print("      host: %s" % rest)
                        check(stats, h, k, pos, o["oD0"], c, vp, rest)
            print("   viewport %s" % vp)
    if "--probe" in flags:
        print("probe: %d vertices compared: %d exact (pixel and colour within 1), %d on a pixel edge (not decided),"
              " %d outside the target and not lit, %d FAILED" % (
                  sum(stats.values()), stats["exact"], stats["tie"], stats["outside"], stats["fail"]))


stats = {"exact": 0, "tie": 0, "outside": 0, "fail": 0}


def check(stats, h, k, pos, d0, c, vp, rest):
    """Where the host should have lit a point at screen position pos: the
    translator maps it back through c58/c59, x_ndc = (pos - c59) / c58, and the
    host viewport maps that to pixel coordinates X + (x_ndc + 1) W / 2 and
    Y + (1 - y_ndc) H / 2; a one-pixel point lights floor() of those (D3D11
    pixel centres at .5). Colour: saturated oD0 as 8 bits."""
    m = re.match(r"lit (-?\d+) pixel (-?\d+) (-?\d+) colour ([0-9A-F]{8})", rest)
    if not m:
        return
    lit, hx, hy, col = int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4), 16)
    X, Y, W, H = vp
    sc = [c[58][i] if c[58][i] != 0 else 1 for i in range(2)]
    nx = (float(pos[0]) - c[59][0]) / sc[0]
    ny = (float(pos[1]) - c[59][1]) / sc[1]
    fx = X + (nx + 1) * W / 2
    fy = Y + (1 - ny) * H / 2
    ex, ey = math.floor(fx), math.floor(fy)
    tie = min(abs(fx - round(fx)), abs(fy - round(fy))) < 1e-3
    inside = 0 <= ex < X + W and 0 <= ey < Y + H
    want = [int(round(min(max(float(x), 0.0), 1.0) * 255)) for x in (d0[3], d0[0], d0[1], d0[2])]
    got = [(col >> s) & 255 for s in (24, 16, 8, 0)]
    col_ok = all(abs(a - b) <= 1 for a, b in zip(want, got))
    if tie:
        verdict = "tie"
    elif not inside:
        verdict = "outside" if lit == 0 else "fail"
    else:
        verdict = "exact" if lit == 1 and (hx, hy) == (ex, ey) and col_ok else "fail"
    stats[verdict] += 1
    print("      expected pixel %d %d (%.3f, %.3f) colour %02X%02X%02X%02X: %s" % (
        ex, ey, fx, fy, want[0], want[1], want[2], want[3], verdict.upper()))


if __name__ == "__main__":
    main()
