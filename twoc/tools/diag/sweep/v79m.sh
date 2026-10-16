# v79m.sh -- step 79 measurements with the TPROF build (TP): cast_bug start (cb) and gliding (cbg), avalanch (av),
# upstairs (up), native 4K 16:9 at 240 Hz, HUD at the edges; the render thread off (o) and on (t), adaptive on, two
# rounds interleaved (the second in reverse); then adaptive off (always 4 frames each game frame), off and on, once.
cd $(dirname $(readlink -f $0))
X=$(cygpath -w $(pwd)/TP/crash_twoc.exe)
run() {   # TAG WHERE MODE
  if [ $3 = t ]; then R=RECOMP_RENDER_THREAD=on; else R=RECOMP_RENDER_THREAD=off; fi
  bash m79.sh $1 $2 --exe=$X $R 2>&1 | tail -1
}
for r in 1 2; do
  if [ $r = 1 ]; then L="cb cbg av up"; M="o t"; else L="up av cbg cb"; M="t o"; fi
  for w in $L; do for m in $M; do run m_${w}_$m$r $w $m; done; done
done
for w in cb cbg av up; do for m in o t; do ADAPT=0 run m0_${w}_$m $w $m; done; done
{ echo "== adaptive on"; for w in cb cbg av up; do for m in o t; do python tpsum.py runs/m_${w}_${m}1.tprof.csv runs/m_${w}_${m}2.tprof.csv; done; done
  echo "== adaptive off"; for w in cb cbg av up; do for m in o t; do python tpsum.py runs/m0_${w}_$m.tprof.csv; done; done
  echo "== frames shown at 1 refresh (the last 2 intervals)"; python shown2.py runs/m_*.err.log runs/m0_*.err.log
  echo "== adaptive changes"; grep -a "host interpolation: [0-9.]* fps" runs/m_*.err.log runs/m0_*.err.log | cut -c1-220
  echo "== threadcheck/watchdog/slots"; grep -a "THREADCHECK:\|watchdog\|frame slots\|CRASH" runs/m_*.err.log runs/m0_*.err.log | head
} > runs/v79m.txt 2>&1
echo ALLDONE
