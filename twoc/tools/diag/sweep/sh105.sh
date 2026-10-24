# sh105.sh -- step 105: F (step 104) and X (the haze band's depth bias) side by side: the hub, Arctic Antics (7),
# Drain Damage (23, a boss), Smokey (3), Gold Rush (19), Medieval Madness (16); PASS=1 (Match display, 16:9),
# 1920x1080, HUD at the edges; a standing grab, then play.py 20 s with a grab every 5 s. Grabs runs/sh_<L>_<E>_*.png.
cd $(dirname $(readlink -f $0))
for L in hub 7 23 3 19 16; do
  for E in ${EXES:-F X}; do
    T=sh_${L}_$E
    X=$(cygpath -w $(pwd)/$E/crash_twoc.exe)
    if [ $L = hub ]; then LV=-; RP=-; else LV=$L; RP=1:1; fi
    PASS=1 RS=1920x1080 WAIT=25 bash sx.sh $T $LV $RP "wait:2 grab:stand x:play.py:$T:20:5" --exe=$X RECOMP_HUD=edges > /dev/null 2>&1
    echo "$T: $(ls runs | grep -c "^${T}_")"
  done
done
echo sh105 done
