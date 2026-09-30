"""Compare wgcgrab grabs of the bridge window with grabs of xemu's window (step 48, notes 18.51).

  python refmatch.py match "OURS_GLOB" "XEMU_GLOB"      each xemu grab's closest grab of ours
  python refmatch.py pair OUT.png OURS.bmp XEMU.bmp [W]  the two game areas side by side, W wide each
  python refmatch.py grid OURS.bmp XEMU.bmp              mean and std per cell of a 4x4 grid, and overall

A file whose folder or name contains "xemu" is an xemu grab: its menu bar is cut off and the
4:3 picture is taken from the middle. Ours is the 640x480 client area at 225% DPI (1440x1080 at
the bottom of the grab). match compares 160x120 grey images by mean absolute difference: the
attract demo's first second is a static camera, so its frames match exactly across the two.
xemu renders at 1x here (no render-scale key in xemu.toml; `scale` there is the UI's).
"""
import glob
import sys
from PIL import Image, ImageChops, ImageStat


def game(f):
    im = Image.open(f).convert("RGB")
    w, h = im.size
    if "xemu" in f.replace("\\", "/").lower():
        g = im.crop((0, int(h * 0.105), w, h))
        gw, gh = g.size
        pw = gh * 4 // 3
        return g.crop(((gw - pw) // 2, 0, (gw + pw) // 2, gh))
    return im.crop((2, h - 1082, w - 2, h - 2))


def small(f):
    return game(f).convert("L").resize((160, 120))


def match(ours_glob, xemu_glob):
    ours = [(f, small(f)) for f in sorted(glob.glob(ours_glob))]
    for x in sorted(glob.glob(xemu_glob)):
        xs = small(x)
        best = min(ours, key=lambda o: ImageStat.Stat(ImageChops.difference(o[1], xs)).mean[0])
        print("%s  %s  %.1f" % (x, best[0], ImageStat.Stat(ImageChops.difference(best[1], xs)).mean[0]))


def pair(out, a, b, w=800):
    s = Image.new("RGB", (2 * w, w * 3 // 4))
    s.paste(game(a).resize((w, w * 3 // 4)), (0, 0))
    s.paste(game(b).resize((w, w * 3 // 4)), (w, 0))
    s.save(out)


def grid(a, b):
    a = game(a).resize((320, 240), Image.BOX)
    b = game(b).resize((320, 240), Image.BOX)
    for gy in range(4):
        row = []
        for gx in range(4):
            box = (gx * 80, gy * 60, gx * 80 + 80, gy * 60 + 60)
            sa, sb = ImageStat.Stat(a.crop(box)), ImageStat.Stat(b.crop(box))
            row.append("%3d/%3d(%2d/%2d)" % (sum(sa.mean) / 3, sum(sb.mean) / 3, sum(sa.stddev) / 3,
                                            sum(sb.stddev) / 3))
        print("  ".join(row))
    sa, sb = ImageStat.Stat(a), ImageStat.Stat(b)
    print("all: ours mean %s std %s; xemu mean %s std %s" % (
        [round(x) for x in sa.mean], [round(x) for x in sa.stddev],
        [round(x) for x in sb.mean], [round(x) for x in sb.stddev]))


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "match":
        match(sys.argv[2], sys.argv[3])
    elif cmd == "pair":
        pair(sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5]) if len(sys.argv) > 5 else 800)
    elif cmd == "grid":
        grid(sys.argv[2], sys.argv[3])
    else:
        print(__doc__)
