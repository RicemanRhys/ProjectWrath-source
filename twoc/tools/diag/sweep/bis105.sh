# bis.sh TAG EXEDIR [ENV...] -- step 105: Smokey, the vehicle pinned at the user's spot (-7.1 0.06 714, capture
# 11-43-43/48), Match display, 40 swaps of full-size sub-frame dumps, then the dark-sliver count in the region
# left of the cactus (darkmap.py on 480x270 copies). Prints "TAG: N of 160".
S=$(dirname $(readlink -f $0)); cd $S
T=$1; E=$2; shift 2
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
FR=display bash sm.sh $T $E "wait:1 pad:a=255_ly=32767 wait:2.5 bg:ballmove.py:-7.1:0.06:714:25 wait:1.5 pad: wait:8 sub:40 wait:${SUBWAIT:-12}" "$@" > runs/log_$T.txt 2>&1
rm -rf ../bis_$T; mkdir -p ../bis_$T
"$PY" down105.py runs/$T ../bis_$T
echo "$T $*: $(REGION=0,60,240,130 "$PY" darkmap.py ../bis_$T 3 10 4 | tail -1)"
rm -f runs/$T/host*.bmp
