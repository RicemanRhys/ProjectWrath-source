# g60chk.sh -- step 106: Game (60) drives with the rolling capture saved 3 times (dr2.sh) and scanned offline (flat.py):
# A twice with the automatic trigger on too (in-process vs offline), B four times.
cd $(dirname $(readlink -f $0))
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
for r in hA1:A:5 hB1:B:0 hA2:A:5 hB2:B:0 hB3:B:0 hB4:B:0; do
  T=${r%%:*}; E=$(echo $r | cut -d: -f2); AU=${r##*:}
  rm -rf runs/$T
  bash dr2.sh $T $E -6.5 0.3 515 "$(cat wp_515.txt)" RECOMP_D3D_BRIDGE_ROLLING_AUTO=$AU > /dev/null 2>&1
  echo "== $T ($E): in-process: $(grep -a '\[ROLL\] flagged' runs/$T.err.log | grep -o 'guest swap [0-9]* ([0-9 of]*)' | tr '\n' ';')"
  for R in runs/$T/roll/rolling_*; do "$PY" flat106.py $R/frames | grep -v ": 0 of"; done
done
