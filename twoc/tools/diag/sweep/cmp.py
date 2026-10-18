"""cmp.py OUT X0 Y0 X1 Y1 SCALE IMG... -- the same crop of each image, scaled, side by side with its name."""
import sys, os
from PIL import Image, ImageDraw

out, x0, y0, x1, y1, k = sys.argv[1], *map(int, sys.argv[2:6]), float(sys.argv[6])
ims = sys.argv[7:]
w, h = int((x1 - x0) * k), int((y1 - y0) * k)
cols = min(len(ims), 3)
rows = (len(ims) + cols - 1) // cols
sheet = Image.new('RGB', (w * cols, (h + 16) * rows), 'black')
d = ImageDraw.Draw(sheet)
for i, p in enumerate(ims):
    im = Image.open(p).convert('RGB').crop((x0, y0, x1, y1)).resize((w, h), Image.LANCZOS)
    cx, cy = (i % cols) * w, (i // cols) * (h + 16)
    sheet.paste(im, (cx, cy + 16))
    d.text((cx + 4, cy + 2), os.path.basename(p)[:40], fill='white')
sheet.save(out)
print(out, sheet.size)
