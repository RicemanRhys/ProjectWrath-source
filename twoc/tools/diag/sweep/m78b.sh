# m78b.sh PFX "EXES": like m78.sh, with any exe folders, interleaved forwards then backwards, cast_bug start and gliding.
cd $(dirname $(readlink -f $0))
P=$1; E=$2
X() { cygpath -w $(pwd)/$1/crash_twoc.exe; }
R=$(echo $E | tr ' ' '\n' | tac | tr '\n' ' ')
for pl in cb cbg; do
  for r in 1 2; do
    if [ $r = 1 ]; then L=$E; else L=$R; fi
    for e in $L; do
      t=${P}${e}_${pl}$r
      case $pl in
        cbg) STEPS="x:glide.py:$t:25:-3:7:3" bash m.sh $t cb --exe=$(X $e) 2>&1 | tail -1 ;;
        *) bash m.sh $t $pl --exe=$(X $e) 2>&1 | tail -1 ;;
      esac
    done
  done
done
for pl in cb cbg; do
  T=""; for e in $E; do T="$T ${P}${e}_${pl}1 ${P}${e}_${pl}2"; done
  python tab.py $T
done
