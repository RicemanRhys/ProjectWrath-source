"""crop.py OUT DIR A B X0 Y0 X1 Y1 SCALE COLS: frames A..B-1 cropped and enlarged, labelled."""
import sys, glob, os
from PIL import Image, ImageDraw
out, d = sys.argv[1], sys.argv[2]
a, b, x0, y0, x1, y1, sc, cols = map(int, sys.argv[3:11])
fs = sorted(glob.glob(d if "*" in d else os.path.join(d, "*.bmp")))[a:b]
W, H = (x1 - x0) * sc, (y1 - y0) * sc
sh = Image.new("RGB", (W * cols, H * ((len(fs) + cols - 1) // cols)))
for k, f in enumerate(fs):
    i = Image.open(f).convert("RGB").crop((x0, y0, x1, y1)).resize((W, H), Image.NEAREST)
    ImageDraw.Draw(i).text((2, 2), os.path.basename(f)[:20], fill=(255, 255, 0))
    sh.paste(i, ((k % cols) * W, (k // cols) * H))
sh.save(out); print(out)
