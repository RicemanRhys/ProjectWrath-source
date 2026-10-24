# wall106.sh TAG EXEDIR [ENV...] -- step 106: step 105's canyon-wall spot reached by driving: ap.py from (-6.5, 0.3, 515)
# along the racers' road to (-7.4, 708) with AP_STOP=1 (coasts to a stop near the finish gate, facing it), then
# FR=display: 40 swaps of full-size sub-frame dumps; else (Game (60)) 80 window grabs 0.1 s apart. Then darkmap.py's
# count left of the cactus (as bis105.sh / g60_105.sh) on 480x270 copies in ../wall_TAG.
S=$(dirname $(readlink -f $0)); cd $S
T=$1; E=$2; shift 2
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
if [ "${FR:-60}" = display ]; then GRAB="sub:40 wait:12"; else GRAB="burst:g:80:0.1"; fi
AP_STOP=1 SECS=200 bash sm105.sh $T $E "wait:1 pad:a=255_ly=32767 wait:2.5 x:drv.py:once:-6.5:0.3:515:0 bg:ap.py:$T:26:$(cat wp_wall.txt) wait:27 pad: wait:2 $GRAB" "$@" > runs/log_$T.txt 2>&1
rm -rf ../wall_$T; mkdir -p ../wall_$T
if [ "${FR:-60}" = display ]; then "$PY" down105.py runs/$T ../wall_$T; rm -f runs/$T/host*.bmp; else
"$PY" -c "
import glob, os
from PIL import Image
for f in sorted(glob.glob('runs/${T}_g_*.png')):
    im = Image.open(f).convert('RGB'); w, h = im.size
    im.crop((0, h - int(w * 9 / 16), w, h)).resize((480, 270), Image.BOX).save(os.path.join('../wall_$T', os.path.basename(f)))
"; fi
echo "$T $*: $(REGION=0,60,240,130 "$PY" darkmap.py ../wall_$T 3 10 4 | tail -1) | stopped at $(grep '^ap' runs/$T.act.txt | tail -1 | cut -d' ' -f3-5)"
