# m.sh TAG WHERE [ENV...] -- step 75 measurement: WHERE cb (cast_bug, room 6 pad 1), av (avalanch, level 5 by pad 4:1)
# or up (the upstairs room); then STEPS (default: wait 25 s with FPROF on, then hs2.py 15 s if SAMP=1). 16:9, HUD at the
# edges, Frame rate: Display, RS (default native), adaptive off unless ADAPT=1. FPROF to runs/TAG.fprof.csv.
S=$(dirname $(readlink -f $0))
cd $S
T=$1; W=$2; shift 2
FP=$(cygpath -w $S/runs/$T.fprof.csv)
EV="RECOMP_HUD=edges RECOMP_FRAMERATE_ADAPTIVE=${ADAPT:-0} RECOMP_D3D_BRIDGE_FPROF=$FP"
ST=${STEPS:-"wait:${SECS1:-25}"}
[ "${SAMP:-0}" = 1 ] && ST="$ST x:hs2.py:exe/crash_twoc.map:15:runs/$T.samp.txt"
export RS=${RS:-native}
case $W in
  cb) WAIT=${WAIT:-22} bash sx.sh $T u 1 "$ST" $EV "$@" ;;
  av) WAIT=${WAIT:-22} bash sx.sh $T 5 4:1 "$ST" $EV "$@" ;;
  up) PRE="upst.py $T" WAIT=${WAIT:-3} bash sx.sh $T - - "$ST" $EV "$@" ;;
esac
