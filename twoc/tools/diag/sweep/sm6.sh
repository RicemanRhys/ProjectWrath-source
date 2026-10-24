# sm6.sh TAG EXEDIR [ENV...] -- step 104 Part 6: Smokey and the Bandicoot (level 3, pad 1:1) at Game (60), 16:9,
# HUD at the edges, 1920x1080 windowed; the vehicle driven (A, stick forward, weaving), a burst of 240 window
# grabs 0.06 s apart (runs/TAG_b_*.png, half size).
S=$(dirname $(readlink -f $0)); cd $S
T=$1; X=$(cygpath -w $(readlink -f $2)/crash_twoc.exe); shift 2
ACT_KEEP_LIVES=1 PASS=B RS=${RS:-1920x1080} WAIT=${WAIT:-30} SECS=${SECS:-300} bash sx.sh $T 3 1:1 \
  "${ST:-wait:2 pad:a=255_ly=32767 wait:1 bg:weave.py:$T burst:b:240:0.06 pad:}" --exe=$X RECOMP_WIDESCREEN=1 RECOMP_HUD=edges "$@"
