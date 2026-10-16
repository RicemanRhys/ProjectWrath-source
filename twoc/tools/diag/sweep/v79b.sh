# v79b.sh -- step 79 verification, part B, the final exe (F), the render thread on, THREADCHECK on: the overlay held
# and running and live changes (t79a.sh), the first 10 main levels standing (a grab, the game's pause and back), and
# 30 minutes cycling levels standing (endur79.sh).
cd $(dirname $(readlink -f $0))
TC="RECOMP_RENDER_THREAD=on RECOMP_D3D_BRIDGE_THREADCHECK=1"
X=$(cygpath -w $(pwd)/F/crash_twoc.exe)
bash t79a.sh F > runs/v79b.live.txt 2>&1
cp runs/t79a.ov.txt runs/v79b.ov.txt; cp runs/t79a.ls.txt runs/v79b.ls.txt
PZ="pad:buttons=0x10 wait:0.15 pad: wait:4 grab:pause pad:buttons=0x10 wait:0.15 pad: wait:6 grab:back"
for i in 1 2 3 4 5 6 7 8 9 10; do
  set -- $(sed -n ${i}p main.txt)
  WAIT=20 bash sx.sh ms$i $1 $2 "grab:start wait:15 grab:mid $PZ" --exe=$X RECOMP_HUD=edges $TC 2>&1 | tail -2 | head -1
done < /dev/null > runs/v79b.sweep.txt 2>&1
{ for i in 1 2 3 4 5 6 7 8 9 10; do
    echo "-- ms$i (level $(sed -n ${i}p main.txt)): $(tail -1 runs/ms$i.gopad.txt)"
    grep -a "render thread: on from\|render thread: off from\|THREADCHECK:\|watchdog\|frame slots\|CRASH" runs/ms$i.err.log | head -4
    grep -a "interpolation sub-frames" runs/ms$i.err.log | tail -1 | cut -c1-230
    python shown2.py runs/ms$i.err.log
  done; } >> runs/v79b.sweep.txt 2>&1
MIN=30 bash endur79.sh en79 > runs/v79b.endur.txt 2>&1
echo ALLDONE
