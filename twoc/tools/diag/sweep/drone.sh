# drone.sh -- step 102 part 3: Droid Void (20) and Arctic Antics (7) with the APU's output written to runs/TAG.wav,
# the option off and on; with it on, a pause, a death and a re-entry through the warp room. EXE dir as $1.
cd $(dirname $(readlink -f $0))
X=$(cygpath -w $(readlink -f $1)/crash_twoc.exe)
W() { cygpath -w $(pwd)/runs/$1.wav; }
P="pad:buttons=0x10 wait:0.15 pad:"
PZ="$P wait:6 $P wait:8"
WARP="$P wait:4 pad:buttons=0x2 wait:0.15 pad: wait:0.5 pad:buttons=0x2 wait:0.15 pad: wait:0.5 pad:a=255 wait:0.15 pad: wait:1 pad:buttons=0x1 wait:0.15 pad: wait:0.5 pad:a=255 wait:0.15 pad: wait:25"
r() { T=$1; L=$2; shift 2; LV=$L RP=1:1 WAIT=8 SECS=600 bash fx.sh $T "$STEPS" --exe=$X RECOMP_APU_OUT_WAV=$(W $T) "$@" > runs/log_$T.txt 2>&1; }
STEPS="wait:45" r dv_off 20
STEPS="wait:45" r dv_on 20 RECOMP_MUTE_DROID_DRONE=1
STEPS="wait:25 $PZ wait:10 x:setpos.py:0:-300:0:1 wait:20 $WARP x:gopad2.py:dv_on2:20 wait:40" r dv_on2 20 RECOMP_MUTE_DROID_DRONE=1
STEPS="wait:20 x:st.py:dv_live:drone_1 wait:5 $WARP x:gopad2.py:dv_live:20 wait:40" r dv_live 20
STEPS="wait:40" r aa_off 7
STEPS="wait:40" r aa_on 7 RECOMP_MUTE_DROID_DRONE=1
echo drone done
