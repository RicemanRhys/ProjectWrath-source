import sys, glob, os
from PIL import Image, ImageDraw
out, d, step, off, cols, W = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])
a = int(sys.argv[7]) if len(sys.argv) > 7 else 0
b = int(sys.argv[8]) if len(sys.argv) > 8 else 10**9
fs = sorted(glob.glob(d if "*" in d else os.path.join(d, "*.bmp")))[a:b][off::step]
ims = []
for f in fs:
    i = Image.open(f).convert("RGB"); i = i.resize((W, int(i.height * W / i.width)))
    ImageDraw.Draw(i).text((2, 2), os.path.basename(f)[:13], fill=(255, 255, 0))
    ims.append(i)
H = ims[0].height
rows = (len(ims) + cols - 1) // cols
sh = Image.new("RGB", (W * cols, H * rows))
for k, i in enumerate(ims): sh.paste(i, ((k % cols) * W, (k // cols) * H))
sh.save(out); print(out, len(ims))
