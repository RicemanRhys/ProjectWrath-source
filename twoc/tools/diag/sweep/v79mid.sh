# v79mid.sh -- step 79: frames shown evenly with the render thread, its sub-frames presented as soon as the queue has
# room (as on one thread) or held to mid-refresh (RECOMP_D3D_BRIDGE_INTERP_MID=1), and the thread off; upstairs
# standing and cast_bug gliding, TP build, at RS (1280x720: the sweep's window), three rounds interleaved.
cd $(dirname $(readlink -f $0))
X=$(cygpath -w $(pwd)/TP/crash_twoc.exe)
export RS=${RS:-1280x720}
run() {   # TAG WHERE MODE: o off, t on, m on + mid
  case $3 in
    o) E="RECOMP_RENDER_THREAD=off" ;;
    t) E="RECOMP_RENDER_THREAD=on" ;;
    m) E="RECOMP_RENDER_THREAD=on RECOMP_D3D_BRIDGE_INTERP_MID=1" ;;
  esac
  bash m79.sh $1 $2 --exe=$X $E 2>&1 | tail -1
}
for r in 1 2 3; do
  case $r in 1) M="o t m" ;; 2) M="m o t" ;; 3) M="t m o" ;; esac
  for w in up cbg; do for m in $M; do run mm_${w}_$m$r $w $m; done; done
done
{ for w in up cbg; do for m in o t m; do python tpsum.py runs/mm_${w}_${m}1.tprof.csv runs/mm_${w}_${m}2.tprof.csv runs/mm_${w}_${m}3.tprof.csv; done; done
  for w in up cbg; do for m in o t m; do K=2 python shown2.py runs/mm_${w}_${m}1.err.log runs/mm_${w}_${m}2.err.log runs/mm_${w}_${m}3.err.log; done; done
} > runs/v79mid.txt 2>&1
echo ALLDONE
