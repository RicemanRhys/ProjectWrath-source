# scenes.sh PFX EXEDIR [ENV...] -- step 92 item 6: aocmp.sh in the hub, Arctic Antics, cast_bug's start, H2 Oh No,
# Drain Damage (a boss), avalanch; MODES as aocmp.sh's. WHERE narrows ("hub aa cb h2 dd av").
S=$(dirname $(readlink -f $0))
cd $S
P=$1; E=$2; shift 2
for w in ${WHERE:-hub aa cb h2 dd av}; do
  case $w in
    hub) bash aocmp.sh ${P}hub - - $E "$@" ;;
    aa) bash aocmp.sh ${P}aa 7 1:1 $E "$@" ;;
    cb) bash aocmp.sh ${P}cb u 1 $E "$@" ;;
    h2) bash aocmp.sh ${P}h2 2 1:1 $E "$@" ;;
    dd) bash aocmp.sh ${P}dd 23 1:1 $E "$@" ;;
    av) bash aocmp.sh ${P}av 5 4:1 $E "$@" ;;
  esac > runs/chain_${P}${w}.txt 2>&1
  "C:/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe" aoset.py ${P}$w
done
echo scenes done
