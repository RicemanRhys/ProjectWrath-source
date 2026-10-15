# batchx.sh PREFIX LISTFILE -- sx.sh per line "TAG|LEVEL|ROOM:PAD|ACT|PRE|WAIT|SECS"; PASS from the environment.
cd $(dirname $(readlink -f $0))
P=$1
while IFS='|' read T L RP A PR W SC; do
  [ -z "$T" ] && continue
  case "$T" in \#*) continue;; esac
  A=${A//TAG/${P}$T}
  PRE="$PR" WAIT=${W:-20} SECS=${SC:-600} bash sx.sh ${P}$T $L $RP "$A" < /dev/null > runs/sx_${P}$T.txt 2>&1
done < $2
echo BATCH DONE > runs/sx_${P}done.txt
