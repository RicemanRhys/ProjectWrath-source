"""edgefilm.py OUT.png FRAME.bmp ... [--ratio=W:H] -- step 64 (notes 18.67): each frame's two strips outside the middle 4:3 (left | right), one row a
frame, a quarter size; a window grab's title bar is cut (the client is the bottom rows at the ratio, 21:9 by default).
Use on grabs a fraction of a second apart while the camera turns: geometry culled late shows up away from the
frame's edge from one row to the next."""
import sys
from PIL import Image
ratio = 21 / 9
files = []
for a in sys.argv[2:]:
    if a.startswith("--ratio="):
        w_, h_ = a[8:].split(":")
        ratio = float(w_) / float(h_)
    else:
        files.append(a)
rows = []
for f in files:
    im = Image.open(f).convert("RGB")
    w, H = im.size
    h = int(round(w / ratio))
    im = im.crop((0, H - h, w, H))
    s = int((w - h * 4 / 3) / 2)
    l, r = im.crop((0, 0, s, h)), im.crop((w - s, 0, w, h))
    row = Image.new("RGB", (2 * s + 12, h), (255, 0, 0))
    row.paste(l, (0, 0)); row.paste(r, (s + 12, 0))
    rows.append(row.resize((row.width // 4, row.height // 4)))
out = Image.new("RGB", (rows[0].width * 4 + 12, ((len(rows) + 3) // 4) * (rows[0].height + 3)), (40, 40, 40))
for k, r in enumerate(rows):
    out.paste(r, ((k % 4) * (r.width + 4), (k // 4) * (r.height + 3)))
out.save(sys.argv[1]); print(sys.argv[1], out.size)
