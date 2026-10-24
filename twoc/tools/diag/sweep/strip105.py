"""strip.py DIR OUT FIRST LAST [STEP] [COLS] [x0,y0,x1,y1] [SCALE] -- frames FIRST..LAST (indices in sorted order) as a labelled sheet."""
import sys, os, glob
from PIL import Image, ImageDraw
d, out, a, b = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
step = int(sys.argv[5]) if len(sys.argv) > 5 else 1
cols = int(sys.argv[6]) if len(sys.argv) > 6 else 6
box = tuple(int(v) for v in sys.argv[7].split(",")) if len(sys.argv) > 7 and sys.argv[7] != "-" else None
sc = float(sys.argv[8]) if len(sys.argv) > 8 else 1.0
fs = sorted(glob.glob(os.path.join(d, "*.bmp")) + glob.glob(os.path.join(d, "*.png")))
sel = fs[a:b + 1:step]
ims = []
for f in sel:
    im = Image.open(f).convert("RGB")
    if box: im = im.crop(box)
    if sc != 1.0: im = im.resize((int(im.width * sc), int(im.height * sc)), Image.NEAREST)
    ims.append((os.path.basename(f)[:22], im))
w, h = ims[0][1].size
rows = (len(ims) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (w + 4), rows * (h + 16)), (40, 40, 40))
dr = ImageDraw.Draw(sheet)
for i, (n, im) in enumerate(ims):
    x, y = (i % cols) * (w + 4), (i // cols) * (h + 16)
    sheet.paste(im, (x, y + 14))
    dr.text((x + 2, y + 1), n, fill=(255, 255, 0))
sheet.save(out)
print(out, sheet.size, len(ims))
