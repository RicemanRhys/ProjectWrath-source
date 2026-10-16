# vfin.sh -- step 79, after the mid-refresh commit (3493f10): the stress script, the 10-level sweep, the measurements
# with the thread on (adaptive on, two rounds), identity against step 78; outputs under runs/fin.*.
cd $(dirname $(readlink -f $0))
bash t79a.sh F > runs/fin.live.txt 2>&1
cp runs/t79a.ov.txt runs/fin.ov.txt; cp runs/t79a.ls.txt runs/fin.ls.txt
TC="RECOMP_RENDER_THREAD=on RECOMP_D3D_BRIDGE_THREADCHECK=1"
X=$(cygpath -w $(pwd)/F/crash_twoc.exe)
PZ="pad:buttons=0x10 wait:0.15 pad: wait:4 grab:pause pad:buttons=0x10 wait:0.15 pad: wait:6 grab:back"
for i in 1 2 3 4 5 6 7 8 9 10; do
  set -- $(sed -n ${i}p main.txt)
  WAIT=20 bash sx.sh fs$i $1 $2 "grab:start wait:15 grab:mid $PZ" --exe=$X RECOMP_HUD=edges $TC 2>&1 | tail -2 | head -1
done < /dev/null > runs/fin.sweep.txt 2>&1
{ for i in 1 2 3 4 5 6 7 8 9 10; do
    grep -a "THREADCHECK:\|watchdog\|frame slots\|CRASH" runs/fs$i.err.log | grep -v "host THREADCHECK" | head -3
    grep -a "interpolation sub-frames" runs/fs$i.err.log | tail -1 | cut -c1-200
    python shown2.py runs/fs$i.err.log
  done; } >> runs/fin.sweep.txt 2>&1
XT=$(cygpath -w $(pwd)/TP/crash_twoc.exe)
for r in 1 2; do
  if [ $r = 1 ]; then L="cb cbg av up"; else L="up av cbg cb"; fi
  for w in $L; do bash m79.sh f_${w}_t$r $w --exe=$XT RECOMP_RENDER_THREAD=on 2>&1 | tail -1; done
done
{ for w in cb cbg av up; do python tpsum.py runs/f_${w}_t1.tprof.csv runs/f_${w}_t2.tprof.csv; done
  python shown2.py runs/f_*.err.log
  grep -a "host interpolation: [0-9.]* fps\|THREADCHECK:\|watchdog\|frame slots\|CRASH" runs/f_*.err.log | head; } > runs/fin.m.txt 2>&1
{ bash id.sh B F; bash idcmp.sh B F; } > runs/fin.id.txt 2>&1
echo VFIN DONE
