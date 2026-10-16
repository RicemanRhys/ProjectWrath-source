# t79a.sh EXEDIR -- step 79 first checks with the render thread on (THREADCHECK): the overlay held and running
# (t78o.sh), live changes incl. Frame rate off and on and the render thread off and on (t78s.sh + rthread).
cd $(dirname $(readlink -f $0))
E=$1
TC="RECOMP_RENDER_THREAD=on RECOMP_D3D_BRIDGE_THREADCHECK=1"
bash t78o.sh a_ov $(pwd)/$E $TC > runs/t79a.ov.txt 2>&1
T=a_ls; X=$(cygpath -w $(pwd)/$E)/crash_twoc.exe
mkdir -p runs/$T
TR=$(cygpath -m $(pwd)/runs/$T)/sub.trig
A="wait:8 grab:a x:st.py:$T:render_1920x1080 wait:3 grab:b x:st.py:$T:aspect_21/9 wait:3 grab:c"
A="$A x:st.py:$T:aa_msaa4x wait:3 grab:d x:st.py:$T:aa_smaa wait:3 grab:e x:st.py:$T:distance_2;aniso_16 wait:3 grab:f"
A="$A x:st.py:$T:framerate_60 wait:4 grab:g x:st.py:$T:framerate_display wait:4 grab:h"
A="$A x:st.py:$T:rthread_off wait:4 grab:i x:st.py:$T:rthread_on wait:4 grab:j"
A="$A x:st.py:$T:framerate_120 wait:4 grab:k x:st.py:$T:framerate_display wait:4 grab:l"
A="$A x:st.py:$T:aa_off;render_0;aspect_16/9;distance_1;aniso_1 wait:4 grab:m"
PRE="upst.py $T" WAIT=3 bash sx.sh $T - - "$A" --exe=$X RECOMP_HUD=edges RECOMP_D3D_BRIDGE_SUBDUMP=@$TR $TC 2>&1 | tail -2 > runs/t79a.ls.txt
grep -aE "trigger file|host picture|\[WIDE\]|anti-aliasing|\[DRAW\]|anisotropic|host frame rate|pacing now|frame rate plan|render thread|frame slots|THREADCHECK|CRASH|watchdog" runs/$T.err.log | grep -v "^\[D3DB\] host frame rate plan: none" | head -80 >> runs/t79a.ls.txt
grep -a "interpolation sub-frames" runs/$T.err.log | tail -2 >> runs/t79a.ls.txt
echo ALLDONE
