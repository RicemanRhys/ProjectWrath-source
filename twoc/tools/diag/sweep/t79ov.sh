# t79ov.sh TAG EXEDIR [ENV...] -- step 79 item 3: the overlay's Render thread line, upstairs, Frame rate: Display,
# the overlay running (pause off, so the trigger file applies while it is open): Auto, then off, on, auto; grabs.
cd $(dirname $(readlink -f $0))
T=$1; X=$(cygpath -w $2)/crash_twoc.exe; shift 2
mkdir -p runs/$T
TR=$(cygpath -m $(pwd)/runs/$T)/sub.trig
A="wait:6 x:esc.py wait:3 grab:auto x:st.py:$T:rthread_off wait:3 grab:off x:st.py:$T:rthread_on wait:3 grab:on"
A="$A x:st.py:$T:rthread_auto wait:3 grab:auto2 x:esc.py wait:3 grab:after"
RS=2560x1440 PRE="upst.py $T" WAIT=3 bash sx.sh $T - - "$A" --exe=$X RECOMP_HUD=edges RECOMP_OVERLAY_PAUSE=0 \
  RECOMP_RENDER_THREAD=auto RECOMP_D3D_BRIDGE_SUBDUMP=@$TR "$@" 2>&1 | tail -1
grep -aE "\[OVL\]|render thread|trigger file|THREADCHECK|CRASH|watchdog" runs/$T.err.log | head -30
