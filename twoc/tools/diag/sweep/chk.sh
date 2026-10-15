# chk.sh PFX EXEDIR [ENV...]: REPLAY_CHECK runs (old path at once, new replayed) in cast_bug (gliding), avalanch, upstairs
cd $(dirname $(readlink -f $0))
P=$1; X=$(cygpath -w $2); shift 2
E="--exe=$X\crash_twoc.exe RECOMP_D3D_BRIDGE_REPLAY_CHECK=1 RECOMP_HUD=edges $*"
WAIT=22 bash sx.sh ${P}_cb u 1 "wait:5 x:glide.py:${P}_cb:30:-3:7:3" $E 2>&1 | tail -1
WAIT=22 bash sx.sh ${P}_av 5 4:1 "wait:30" $E 2>&1 | tail -1
PRE="upst.py ${P}_up" WAIT=3 bash sx.sh ${P}_up - - "wait:30" $E 2>&1 | tail -1
for t in cb av up; do echo "== ${P}_$t"; grep -a "replay check" runs/${P}_$t.err.log | tail -3; grep -ac "differs" runs/${P}_$t.err.log; done
