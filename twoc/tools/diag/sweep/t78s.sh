# t78s.sh TAG EXEDIR [ENV...] -- step 78 item 4: live settings through the trigger file (st.py), upstairs, Frame
# rate: Display, 16:9, HUD at the edges: render size, aspect, MSAA, SMAA, draw distance + anisotropy, frame rate off
# and on, back; a grab after each. Then the log's lines for each change.
cd $(dirname $(readlink -f $0))
T=$1; X=$(cygpath -w $2)/crash_twoc.exe; shift 2
mkdir -p runs/$T
TR=$(cygpath -m $(pwd)/runs/$T)/sub.trig
A="wait:8 grab:a x:st.py:$T:render_1920x1080 wait:3 grab:b x:st.py:$T:aspect_21/9 wait:3 grab:c"
A="$A x:st.py:$T:aa_msaa4x wait:3 grab:d x:st.py:$T:aa_smaa wait:3 grab:e x:st.py:$T:distance_2;aniso_16 wait:3 grab:f"
A="$A x:st.py:$T:framerate_60 wait:4 grab:g x:st.py:$T:framerate_display wait:4 grab:h"
A="$A x:st.py:$T:aa_off;render_0;aspect_16/9;distance_1;aniso_1 wait:4 grab:i"
PRE="upst.py $T" WAIT=3 bash sx.sh $T - - "$A" --exe=$X RECOMP_HUD=edges RECOMP_D3D_BRIDGE_SUBDUMP=@$TR "$@" 2>&1 | tail -2
grep -aE "trigger file|host picture|\[WIDE\]|anti-aliasing|\[DRAW\]|anisotropic|host frame rate|pacing now|frame rate plan|frame slots|THREADCHECK|CRASH" runs/$T.err.log | grep -v "^\[D3DB\] host frame rate plan: none" | head -60
grep -a "interpolation sub-frames" runs/$T.err.log | tail -2
