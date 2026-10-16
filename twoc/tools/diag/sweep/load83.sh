# load83.sh TAG [ENV=VAL...] -- step 83: launch, the main menu's LOAD GAME, then grabs: TAG_l0 (menu), TAG_l1
# (after LOAD GAME), TAG_l2... as the steps go. The game keeps running.
S=$(dirname $(readlink -f $0))
cd $S
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
T=$1; shift
until ! tasklist //FI "IMAGENAME eq crash_twoc.exe" | grep -q crash_twoc; do sleep 2; done
rm -f runs/${T}_* runs/$T.*
"$PY" launch.py $T ${SECS:-900} "$@" > runs/$T.launch.txt 2>&1 &
for i in $(seq 90); do grep -aq "MAIN MENU OPEN" runs/$T.err.log 2>/dev/null && break; sleep 1; done; sleep 1
grep -a "\[SAVE\]\|\[FILES\]" runs/$T.err.log
"$PY" ctl.py $T press down wait:0.5 a wait:${LW:-12}
"$PY" ctl.py $T grab ${T}_l1 > /dev/null
echo done $T
