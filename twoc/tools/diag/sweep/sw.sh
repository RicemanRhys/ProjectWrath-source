# sw.sh TAG LEVEL ROOM:PAD [ENV...] -- step 73 sweep: new game WOMBAT, LEVEL through pad ROOM:PAD (gopad), wait WAIT (20) s,
# grab, play.py PLAY (60) s, Start (pause) + grab, kill. PASS=1: 16:9 + interpolation; PASS=2: 4:3, 60 fps.
S=$(dirname $(readlink -f $0))
cd $S
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
T=$1; L=$2; RP=$3; shift 3
if [ "${PASS:-1}" = 1 ]; then PE="RECOMP_FRAMERATE=display RECOMP_WIDESCREEN=1"; else PE="RECOMP_FRAMERATE=60 RECOMP_WIDESCREEN=0"; fi
until ! tasklist //FI "IMAGENAME eq crash_twoc.exe" | grep -q crash_twoc; do sleep 3; done
rm -f runs/${T}_* runs/${T}.*
"$PY" launch.py $T ${SECS:-420} RECOMP_RENDER_SIZE=${RS:-1280x720} $PE "$@" > runs/$T.launch.txt 2>&1 &
sleep 3
"$PY" newgame2.py $T > runs/$T.newgame.txt 2>&1
sleep 2
[ -n "$PRE" ] && "$PY" $PRE > runs/$T.pre.txt 2>&1
if [ "$L" = "u" ]; then            # upstairs: room 6 pad RP by the lift
  "$PY" -u gopad6.py $T $RP > runs/$T.gopad.txt 2>&1
  sleep ${WAIT:-20}
elif [ "$L" != "-" ]; then
  "$PY" -u gopad2.py $T $L $RP > runs/$T.gopad.txt 2>&1
  sleep ${WAIT:-20}
fi
"$PY" ctl.py $T grab ${T}_start > /dev/null
"$PY" play.py $T ${PLAY:-60} > runs/$T.play.txt 2>&1
"$PY" ctl.py $T press start > /dev/null; sleep 3
"$PY" ctl.py $T grab ${T}_pause > /dev/null
sleep 1
taskkill //IM crash_twoc.exe //F > /dev/null 2>&1
wait
for f in runs/${T}_*.bmp; do [ -f "$f" ] && "$PY" topng.py $f > /dev/null && rm -f $f; done
cat runs/$T.play.txt | head -3
grep -a "loading\|gate\|pad " runs/$T.gopad.txt | tail -2
grep -a "\[PATH\].*levels.*\.nux" runs/$T.err.log | sed 's/.*levels.//' | tr '\n' ' '; echo
grep -a "CRASH\|interpolation off" runs/$T.err.log | head -3
echo done $T
