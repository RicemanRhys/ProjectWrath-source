# save83.sh TAG "SLOTKEYS" [ENV=VAL...] -- step 83: launch (game left running), new game WOMBAT, the hub's
# Load/Save console, SAVE GAME, SLOTKEYS in the slot list (e.g. "a" = the first slot), then YES and grabs.
# OVERWRITE=1 adds a YES for the overwrite question. The game keeps running: kill it yourself (k83.sh).
S=$(dirname $(readlink -f $0))
cd $S
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
T=$1; K=$2; shift 2
until ! tasklist //FI "IMAGENAME eq crash_twoc.exe" | grep -q crash_twoc; do sleep 2; done
rm -f runs/${T}_* runs/$T.*
"$PY" launch.py $T ${SECS:-900} "$@" > runs/$T.launch.txt 2>&1 &
sleep 2
"$PY" newgame2.py $T > runs/$T.newgame.txt 2>&1
sleep 1
echo "ly=32767" > runs/$T.pad.txt; sleep 1.5; echo > runs/$T.pad.txt; sleep 3
"$PY" ctl.py $T grab ${T}_c0 > /dev/null
"$PY" ctl.py $T press up*3 a wait:2
"$PY" ctl.py $T grab ${T}_c1 > /dev/null
"$PY" ctl.py $T press $K wait:2
"$PY" ctl.py $T grab ${T}_c2 > /dev/null
if [ -n "$OVERWRITE" ]; then "$PY" ctl.py $T press up a wait:2; "$PY" ctl.py $T grab ${T}_c2b > /dev/null; fi
"$PY" ctl.py $T press up a wait:1
"$PY" ctl.py $T grab ${T}_c3 > /dev/null
sleep 4
"$PY" ctl.py $T grab ${T}_c4 > /dev/null
grep -a "\[SAVE\]" runs/$T.err.log
echo done $T
