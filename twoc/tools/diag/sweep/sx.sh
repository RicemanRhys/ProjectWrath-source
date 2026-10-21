# sx.sh TAG LEVEL ROOM:PAD "ACT STEPS" [ENV...] -- step 73 scenarios: like sw.sh (PASS, PRE, WAIT, SECS, RS), but after
# the load runs act.py STEPS (x:play.py:TAG:60 plays; burst:NAME:N:GAP grabs), then kills. LEVEL u = upstairs pad.
S=$(dirname $(readlink -f $0))
cd $S
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
T=$1; L=$2; RP=$3; A=$4; shift 4
case "${PASS:-1}" in
  1) PE="RECOMP_FRAMERATE=display RECOMP_WIDESCREEN=1";;
  # step 99: pass A (Match display at 240 Hz, render thread Auto, 16:9, native 4K, HUD at the edges, adaptive off,
  # fullscreen; THREADCHECK on) and pass B (4:3, Game (60), no render thread, windowed)
  A) PE="RECOMP_FRAMERATE=display RECOMP_RENDER_THREAD=auto RECOMP_WIDESCREEN=1 RECOMP_HUD=edges RECOMP_FRAMERATE_ADAPTIVE=0 RECOMP_FULLSCREEN=1 RECOMP_D3D_BRIDGE_THREADCHECK=1"; RS=${RS:-native};;
  B) PE="RECOMP_FRAMERATE=60 RECOMP_RENDER_THREAD=off RECOMP_WIDESCREEN=0 RECOMP_FULLSCREEN=0";;
  *) PE="RECOMP_FRAMERATE=60 RECOMP_WIDESCREEN=0";;
esac
until ! tasklist //FI "IMAGENAME eq crash_twoc.exe" | grep -q crash_twoc; do sleep 3; done
rm -f runs/${T}_* runs/${T}.*
"$PY" launch.py $T ${SECS:-600} RECOMP_RENDER_SIZE=${RS:-1280x720} $PE "$@" > runs/$T.launch.txt 2>&1 &
sleep 3
"$PY" newgame2.py $T > runs/$T.newgame.txt 2>&1
sleep 2
[ -n "$PRE" ] && "$PY" $PRE > runs/$T.pre.txt 2>&1
if [ "$L" = "u" ]; then
  "$PY" -u gopad6.py $T $RP > runs/$T.gopad.txt 2>&1
  sleep ${WAIT:-20}
elif [ "$L" != "-" ]; then
  "$PY" -u gopad2.py $T $L $RP > runs/$T.gopad.txt 2>&1
  sleep ${WAIT:-20}
fi
"$PY" act.py $T $A > runs/$T.act.txt 2>&1
sleep ${TAIL:-2}
taskkill //IM crash_twoc.exe //F > /dev/null 2>&1
wait
for f in runs/${T}_*.bmp; do [ -f "$f" ] && "$PY" topng.py $f > /dev/null && rm -f $f; done
"$PY" summ.py $T
grep -a "CRASH" runs/$T.err.log | head -3
echo done $T
