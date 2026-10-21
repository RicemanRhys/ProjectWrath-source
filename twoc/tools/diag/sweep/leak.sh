# leak.sh TAG endur|enter|stand [ARGS] -- step 99 part 4 item 3, pass A's settings, one process each:
#   endur MIN         new game, cycle.py MIN minutes (a level on pad 1:1, 120 s of play, back to the hub, 30 s)
#   enter LEVEL N     new game, cycle.py with LEVELS=LEVEL ENTRIES=N (40 s of play, 10 s in the hub)
#   stand LEVEL MIN   new game, LEVEL on pad 1:1, MIN minutes standing at its start (lives kept), then pause >
#                     WARP ROOM > YES and MIN minutes standing in the hub
# The [MEM] lines (every 10 s) are read afterwards by memfit.py.
cd "$(dirname "$0")"
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
T=$1; K=$2; shift 2
until ! tasklist //FI "IMAGENAME eq crash_twoc.exe" | grep -q crash_twoc; do sleep 5; done
rm -f runs/${T}_* runs/${T}.*
PE="RECOMP_FRAMERATE=display RECOMP_RENDER_THREAD=auto RECOMP_WIDESCREEN=1 RECOMP_HUD=edges RECOMP_FRAMERATE_ADAPTIVE=0 RECOMP_FULLSCREEN=1 RECOMP_D3D_BRIDGE_THREADCHECK=1 RECOMP_RENDER_SIZE=native"
"$PY" launch.py $T 9000 $PE > runs/$T.launch.txt 2>&1 &
sleep 4
"$PY" newgame2.py $T > runs/$T.newgame.txt 2>&1
[ "$1" = "cb" ] && { L=$("$PY" rdlv.py 60 | tr -d '\r'); echo "cast_bug is level $L" > runs/$T.cb.txt; shift; set -- $L "$@"; }
case $K in
  endur) "$PY" -u cycle.py $T $1 > runs/$T.cyc.out 2>&1 ;;
  enter) LEVELS=$1 ENTRIES=$2 PLAYSECS=40 HUBSECS=10 "$PY" -u cycle.py $T 600 > runs/$T.cyc.out 2>&1 ;;
  stand)
    "$PY" -u gopad2.py $T $1 1:1 > runs/$T.gopad.txt 2>&1
    sleep 25
    "$PY" ctl.py $T grab ${T}_lv0 > /dev/null
    END=$(( $(date +%s) + $2 * 60 ))
    while [ $(date +%s) -lt $END ]; do "$PY" poke.py 56299C 1 9 > /dev/null; sleep 30; done
    "$PY" ctl.py $T grab ${T}_lv1 > /dev/null
    for a in 1 2 3; do
      P0=$(stat -c %s runs/$T.err.log)
      "$PY" ctl.py $T press start wait:4 down down a wait:1 up a > /dev/null
      sleep 25
      tail -c +$P0 runs/$T.err.log | grep -aq 'levels.b.hub.hub.nux' && { echo "hub after $a" > runs/$T.hub.txt; break; }
      "$PY" ctl.py $T press b wait:1 b > /dev/null
    done
    "$PY" ctl.py $T grab ${T}_hub0 > /dev/null
    sleep $(( $2 * 60 ))
    "$PY" ctl.py $T grab ${T}_hub1 > /dev/null ;;
esac
"$PY" ctl.py $T grab ${T}_end > /dev/null
taskkill //IM crash_twoc.exe //F > /dev/null 2>&1
wait
for f in runs/${T}_*.bmp; do [ -f "$f" ] && "$PY" topng.py $f > /dev/null && rm -f $f; done
"$PY" memfit.py runs/$T.err.log 5 > runs/$T.memfit.txt
echo LEAK DONE $T
