# fv.sh EXEDIR -- step 102 part 4: "Character fur" live, three ways, at Match display (240) with the render thread on,
# windowed 16:9: the hub, Crashteroids (26, Coco), earth_b (21, Crunch), garden (12, Coco); grabs + a draw log each way.
cd $(dirname $(readlink -f $0))
X=$(cygpath -w $(readlink -f $1)/crash_twoc.exe)
v() { T=$1; L=$2; echo "wait:2 grab:on wait:${HOLD:-2} x:st.py:$T:fur_crash wait:2 grab:crash wait:${HOLD:-2} x:st.py:$T:fur_0 wait:2 grab:off wait:${HOLD:-2} x:st.py:$T:fur_1 wait:2 grab:on2 wait:${HOLD:-2}"; }
for p in ${FVP:-fvh,- fv26,26 fv21,21 fv12,12}; do set -- ${p/,/ }
  PASS=1 LV=$2 RP=1:1 WAIT=20 SECS=300 bash fx.sh $1 "$(v $1 $2)" --exe=$X RECOMP_RENDER_THREAD=on > runs/log_$1.txt 2>&1
done
echo fv done
