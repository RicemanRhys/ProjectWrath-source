# t4.sh: step 77 at 240 Hz, the rest: avalanch at display (the adaptive ladder), the overlay at 90, then item 5's
# identity runs (../id.sh exe_head exe_item3).
cd $(dirname $(readlink -f $0))
X=$(cygpath -w $(pwd)/exe_item3); E="--exe=$X\crash_twoc.exe"
WAIT=30 bash sx.sh k3av 5 4:1 "wait:40" $E RECOMP_FRAMERATE=display 2>&1 | tail -1
PRE="upst.py k3ov" WAIT=3 bash sx.sh k3ov - - "wait:8 x:esc.py wait:2 grab:ovl wait:1" $E RECOMP_FRAMERATE=90 2>&1 | tail -1
bash ../id.sh exe_head exe_item3
echo ALLDONE
