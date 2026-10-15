"""cs.py OUT.png COLS W IN.png ... -- contact sheet: each image scaled to width W, labelled with its name."""
import sys, os
from PIL import Image, ImageDraw
out, cols, W = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
ims = []
for p in sys.argv[4:]:
    im = Image.open(p).convert("RGB")
    im = im.resize((W, int(im.height * W / im.width)))
    d = ImageDraw.Draw(im); d.rectangle((0, 0, 8 * len(os.path.basename(p)), 12), fill=(0, 0, 0))
    d.text((2, 0), os.path.basename(p), fill=(255, 255, 0)); ims.append(im)
H = max(i.height for i in ims)
rows = (len(ims) + cols - 1) // cols
sh = Image.new("RGB", (cols * (W + 4), rows * (H + 4)), (60, 60, 60))
for k, im in enumerate(ims):
    sh.paste(im, ((k % cols) * (W + 4), (k // cols) * (H + 4)))
sh.save(out); print(out, sh.size)
