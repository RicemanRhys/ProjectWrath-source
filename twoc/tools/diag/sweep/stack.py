import sys
from PIL import Image
out=sys.argv[1]; ims=[Image.open(p) for p in sys.argv[2:]]
W=max(i.width for i in ims); H=sum(i.height+2 for i in ims)
s=Image.new("RGB",(W,H),(40,40,40)); y=0
for i in ims: s.paste(i,(0,y)); y+=i.height+2
s.save(out); print(out,s.size)
