"""overview.py OUT.png TAG:LABEL ... -- one row per run: start, p01, p03, p05, pause grabs (whatever exists), with the
label on the left. Thumbnails 240 px wide."""
import glob, os, sys
from PIL import Image, ImageDraw
HERE = os.path.dirname(os.path.abspath(__file__))
R = os.path.join(HERE, "runs")
W, LW = 240, 150
rows = []
for arg in sys.argv[2:]:
    tag, label = arg.split(":", 1)
    label = label.replace("_", " ")
    ims = []
    for k in ["start", "p01", "p03", "p05", "pause"]:
        g = sorted(glob.glob(os.path.join(R, "%s_%s_*.png" % (tag, k))))
        if g:
            im = Image.open(g[0]).convert("RGB")
            im = im.crop((0, 34, im.width, im.height)) if im.height > 300 else im   # drop the title bar
            ims.append(im.resize((W, int(im.height * W / im.width))))
    rows.append((label, ims))
H = max((max(i.height for i in ims) for _, ims in rows if ims), default=180)
sheet = Image.new("RGB", (LW + 5 * (W + 4), len(rows) * (H + 4)), (30, 30, 30))
d = ImageDraw.Draw(sheet)
for r, (label, ims) in enumerate(rows):
    y = r * (H + 4)
    for j, line in enumerate(label.split("|")):
        d.text((4, y + 4 + 14 * j), line, fill=(255, 255, 0))
    for c, im in enumerate(ims):
        sheet.paste(im, (LW + c * (W + 4), y))
sheet.save(sys.argv[1])
print(sys.argv[1], sheet.size)
