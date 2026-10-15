# t3.sh EXEDIR PFX: step 77 item 3 at 240 Hz, the adaptive ladder: cast_bug gliding at display (240) and at 120,
# upstairs standing at display.
cd $(dirname $(readlink -f $0))
X=$(cygpath -w $(readlink -f $1)); P=$2
E="--exe=$X\crash_twoc.exe"
bash sx.sh ${P}cb240 u 1 "x:glide.py:${P}cb240:40:-3:7:3" $E RECOMP_FRAMERATE=display 2>&1 | tail -1
bash sx.sh ${P}cb120 u 1 "x:glide.py:${P}cb120:40:-3:7:3" $E RECOMP_FRAMERATE=120 2>&1 | tail -1
PRE="upst.py ${P}up240" WAIT=3 bash sx.sh ${P}up240 - - "wait:35" $E RECOMP_FRAMERATE=display 2>&1 | tail -1
echo ALLDONE
