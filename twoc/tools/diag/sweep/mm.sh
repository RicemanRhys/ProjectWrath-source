# mm.sh PFX ROUNDS "CFG..." -- step 87: m84.sh runs interleaved by config, cast_bug start / avalanch / upstairs,
# standing, render thread on, native 4K at 240 Hz. CFG = EXEDIR[:ENV=VAL,ENV=VAL] (exe folder beside this one).
# Tags PFX<round><where>_<cfgname>. Then fsum.py PFX.
cd $(dirname $(readlink -f $0))
P=$1; R=$2; shift 2
for r in $(seq 1 $R); do for w in ${WHERE:-cb av up}; do for c in "$@"; do
  X=${c%%:*}; E=""; N=$X
  case $c in *:*) E=$(echo ${c#*:} | tr ',' ' '); N=$(echo $c | tr ':=,' '___');; esac
  T=$P$r${w}_$N
  bash m84.sh $T $w $X $E > /dev/null 2>&1
  echo "$T $(grep -a 'levels\\' runs/$T.err.log | tail -1 | sed 's/.*levels\\//')"
done; done; done
echo ALLDONE
