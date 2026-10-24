import sys, glob
from PIL import Image
out, pat = sys.argv[1], sys.argv[2]
cols = int(sys.argv[3]) if len(sys.argv) > 3 else 3
W = int(sys.argv[4]) if len(sys.argv) > 4 else 700
fs = sorted(glob.glob(pat))
ims = [Image.open(f) for f in fs]
ims = [i.resize((W, int(i.height * W / i.width))) for i in ims]
H = max(i.height for i in ims)
rows = (len(ims) + cols - 1) // cols
sh = Image.new("RGB", (W * cols, H * rows), "white")
for k, i in enumerate(ims):
    sh.paste(i, ((k % cols) * W, (k // cols) * H))
sh.save(out)
print(out, [f.split("_",1)[1] for f in fs])
