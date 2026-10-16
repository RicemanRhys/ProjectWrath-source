# t78o.sh TAG EXEDIR [ENV...] -- step 78: the settings overlay upstairs with Frame rate: Display, held (pause on) then
# running behind it (RECOMP_OVERLAY_PAUSE=0, a live change through the trigger file while it is open). Grabs; the log.
cd $(dirname $(readlink -f $0))
T=$1; X=$(cygpath -w $2)/crash_twoc.exe; shift 2
for m in h r; do
  mkdir -p runs/${T}$m
  TR=$(cygpath -m $(pwd)/runs/${T}$m)/sub.trig
  if [ $m = h ]; then
    A="wait:6 x:esc.py wait:3 grab:open wait:2 grab:open2 x:esc.py wait:4 grab:after"
    P=1
  else
    A="wait:6 x:esc.py wait:3 grab:open x:st.py:${T}$m:aa_msaa4x;render_1920x1080 wait:3 grab:changed x:esc.py wait:4 grab:after"
    P=0
  fi
  RS=2560x1440 PRE="upst.py ${T}$m" WAIT=3 bash sx.sh ${T}$m - - "$A" --exe=$X RECOMP_HUD=edges RECOMP_OVERLAY_PAUSE=$P \
    RECOMP_D3D_BRIDGE_SUBDUMP=@$TR "$@" 2>&1 | tail -1
  grep -aE "\[OVL\]|trigger file|host picture|anti-aliasing:|frame slots|THREADCHECK|CRASH" runs/${T}$m.err.log | head -20
  grep -a "interpolation sub-frames" runs/${T}$m.err.log | tail -1
done
