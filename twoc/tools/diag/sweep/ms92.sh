# ms92.sh TAG cb|av|up EXEDIR [ENV...] -- step 92 item 5: standing in cast_bug's start, avalanch's or the upstairs
# room with the render thread on, Frame rate: Display (240 Hz here, interpolated), adaptive on, 16:9, HUD at the
# edges, RS (default native), GPU timestamps on; ambient occlusion switched live: off, gtao, ssao, off, SEG s each
# (default 32). ms92sum.py TAG summarises by mode.
S=$(dirname $(readlink -f $0))
cd $S
T=$1; W=$2; X=$(cygpath -w $(readlink -f $3)/crash_twoc.exe); shift 3
mkdir -p runs/$T
TR=$(cygpath -m $(pwd)/runs/$T/sub.trig)
G=${SEG:-32}
ST="${STEPS:-x:st.py:$T:ao_off wait:$G x:st.py:$T:ao_gtao wait:$G x:st.py:$T:ao_ssao wait:$G x:st.py:$T:ao_off wait:$G}"
EV="--exe=$X RECOMP_HUD=edges RECOMP_RENDER_THREAD=${RT:-on} RECOMP_FRAMERATE_ADAPTIVE=${ADAPT:-1} RECOMP_AO_GPU=1 RECOMP_D3D_BRIDGE_SUBDUMP=@$TR"
export RS=${RS:-native}
case $W in
  cb)  WAIT=${WAIT:-22} bash sx.sh $T u 1 "$ST" $EV "$@" ;;
  av)  WAIT=${WAIT:-22} bash sx.sh $T 5 4:1 "$ST" $EV "$@" ;;
  up)  PRE="upst.py $T" WAIT=${WAIT:-3} bash sx.sh $T - - "$ST" $EV "$@" ;;
esac
