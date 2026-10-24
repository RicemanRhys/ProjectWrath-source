"""flagsheet.py OUT RUNDIR... -- step 106: each automatic save's flagged picture (from its info.txt) beside its
neighbours, one row per save."""
import sys, os, glob, re
from PIL import Image, ImageDraw
rows = []
for rd in sys.argv[2:]:
    for cap in sorted(glob.glob(os.path.join(rd, "roll", "rolling_*"))):
        info = open(os.path.join(cap, "info.txt"), encoding="utf-8", errors="replace").read()
        m = re.search(r"guest swap (\d+) \((\d+) of (\d+)\)", info)
        if not m or int(m.group(1)) < int(os.environ.get('MINSWAP', 0)):
            continue
        fs = sorted(glob.glob(os.path.join(cap, "frames", "*.bmp")))
        key = "_s%06d_%s-of-%s" % (int(m.group(1)), m.group(2), m.group(3))
        i = [k for k, f in enumerate(fs) if key in f]
        if not i:
            continue
        i = i[0]
        row = [Image.open(fs[j]).convert("RGB") for j in (i - 1, i, i + 1) if 0 <= j < len(fs)]
        ImageDraw.Draw(row[1 if len(row) > 1 else 0]).text((2, 2), os.path.basename(rd) + key, fill=(255, 255, 0))
        rows.append(row)
sh = Image.new("RGB", (480 * 3, 270 * max(1, len(rows))))
for r, row in enumerate(rows):
    for c, im in enumerate(row):
        sh.paste(im, (c * 480, r * 270))
sh.save(sys.argv[1]); print(sys.argv[1], len(rows))
