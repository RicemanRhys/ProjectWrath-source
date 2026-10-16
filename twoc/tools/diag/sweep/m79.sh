# m79.sh TAG WHERE [ENV...] -- step 79 measurement with a TPROF build (../step79_thread_profile.patch): WHERE cb
# (cast_bug start, standing), cbg (cast_bug, Crash gliding by his position), av (avalanch), up (the upstairs room).
# 16:9, HUD at the edges, Frame rate: Display, RS (default native), adaptive on unless ADAPT=0; 25 s measured after the
# load (STEPS). The per-frame CSV goes to runs/TAG.tprof.csv; tpsum.py summarises it, shown.py the log's evenness.
S=$(dirname $(readlink -f $0))
cd $S
T=$1; W=$2; shift 2
TP=$(cygpath -w $S/runs/$T.tprof.csv)
EV="RECOMP_HUD=edges RECOMP_FRAMERATE_ADAPTIVE=${ADAPT:-1} RECOMP_D3D_BRIDGE_TPROF=$TP"
export RS=${RS:-native}
case $W in
  cb)  WAIT=${WAIT:-22} bash sx.sh $T u 1 "${STEPS:-wait:25}" $EV "$@" ;;
  cbg) WAIT=${WAIT:-22} bash sx.sh $T u 1 "x:glide.py:$T:25:-3:7:3" $EV "$@" ;;
  av)  WAIT=${WAIT:-22} bash sx.sh $T 5 4:1 "${STEPS:-wait:25}" $EV "$@" ;;
  up)  PRE="upst.py $T" WAIT=${WAIT:-3} bash sx.sh $T - - "${STEPS:-wait:25}" $EV "$@" ;;
esac
