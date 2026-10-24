# cmp.sh PFX N "EXEDIRS" -- step 106: N rounds of dr3.sh drives per exe folder, interleaved (FR as dr3.sh; FULL=0,
# AUTO=5), then per run the [ROLL] flagged lines after the level load (guest swap > 3000) and where the drive ended.
S=$(dirname $(readlink -f $0)); cd $S
for i in $(seq $2); do for E in $3; do
  FULL=${FULL:-0} bash dr3.sh $1$E$i $E > /dev/null 2>&1
  n=$(grep -a "\[ROLL\] flagged" runs/$1$E$i.err.log | awk '{for(i=1;i<=NF;i++) if($i=="swap") print $(i+1)}' | awk '$1>3000' | wc -l)
  echo "$1$E$i $E flagged $n last $(grep '^ap' runs/$1$E$i.act.txt | tail -1 | cut -d' ' -f3-5) errs $(grep -a -c -i 'compile\|CRASH' runs/$1$E$i.err.log)"
done; done
