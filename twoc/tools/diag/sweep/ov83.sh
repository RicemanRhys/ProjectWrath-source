# ov83.sh TAG [ENV=VAL...] -- step 83: launch at 2560x1440 (the overlay fits), wait for the main menu, Esc, RB x3
# (the Saves tab), grab TAG_o1. The game keeps running.
S=$(dirname $(readlink -f $0))
cd $S
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
T=$1; shift
until ! tasklist //FI "IMAGENAME eq crash_twoc.exe" | grep -q crash_twoc; do sleep 2; done
rm -f runs/${T}_* runs/$T.*
"$PY" launch.py $T ${SECS:-900} RECOMP_RENDER_SIZE=2560x1440 "$@" > runs/$T.launch.txt 2>&1 &
for i in $(seq 90); do grep -aq "MAIN MENU OPEN" runs/$T.err.log 2>/dev/null && break; sleep 1; done; sleep 1
grep -a "\[SAVE\]\|\[FILES\]" runs/$T.err.log
"$PY" esc.py > /dev/null; sleep 1.5
"$PY" ctl.py $T press white wait:0.5 white wait:0.5 white wait:1
"$PY" ctl.py $T grab ${T}_o1 > /dev/null
echo done $T
