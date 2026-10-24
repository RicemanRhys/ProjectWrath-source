# g60.sh TAG EXEDIR [ENV...] -- step 105: Smokey, the vehicle pinned at the user's spot, Game (60), 80 window grabs
# 0.1 s apart (half size, 960x540), then the sliver count left of the cactus (darkmap.py on 480x270 copies).
S=$(dirname $(readlink -f $0)); cd $S
T=$1; E=$2; shift 2
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
bash sm.sh $T $E "wait:1 pad:a=255_ly=32767 wait:2.5 bg:ballmove.py:-7.1:0.06:714:60 wait:1.5 pad: wait:8 burst:g:80:0.1" "$@" > runs/log_$T.txt 2>&1
rm -rf ../g60_$T; mkdir -p ../g60_$T
"$PY" -c "
import glob, os, sys
from PIL import Image
for f in sorted(glob.glob('runs/${T}_g_*.png')):
    im = Image.open(f).convert('RGB')
    w, h = im.size
    im.crop((0, h - int(w * 9 / 16), w, h)).resize((480, 270), Image.BOX).save(os.path.join('../g60_$T', os.path.basename(f)))
"
echo "$T $*: $(REGION=0,60,240,130 "$PY" darkmap.py ../g60_$T 3 10 4 | tail -1)"
