cd $(dirname $(readlink -f $0))
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
T=${1:-en1}
rm -f runs/${T}_* runs/${T}.*
"$PY" launch.py $T 4500 --exe="$(cygpath -w exe2/crash_twoc.exe)" RECOMP_RENDER_SIZE=1280x720 RECOMP_FRAMERATE=display RECOMP_WIDESCREEN=1 > runs/$T.launch.txt 2>&1 &
sleep 4
"$PY" procmon.py $T 60 &
"$PY" newgame2.py $T > runs/$T.newgame.txt 2>&1
"$PY" -u cycle.py $T ${MIN:-60} > runs/$T.cyc.out 2>&1
"$PY" ctl.py $T grab ${T}_end > /dev/null
sleep 65
taskkill //IM crash_twoc.exe //F > /dev/null 2>&1
wait
echo ENDUR DONE
