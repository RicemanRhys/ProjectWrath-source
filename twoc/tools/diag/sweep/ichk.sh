# ichk.sh PFX EXEDIR [ENV...]: TMP_ICHECK runs (each sub-frame old way then new way, compared), Frame rate: Display,
# 4 sub-frames (adaptive off), HUD at the edges: cast_bug (gliding), avalanch, upstairs
cd $(dirname $(readlink -f $0))
P=$1; X=$(cygpath -w $2); shift 2
E="--exe=$X\crash_twoc.exe TMP_ICHECK=1 RECOMP_FRAMERATE_ADAPTIVE=0 RECOMP_HUD=edges $*"
WAIT=30 bash sx.sh ${P}_cb u 1 "wait:5 x:glide.py:${P}_cb:40:-3:7:3" $E 2>&1 | tail -1
WAIT=30 bash sx.sh ${P}_av 5 4:1 "wait:40" $E 2>&1 | tail -1
PRE="upst.py ${P}_up" WAIT=3 bash sx.sh ${P}_up - - "wait:40" $E 2>&1 | tail -1
for t in cb av up; do echo "== ${P}_$t"; grep -a "replay check" runs/${P}_$t.err.log | tail -2; grep -a "interpolat" runs/${P}_$t.err.log | tail -2; done
