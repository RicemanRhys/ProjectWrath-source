"""Apply the bridge's texel_patch to a dumped vertex program and evaluate it (step 52, notes 18.55).

  python texpatch.py VSDUMPLOG STAGE WIDTH HEIGHT

VSDUMPLOG holds [D3DB] vsdump lines (RECOMP_D3D_BRIDGE_VSDUMP=1, with the draw log for per-draw dumps).
Patches oT<STAGE>.xy the way d3d_bridge.c texel_patch does (a free temp, the highest unread constant,
MUL oTn.xy, Rk, c[K] appended) for a WIDTHxHEIGHT linear texture, then runs the patched program
on the dumped vertices with vscheck.py's NV2A evaluator and prints oT<STAGE>, oT1, oT2 and oD0.
"""
import sys
import numpy as np
import vscheck as V


def VF(w, sh, n):
    return (w >> sh) & ((1 << n) - 1)


def patch(code, consts, s, wdt, hgt):
    n_insns = len(code) // 4
    code = list(code)
    used = 1 << 12
    cread = set()
    rel_lo = 192
    touch = []
    for i in range(n_insns):
        w = code[4 * i:4 * i + 4]
        mac, ilu, cm = VF(w[1], 21, 4), VF(w[1], 25, 3), VF(w[3], 28, 2)
        am, bm, c = VF(w[2], 26, 2), VF(w[2], 11, 2), VF(w[1], 13, 8)
        t = 0
        if am == 1: t |= 1 << VF(w[2], 28, 4)
        if bm == 1: t |= 1 << VF(w[2], 13, 4)
        if cm == 1: t |= 1 << ((VF(w[2], 0, 2) << 2) | VF(w[3], 30, 2))
        if mac and VF(w[3], 24, 4): t |= 1 << VF(w[3], 20, 4)
        if ilu and VF(w[3], 16, 4): t |= 1 << (1 if mac else VF(w[3], 20, 4))
        touch.append(t)
        used |= t
        if 3 in (am, bm, cm):
            if VF(w[3], 1, 1):
                rel_lo = min(rel_lo, c)
            else:
                cread.add(c)
    writers = []
    for i in range(n_insns):
        w3, w1 = code[4 * i + 3], code[4 * i + 1]
        if not VF(w3, 11, 1) or VF(w3, 3, 8) != 9 + s or not (VF(w3, 12, 4) & 0xC):
            continue
        writers.append(i)
        if (VF(w1, 21, 4) or VF(w3, 16, 4)) if VF(w3, 2, 1) else VF(w3, 24, 4):
            return None, "writer already writes a temp"
    if not writers:
        return None, "no writer"
    first = writers[0]
    late = 0
    for i in range(first + 1, n_insns):
        late |= touch[i]
    k = 11
    while k >= 2 and used & (1 << k):
        k -= 1
    if k < 2:
        k = 11
        while k >= 2 and late & (1 << k):
            k -= 1
    K = 191
    while K > 59 and (K in cread or rel_lo <= K < rel_lo + 64):
        K -= 1
    for i in writers:
        w = code[4 * i:4 * i + 4]
        om = VF(w[3], 12, 4)
        if VF(w[3], 2, 1):
            w[3] = (w[3] & ~(0xF << 16) & ~(0xF << 20)) | (om << 16) | (k << 20)
        else:
            w[3] = (w[3] & ~(0xF << 24) & ~(0xF << 20)) | (om << 24) | (k << 20)
        code[4 * i:4 * i + 4] = w
    code[4 * (n_insns - 1) + 3] &= ~1
    code += [0, (2 << 21) | (K << 13) | 0x1B, (k << 28) | (1 << 26) | (0x1B << 17) | (3 << 11) | (0x1B << 2),
             (1 << 28) | (0xC << 12) | (1 << 11) | ((9 + s) << 3) | 1]
    consts = consts.copy().reshape(192, 4)
    consts[K] = (1.0 / wdt, 1.0 / hgt, 1, 1)
    return (code, consts.reshape(-1)), "temp R%d constant %d, %d writers" % (k, K, len(writers))


progs, _ = V.parse(sys.argv[1])
s, wdt, hgt = int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
for h, p in progs.items():
    if "code" not in p:
        continue
    res, why = patch(p["code"], p["const"], s, wdt, hgt)
    print(h, why)
    if not res:
        continue
    code, consts = res
    prog = [V.decode(code[i:i + 4]) for i in range(0, len(code), 4)]
    for i, d in enumerate(prog[-3:]):
        print("   ", V.disasm(d))
    for k, v in enumerate(p["verts"][:6]):
        o = V.Vm(consts).run(prog, v)
        print("  vertex %d: oT%d %s  oT1 %s oT2 %s oD0 %s" % (k, s, np.round(o["oT%d" % s], 4), np.round(o["oT1"], 4), np.round(o["oT2"], 4), np.round(o["oD0"], 4)))
