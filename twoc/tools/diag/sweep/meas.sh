# meas.sh: the step-76 table, interleaved: per place, exes B (before), A1 (B1), A (B1+B2), then A, A1, B.
cd $(dirname $(readlink -f $0))
X() { cygpath -w $(readlink -f ../$1)/crash_twoc.exe; }
one() {   # TAG PLACE EXE
  case $2 in
    cbg) STEPS="x:glide.py:$1:25:-3:7:3" bash m.sh $1 cb --exe=$(X $3) 2>&1 | tail -1 ;;
    *) bash m.sh $1 $2 --exe=$(X $3) 2>&1 | tail -1 ;;
  esac
}
for pl in cb cbg av up; do
  for r in 1 2; do
    if [ $r = 1 ]; then L="B A1 A"; else L="A A1 B"; fi
    for e in $L; do one m${e}_${pl}$r $pl $e; done
  done
done
