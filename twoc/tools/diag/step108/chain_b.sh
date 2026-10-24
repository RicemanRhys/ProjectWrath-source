# chain_b.sh EXEDIR PFX -- every scenario twice at Game (60) and Match display
cd $(dirname $(readlink -f $0))
E=$1; P=$2
for r in a b; do
  for sc in title demo hub; do
    for fr in 60 display; do
      bash aud108.sh ${P}_${sc}_${fr}_$r $E $sc RECOMP_FRAMERATE=$fr > runs/chain_${P}_${sc}_${fr}_$r.txt 2>&1
    done
  done
done
echo CHAIN $P DONE
