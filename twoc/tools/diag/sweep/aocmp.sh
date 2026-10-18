# aocmp.sh TAG LEVEL ROOM:PAD EXEDIR [ENV...] -- step 92: one run standing in a level (16:9, Frame rate: Display,
# RS default 1920x1080), ambient occlusion switched live through the trigger file, a window grab and a full-size
# sub-frame dump (runs/TAG/) after each step. LEVEL - : the hub; u: upstairs (gopad6).
# MODES: "NAME=LINES ..." (st.py lines: ao_gtao;aoradius_0.5); default "off=ao_off gtao=ao_gtao ssao=ao_ssao".
S=$(dirname $(readlink -f $0))
cd $S
T=$1; L=$2; RP=$3; X=$(cygpath -w $4/crash_twoc.exe); shift 4
mkdir -p runs/$T
TR=$(cygpath -m $(pwd)/runs/$T/sub.trig)
ST=""
for m in ${MODES:-off=ao_off gtao=ao_gtao ssao=ao_ssao}; do
  ST="$ST x:st.py:$T:${m#*=} wait:1.2 grab:${m%%=*} sub:1 wait:1.2"
done
PASS=1 RS=${RS:-1920x1080} bash sx.sh $T $L $RP "wait:${PRE_WAIT:-2} $ST" --exe=$X RECOMP_D3D_BRIDGE_SUBDUMP=@$TR \
  RECOMP_FB_DUMP=$(cygpath -m $(pwd)/runs/$T)/shot "$@"
grep -a "^\[AO\].*compiled\|ambient occlusion:\|CRASH" runs/$T.err.log | head -8
