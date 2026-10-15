"""topng.py IN.bmp [OUT.png] -- half-size PNG for viewing."""
import sys
from PIL import Image
src = sys.argv[1]
dst = sys.argv[2] if len(sys.argv) > 2 else src.rsplit(".", 1)[0] + ".png"
im = Image.open(src)
im = im.resize((im.width // 2, im.height // 2))
im.save(dst)
print(dst)
