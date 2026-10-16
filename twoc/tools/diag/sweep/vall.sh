# vall.sh -- step 79: the watchdog's one block an episode (WDT), then v79b (stress, sweep, endurance), v79m
# (measurements), v79d (identity), one after another.
cd $(dirname $(readlink -f $0))
PRE="upst.py vwd3" WAIT=3 bash sx.sh vwd3 - - "wait:20" --exe=$(cygpath -w $(pwd)/WDT/crash_twoc.exe) RECOMP_HUD=edges RECOMP_RENDER_THREAD=on TMP_RT_STALL=2900 RECOMP_D3D_BRIDGE_THREADCHECK=1 > runs/v79a.watchdog2.txt 2>&1
grep -a "TMP\]\|watchdog" runs/vwd3.err.log >> runs/v79a.watchdog2.txt
bash v79b.sh
bash v79m.sh
bash v79d.sh
echo VALL DONE
