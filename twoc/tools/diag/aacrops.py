"""Step 58 (notes 18.61): the same box cropped from several frames (host dumps or wgcgrab grabs), zoomed with nearest
neighbour and laid side by side with labels; and how far each frame is from the first, overall and away from its
edges (the first frame's edges, found and widened by 2 pixels), to check that anti-aliasing changes only edges.

  python aacrops.py OUT.png X Y W H ZOOM LABEL=IMAGE [LABEL=IMAGE ...]

Two dumps of the same guest swap in two runs are the same picture in the menu, and not in the attract demo (its phase
follows the load's timing): there, compare runs whose frames match away from the edges.
"""
import sys
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageStat

out, x, y, w, h, z = sys.argv[1], *map(int, sys.argv[2:7])
ims = [(label, Image.open(path).convert("RGB")) for label, path in (a.split("=", 1) for a in sys.argv[7:])]
base = ims[0][1]
edges = base.convert("L").filter(ImageFilter.FIND_EDGES).point(lambda v: 255 if v > 24 else 0)
flat = edges.filter(ImageFilter.MaxFilter(5)).point(lambda v: 255 - v)
for label, im in ims[1:]:
    d = ImageChops.difference(base, im).convert("L")
    over = ImageStat.Stat(d.point(lambda v: 1 if v > 8 else 0)).sum[0]
    print("%s: mean difference %.3f, away from edges %.3f, pixels more than 8 apart %d"
          % (label, ImageStat.Stat(d).mean[0], ImageStat.Stat(d, mask=flat).mean[0], over))
sheet = Image.new("RGB", (len(ims) * (w * z + 8), h * z + 24), (40, 40, 40))
draw = ImageDraw.Draw(sheet)
for i, (label, im) in enumerate(ims):
    sheet.paste(im.crop((x, y, x + w, y + h)).resize((w * z, h * z), Image.NEAREST), (i * (w * z + 8), 24))
    draw.text((i * (w * z + 8) + 4, 4), label, fill=(255, 255, 255))
sheet.save(out)
print(out)
