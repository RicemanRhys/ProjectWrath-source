# png.py IN.bmp [OUT.png] [MAXW]: a grab as a smaller PNG to look at.
import sys
from PIL import Image
src = sys.argv[1]
out = sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].isdigit() else src.rsplit('.', 1)[0] + '.png'
maxw = int(sys.argv[-1]) if sys.argv[-1].isdigit() else 1400
im = Image.open(src).convert('RGB')
if im.width > maxw:
    im = im.resize((maxw, im.height * maxw // im.width), Image.LANCZOS)
im.save(out)
print(out, im.size)
