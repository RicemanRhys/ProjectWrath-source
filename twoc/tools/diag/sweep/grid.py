"""grid.py OUT.png COLS FILE... -- step 99: thumbnails 320 px wide in a grid, the file's name under each."""
import sys
from PIL import Image, ImageDraw
out, cols, fs = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
W = 320
ims = []
for f in fs:
    im = Image.open(f).convert("RGB")
    ims.append((f.replace("\\", "/").split("/")[-1][:-4], im.resize((W, int(im.height * W / im.width)))))
H = max(i.height for _, i in ims) + 14
rows = (len(ims) + cols - 1) // cols
o = Image.new("RGB", (cols * W, rows * H), (24, 24, 24))
d = ImageDraw.Draw(o)
for n, (name, im) in enumerate(ims):
    x, y = (n % cols) * W, (n // cols) * H
    o.paste(im, (x, y))
    d.text((x + 3, y + im.height + 1), name, fill=(255, 255, 0))
o.save(out)
print(out, o.size)
