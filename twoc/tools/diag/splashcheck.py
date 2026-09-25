"""Check the bridge's splash frames against the executor's (notes 18.34, Option B S3).

  python splashcheck.py ERRLOG DUMPDIR [--ref=shot000.bmp] [--tol=8]

The splash is one textured quad a frame (the Universal logo, DXT5, MODULATE
with the vertex colour) plus an untextured quad that multiplies to nothing.
The fade in and out is the vertex colour: grey FF000000 .. FFFFFFFF.

The executor's dumps can't be compared frame for frame: it takes them in the
middle of a frame, only for the first 8 draws, and it ignores the vertex
colour, so all of them show the texture at full brightness. That image is the
reference. Each host dump of a splash frame is compared with the reference
scaled by that frame's vertex colour, which the bridge logs as
"[D3DB] ... UP draw N at guest swap S: tex0 set, vertex 0 diffuse XXXXXXXX".
A host dump written at "guest swap N" shows the draws logged at swap N - 1.

Needs a run with RECOMP_D3D_BRIDGE=1, RECOMP_FB_DUMP and preferably
RECOMP_D3D_BRIDGE_DUMP_EVERY (drive.py --bridge --dump=DIR --dump-every=1).
Prints one line per splash frame and whether each reaches 99% within tol.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fbcompare import read_bmp  # noqa: E402


def main(argv):
    opts = dict(a[2:].split("=", 1) for a in argv if a.startswith("--") and "=" in a)
    args = [a for a in argv if not a.startswith("--")]
    if len(args) != 2:
        print(__doc__)
        return 2
    log, d = args
    tol = int(opts.get("tol", "8"))
    ref_path = os.path.join(d, opts.get("ref", "shot000.bmp"))

    diffuse = {}                                      # guest swap -> textured quad's grey
    dumps = []                                        # (file, guest swap)
    for line in open(log, errors="replace"):
        m = re.search(r"UP draw \d+ at guest swap (\d+): tex0 set, vertex 0 diffuse ([0-9A-F]{8})", line)
        if m:
            diffuse[int(m.group(1))] = int(m.group(2), 16)
        m = re.search(r"host dump .*[\\/](host\d+\.bmp) at \+[\d.]+ s, guest swap (\d+)", line)
        if m:
            dumps.append((m.group(1), int(m.group(2))))

    w, h, ref = read_bmp(ref_path)
    others = sorted(f for f in os.listdir(d) if f.startswith("shot") and f.endswith(".bmp"))
    same = [f for f in others if read_bmp(os.path.join(d, f))[2] == ref]
    print("reference %s; executor dumps identical to it: %s" % (os.path.basename(ref_path), " ".join(same)))

    checked = passed = 0
    for name, swap in dumps:
        c = diffuse.get(swap - 1)
        if c is None:
            continue                                  # not a splash frame
        k = [(c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF]   # R, G, B
        hw, hh, host = read_bmp(os.path.join(d, name))
        if (hw, hh) != (w, h):
            print("%s: size %dx%d" % (name, hw, hh))
            continue
        within = 0
        worst = 0
        for i in range(0, len(ref), 3):
            m = 0
            for ch in range(3):                       # BGR in the file
                exp = (ref[i + ch] * k[2 - ch] + 127) // 255
                m = max(m, abs(host[i + ch] - exp))
            worst = max(worst, m)
            within += m <= tol
        pct = 100.0 * within / (w * h)
        checked += 1
        passed += pct >= 99.0
        print("%s  guest swap %3d  diffuse %08X  within %d: %6.2f%%  max %3d  %s" % (
            name, swap, c, tol, pct, worst, "ok" if pct >= 99.0 else "FAIL"))
    print("%d splash frames checked, %d with at least 99%% of pixels within %d" % (checked, passed, tol))
    return 0 if checked and passed == checked else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
