# freq60.sh PFX N EXEDIR -- step 106: N Game (60) drives z 515 -> finish (dr2.sh), then wedge.py over every capture
S=$(dirname $(readlink -f $0)); cd $S
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
for i in $(seq $2); do
  rm -rf runs/$1$i
  bash dr2.sh $1$i $3 -6.5 0.3 515 "$(cat wp_515.txt)" "${@:4}"
  for R in runs/$1$i/roll/rolling_*; do echo "== $R"; FROM=${FROMSKIP:-0} "$PY" wedge106.py $R/frames | grep -v "^0000\|^0001\|^0002" ; done
done
