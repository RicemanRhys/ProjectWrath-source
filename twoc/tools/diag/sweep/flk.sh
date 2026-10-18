# flk.sh TAG EXEDIR MODE [ENV...] -- step 92 item 6: sub-frame flicker. cast_bug's start, the player gliding along z
# (glideb.py, the camera following), Frame rate: Display (4 sub-frames at 240 Hz), ambient occlusion MODE from the
# start; after 6 s of gliding the 4 sub-frames of 3 swaps dumped at full size (runs/TAG/hostsub*). RS 1920x1080.
S=$(dirname $(readlink -f $0))
cd $S
T=$1; X=$(cygpath -w $(readlink -f $2)/crash_twoc.exe); M=$3; shift 3
mkdir -p runs/$T
TR=$(cygpath -m $(pwd)/runs/$T/sub.trig)
PASS=1 RS=${RS:-1920x1080} WAIT=22 bash sx.sh $T u 1 "x:glideb.py:$T:20:-3:7:3 wait:6 sub:3 wait:3" --exe=$X \
  RECOMP_AO=$M RECOMP_FRAMERATE_ADAPTIVE=0 RECOMP_D3D_BRIDGE_SUBDUMP=@$TR RECOMP_FB_DUMP=$(cygpath -m $(pwd)/runs/$T)/shot "$@"
ls runs/$T | grep -c hostsub
