# endur79.sh TAG -- step 79: endur.sh with the final exe (F), the render thread on and THREADCHECK, Frame rate: Display
# at 1280x720 16:9, cycling levels standing (cycle79.py) for MIN (30) minutes; procmon.py samples memory and handles.
cd $(dirname $(readlink -f $0))
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
T=${1:-en79}
until ! tasklist //FI "IMAGENAME eq crash_twoc.exe" | grep -q crash_twoc; do sleep 3; done
rm -f runs/${T}_* runs/${T}.*
"$PY" launch.py $T $(( ${MIN:-30} * 60 + 600 )) --exe="$(cygpath -w F/crash_twoc.exe)" RECOMP_RENDER_SIZE=1280x720 RECOMP_FRAMERATE=display RECOMP_WIDESCREEN=1 RECOMP_HUD=edges RECOMP_RENDER_THREAD=on RECOMP_D3D_BRIDGE_THREADCHECK=1 > runs/$T.launch.txt 2>&1 &
sleep 4
"$PY" procmon.py $T 60 &
"$PY" newgame2.py $T > runs/$T.newgame.txt 2>&1
"$PY" -u cycle79.py $T ${MIN:-30} > runs/$T.cyc.out 2>&1
"$PY" ctl.py $T grab ${T}_end > /dev/null
sleep 65
taskkill //IM crash_twoc.exe //F > /dev/null 2>&1
wait
grep -a "render thread: on from\|render thread: off from\|THREADCHECK:\|watchdog\|frame slots\|CRASH" runs/$T.err.log | head -8
tail -5 runs/$T.cycle.txt
grep -ac "stood" runs/$T.cycle.txt
echo ENDUR DONE
