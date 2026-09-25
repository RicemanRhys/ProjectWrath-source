"""Summarise a RECOMP_D3D_BRIDGE=1 census log by game phase (notes 18.32).

  python census.py run.err.log [--keys]

d3d_bridge.c writes a block of "[D3DB] c <t> <interval> <total> <key>" lines
every 10 s and at each phase mark (splash routine entered/returned, main
menu). This splits the blocks into phases using markers from the same log:

  boot      until the splash routine starts
  splash    the splash routine (sub_000875A0)
  title     splash end to main menu
  menu      main menu to the first intro level file
  intro-load, intro (from INTRO1.wav), hub-load (from the Start press
  that skips the intro), hub (from THEMEL.wav after that)

A block that contains a marker is counted as "a>b" and left out of the
per-phase tables. --keys prints every key per phase.
"""
import collections
import re
import sys

XDK_PRIM = {1: "POINTLIST", 2: "LINELIST", 3: "LINELOOP", 4: "LINESTRIP", 5: "TRIANGLELIST",
            6: "TRIANGLESTRIP", 7: "TRIANGLEFAN", 8: "QUADLIST", 9: "QUADSTRIP", 10: "POLYGON"}
# XDK D3DFORMAT codes (format byte of the resource Format). From memory of the
# XDK headers; the back buffer (6) and depth (0x2A) match CreateDevice's
# parameters. Cross-check against Cxbx-Reloaded before relying on the rest.
XDK_FMT = {0x00: "L8", 0x01: "AL8", 0x02: "A1R5G5B5", 0x03: "X1R5G5B5", 0x04: "A4R4G4B4",
           0x05: "R5G6B5", 0x06: "A8R8G8B8", 0x07: "X8R8G8B8", 0x0B: "P8", 0x0C: "DXT1",
           0x0E: "DXT3", 0x0F: "DXT5", 0x10: "LIN_A1R5G5B5", 0x11: "LIN_R5G6B5",
           0x12: "LIN_A8R8G8B8", 0x13: "LIN_L8", 0x19: "A8", 0x1A: "A8L8", 0x1E: "LIN_X8R8G8B8",
           0x2A: "D24S8", 0x2C: "D16", 0x2E: "LIN_D24S8", 0x30: "LIN_D16"}

# XDK render-state and texture-stage names by index, also from memory. The
# group boundaries (57 pixel-shader, 25 simple, 35 deferred, 29 complex) and the
# complex order match the D3D library's own tables (step29 report, section 1).
_PS = (["PSALPHAINPUTS%d" % i for i in range(8)] + ["PSFINALCOMBINERINPUTSABCD", "PSFINALCOMBINERINPUTSEFG"]
       + ["PSCONSTANT0_%d" % i for i in range(8)] + ["PSCONSTANT1_%d" % i for i in range(8)]
       + ["PSALPHAOUTPUTS%d" % i for i in range(8)] + ["PSRGBINPUTS%d" % i for i in range(8)]
       + ["PSCOMPAREMODE", "PSFINALCOMBINERCONSTANT0", "PSFINALCOMBINERCONSTANT1"]
       + ["PSRGBOUTPUTS%d" % i for i in range(8)] + ["PSCOMBINERCOUNT", "PS_RESERVED", "PSDOTMAPPING", "PSINPUTTEXTURE"])
_SIMPLE = ("ZFUNC ALPHAFUNC ALPHABLENDENABLE ALPHATESTENABLE ALPHAREF SRCBLEND DESTBLEND ZWRITEENABLE "
           "DITHERENABLE SHADEMODE COLORWRITEENABLE STENCILZFAIL STENCILPASS STENCILFUNC STENCILREF "
           "STENCILMASK STENCILWRITEMASK BLENDOP BLENDCOLOR SWATHWIDTH POLYGONOFFSETZSLOPESCALE "
           "POLYGONOFFSETZOFFSET POINTOFFSETENABLE WIREFRAMEOFFSETENABLE SOLIDOFFSETENABLE").split()
_DEFERRED = ("FOGENABLE FOGTABLEMODE FOGSTART FOGEND FOGDENSITY RANGEFOGENABLE WRAP0 WRAP1 WRAP2 WRAP3 "
             "LIGHTING SPECULARENABLE LOCALVIEWER COLORVERTEX BACKSPECULARMATERIALSOURCE "
             "BACKDIFFUSEMATERIALSOURCE BACKAMBIENTMATERIALSOURCE BACKEMISSIVEMATERIALSOURCE "
             "SPECULARMATERIALSOURCE DIFFUSEMATERIALSOURCE AMBIENTMATERIALSOURCE EMISSIVEMATERIALSOURCE "
             "BACKAMBIENT AMBIENT POINTSIZE POINTSIZE_MIN POINTSPRITEENABLE POINTSCALEENABLE POINTSCALE_A "
             "POINTSCALE_B POINTSCALE_C POINTSIZE_MAX PATCHEDGESTYLE PATCHSEGMENTS SWAPFILTER").split()
_COMPLEX = ("PSTEXTUREMODES VERTEXBLEND FOGCOLOR FILLMODE BACKFILLMODE TWOSIDEDLIGHTING NORMALIZENORMALS "
            "ZENABLE STENCILENABLE STENCILFAIL FRONTFACE CULLMODE TEXTUREFACTOR ZBIAS LOGICOP "
            "EDGEANTIALIAS MULTISAMPLEANTIALIAS MULTISAMPLEMASK MULTISAMPLEMODE "
            "MULTISAMPLERENDERTARGETMODE SHADOWFUNC LINEWIDTH DXT1NOISEENABLE YUVENABLE "
            "OCCLUSIONCULLENABLE STENCILCULLENABLE ROPZCMPALWAYSREAD ROPZREAD DONOTCULLUNCOMPRESSED").split()
XDK_RS = _PS + _SIMPLE + _DEFERRED + _COMPLEX
assert len(_PS) == 57 and len(_SIMPLE) == 25 and len(_DEFERRED) == 35 and len(XDK_RS) == 146
XDK_TSS = ("ADDRESSU ADDRESSV ADDRESSW MAGFILTER MINFILTER MIPFILTER MIPMAPLODBIAS MAXMIPLEVEL "
           "MAXANISOTROPY COLORKEYOP COLORSIGN ALPHAKILL COLOROP COLORARG0 COLORARG1 COLORARG2 ALPHAOP "
           "ALPHAARG0 ALPHAARG1 ALPHAARG2 RESULTARG TEXTURETRANSFORMFLAGS BUMPENVMAT00 BUMPENVMAT01 "
           "BUMPENVMAT11 BUMPENVMAT10 BUMPENVLSCALE BUMPENVLOFFSET TEXCOORDINDEX BORDERCOLOR "
           "COLORKEYCOLOR").split()
assert len(XDK_TSS) == 31

log = sys.argv[1]
show_keys = "--keys" in sys.argv
lines = open(log, encoding="utf-8", errors="replace").read().splitlines()

# Phase markers, in log order.
marks = []                       # (line index, phase that starts there)
start_seen = False
for i, l in enumerate(lines):
    if "mark: splash routine sub_000875A0 entered" in l:
        marks.append((i, "splash"))
    elif "mark: splash routine sub_000875A0 returned" in l:
        marks.append((i, "title"))
    elif "mark: main menu opens" in l:
        marks.append((i, "menu"))
    elif re.search(r"\\levels\\b\\intro\\introchars\.dat", l) and not any(p == "intro-load" for _, p in marks):
        marks.append((i, "intro-load"))
    elif l.endswith("INTRO1.wav") and not any(p == "intro" for _, p in marks):
        marks.append((i, "intro"))
    elif re.search(r'\[DEV\] input file \(\+[\d.]+ s\): "buttons=0x10"', l) and not start_seen:
        start_seen = True
        marks.append((i, "hub-load"))
    elif l.endswith("THEMEL.wav") and start_seen and not any(p == "hub" for _, p in marks):
        marks.append((i, "hub"))

def phase_at(idx):
    ph = "boot"
    for i, p in marks:
        if i <= idx:
            ph = p
    return ph

# Blocks: header line "census +t s", then its c lines. A block covers the log
# from the previous header to its own.
blocks = []
prev_hdr, prev_t = -1, 0.0
for i, l in enumerate(lines):
    m = re.match(r"\[D3DB\] census \+([\d.]+) s", l)
    if not m:
        continue
    t = float(m.group(1))
    keys = {}
    # Other threads' lines can land inside a block, so collect every c line
    # carrying this block's time until the next header.
    tag = "[D3DB] c %s " % m.group(1)
    j = i + 1
    while j < len(lines) and not lines[j].startswith("[D3DB] census +"):
        if lines[j].startswith(tag):
            parts = lines[j].split(" ", 5)
            keys[parts[5]] = int(parts[3])
        j += 1
    p0, p1 = phase_at(prev_hdr + 1), phase_at(i)
    # A mark line right before the header closes the block at the mark.
    inner = [p for k, p in marks if prev_hdr < k < i - 1]
    label = p0 if not inner else p0 + ">" + inner[-1]
    blocks.append((label, prev_t, t, keys))
    prev_hdr, prev_t = i, t

order = ["boot", "splash", "title", "menu", "intro-load", "intro", "hub-load", "hub"]
agg = collections.OrderedDict()
dur = collections.Counter()
print("blocks:")
for label, t0, t1, keys in blocks:
    print("  %6.1f-%6.1f  %-18s swaps %4d  draws %6d" % (
        t0, t1, label, keys.get("call Swap", 0),
        sum(v for k, v in keys.items() if k.startswith("draw ") and " prim=" in k)))
    if ">" in label:
        continue
    agg.setdefault(label, collections.Counter()).update(keys)
    dur[label] += t1 - t0

def section(ph, c, d):
    swaps = c.get("call Swap", 0)
    per = (lambda n: n / swaps) if swaps else (lambda n: float("nan"))
    out = ["", "## %s: %.1f s, %d swaps (%.1f/s)" % (ph, d, swaps, swaps / d if d else 0)]
    calls = sorted((k[5:], v) for k, v in c.items() if k.startswith("call "))
    out.append("calls (per swap): " + ", ".join("%s %d (%.1f)" % (k, v, per(v)) for k, v in calls))
    # "verts" keys: vertices for UP and Immediate, indices for Indexed.
    draws = [(k, v) for k, v in c.items() if k.startswith("draw ") and " prim=" in k]
    out.append("draws: " + ", ".join("%s %s %d (%.1f/swap)" % (
        k.split()[1], XDK_PRIM.get(int(k.split("prim=")[1]), "?"), v, per(v)) for k, v in sorted(draws)))
    for pre in ("verts ", "draw rt=", "draw vs=", "draw ps=", "draw textures=", "stride ", "streams ",
                "tid draw", "imm ", "clear ", "swap ", "vsc ", "fastpath", "copyrects", "sb ", "probe ",
                "draw fastpath"):
        items = sorted((k, v) for k, v in c.items() if k.startswith(pre))
        if items:
            out.append("%s: %s" % (pre.strip(), "; ".join("%s x%d" % (k[len(pre):].strip() or k, v) for k, v in items[:24])
                                     + (" ... (%d keys)" % len(items) if len(items) > 24 else "")))
    fmts = collections.Counter()
    for k, v in c.items():
        m = re.match(r"tex (\d) fmt=([0-9A-F]{2}) (\d+)x(\d+) lv=(\d+)(.*)", k)
        if m:
            f = int(m.group(2), 16)
            kind = ("cube " if "cube" in m.group(6) else "") + ("linear " if "linear" in m.group(6) else "")
            fmts["%02X %s%s" % (f, kind, XDK_FMT.get(f, "?"))] += v
    if fmts:
        out.append("texture formats bound (changes): " + ", ".join("%s x%d" % kv for kv in fmts.most_common()))
    vs = [k for k in c if k.startswith("vs ")]
    out.append("vertex shaders: %d FVFs %s; %d program objects" % (
        sum(1 for k in vs if "fvf" in k), sorted(k[7:] for k in vs if "fvf" in k),
        sum(1 for k in vs if "program" in k)))
    rs = sorted({int(k[3:6]) for k in c if k.startswith("rs ")})
    tss = sorted({(int(k[4]), int(k[6:8])) for k in c if k.startswith("tss ")})
    out.append("RS indices that changed between draws (%d): %s" % (
        len(rs), ", ".join("%d %s" % (i, XDK_RS[i]) for i in rs)))
    out.append("TSS stage.index that changed (%d): %s" % (
        len(tss), ", ".join("%d.%d %s" % (s, i, XDK_TSS[i] if i < 31 else "?") for s, i in tss)))
    for pre in ("rt ", "zs ", "viewport ", "lock "):
        items = sorted((k, v) for k, v in c.items() if k.startswith(pre))
        if items:
            out.append("%s(%d keys): %s" % (pre, len(items), "; ".join("%s x%d" % (k[len(pre):], v) for k, v in items[:12])
                                            + (" ..." if len(items) > 12 else "")))
    if show_keys:
        out += ["  %8d %s" % (v, k) for k, v in sorted(c.items())]
    return "\n".join(out)

for ph in order:
    if ph in agg:
        print(section(ph, agg[ph], dur[ph]))
