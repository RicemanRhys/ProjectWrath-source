# m78.sh PFX: step 78's cost, interleaved: cast_bug's start and cast_bug gliding, 4 sub-frames (adaptive off), native
# 4K, 16:9, HUD at the edges, with the step-75 profile on the code before (PB) and after (PA): PB PA, then PA PB.
cd $(dirname $(readlink -f $0))
P=${1:-m}
X() { cygpath -w $(pwd)/$1/crash_twoc.exe; }
one() {   # TAG PLACE EXEDIR
  case $2 in
    cbg) STEPS="x:glide.py:$1:25:-3:7:3" bash m.sh $1 cb --exe=$(X $3) 2>&1 | tail -1 ;;
    *) bash m.sh $1 $2 --exe=$(X $3) 2>&1 | tail -1 ;;
  esac
}
for pl in cb cbg; do
  for r in 1 2; do
    if [ $r = 1 ]; then L="PB PA"; else L="PA PB"; fi
    for e in $L; do one ${P}${e}_${pl}$r $pl $e; done
  done
done
python tab.py ${P}PB_cb1 ${P}PB_cb2 ${P}PA_cb1 ${P}PA_cb2
python tab.py ${P}PB_cbg1 ${P}PB_cbg2 ${P}PA_cbg1 ${P}PA_cbg2
