# m84.sh TAG WHERE EXEDIR [ENV...] -- step 84: the game thread's work a frame with the render thread on, standing.
# WHERE cb (cast_bug start), av (avalanch start), up (the upstairs room). EXEDIR beside this folder (P, B, ...),
# a ../step84_game_profile.patch build. 16:9, HUD at the edges, Frame rate: Display, RS (default native), adaptive on.
# SAMP=1: hs84.py samples the game thread 15 s (EXEDIR/crash_twoc.map needed) in the middle of the 25 s.
# Writes runs/TAG.gprof.csv (game thread, a line a frame) and runs/TAG.tprof.csv (both threads).
S=$(dirname $(readlink -f $0))
cd $S
T=$1; W=$2; X=$3; shift 3
GP=$(cygpath -w $S/runs/$T.gprof.csv)
TP=$(cygpath -w $S/runs/$T.tprof.csv)
EXE=$(cygpath -w $S/$X/crash_twoc.exe)
EV="--exe=$EXE RECOMP_HUD=edges RECOMP_RENDER_THREAD=${RT:-on} RECOMP_FRAMERATE_ADAPTIVE=${ADAPT:-1} RECOMP_D3D_BRIDGE_GPROF=$GP RECOMP_D3D_BRIDGE_TPROF=$TP"
if [ "${SAMP:-0}" = 1 ]; then ST="wait:5 x:hs84.py:$X/crash_twoc.map:15:runs/$T.hs.txt wait:3"; else ST="wait:25"; fi
export RS=${RS:-native}
case $W in
  cb)  WAIT=${WAIT:-22} bash sx.sh $T u 1 "${STEPS:-$ST}" $EV "$@" ;;
  av)  WAIT=${WAIT:-22} bash sx.sh $T 5 4:1 "${STEPS:-$ST}" $EV "$@" ;;
  up)  PRE="upst.py $T" WAIT=${WAIT:-3} bash sx.sh $T - - "${STEPS:-$ST}" $EV "$@" ;;
esac
