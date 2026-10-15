# batch.sh PREFIX LISTFILE -- sw.sh for each "LEVEL ROOM:PAD" line; PASS from the environment.
cd $(dirname $(readlink -f $0))
P=$1
while read L RP; do
  [ -z "$L" ] && continue
  bash sw.sh ${P}$(printf %02d $L) $L $RP < /dev/null > runs/${P}$(printf %02d $L).sw.txt 2>&1
  tail -6 runs/${P}$(printf %02d $L).sw.txt >> runs/${P}batch.txt
done < $2
echo BATCH DONE >> runs/${P}batch.txt
