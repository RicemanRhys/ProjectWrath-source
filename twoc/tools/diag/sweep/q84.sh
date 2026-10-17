# q84.sh PFX WHERE "EXE[:ENV]..." [ROUNDS] -- quick interleaved timing: for each round, each EXE (a folder here, with
# an optional :ENV=VAL list after it, ',' between several) runs m84.sh WHERE; then tab84.py's head lines.
cd $(dirname $(readlink -f $0))
P=$1; W=$2; L=$3; R=${4:-1}
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
for r in $(seq 1 $R); do
  for x in $L; do
    X=${x%%:*}; E=""; [ "$x" != "$X" ] && E=$(echo ${x#*:} | tr ',' ' ')
    T=${P}${r}${W}_$(echo $x | tr ':=,' '___')
    bash m84.sh $T $W $X $E > /dev/null 2>&1
    echo "$T $("$PY" tab84.py $T 1200 | sed -n 2p)"
  done
done
