# fin.sh [PART...] -- step 84's measurements, standing, the render thread on, native 4K at 240 Hz, adaptive on,
# interleaved by build, two rounds. Parts: items (P baseline, PA a, PB b, PC c, PB2 b2+c2; :vc = the vertex cache
# on), flags (D0 as twoc/build, DLT link-time optimisation, DAV AVX2, DLA both, DPG profile-guided; the snapshot
# d84), ack (PB2 with the cache on, the NV2A ack thread spinning or waiting 100 / 250 us, with thr.py's per-thread
# CPU). Each run's game-thread CSV and both threads' CSV in runs/; fsum.py tabulates.
cd $(dirname $(readlink -f $0))
run() {  # TAG WHERE CFG
  local X=${3%%:*} E=""
  case $3 in *:vc*) E="$E RECOMP_D3D_BRIDGE_VCACHE=1";; esac
  case $3 in *:a100*) E="$E RECOMP_NV2A_ACK_WAIT=100";; esac
  case $3 in *:a250*) E="$E RECOMP_NV2A_ACK_WAIT=250";; esac
  if [ -n "$THR" ]; then
    STEPS="wait:8 x:thr.py:$X/crash_twoc.map:10:runs/$1.thr.txt wait:7" bash m84.sh $1 $2 $X $E > /dev/null 2>&1
  else
    bash m84.sh $1 $2 $X $E > /dev/null 2>&1
  fi
  echo "$1 $(grep -a 'levels\\' runs/$1.err.log | tail -1 | sed 's/.*levels\\//')"
}
PARTS=${*:-items flags ack}
for part in $PARTS; do
  case $part in
    items) for r in 1 2; do for w in cb av up; do for c in P PA PB PC PC:vc PB2 PB2:vc; do
             run fi$r${w}_$(echo $c | tr ':' '_') $w $c; done; done; done ;;
    flags) for r in 1 2; do for w in cb av up; do for c in D0 DLT DAV DLA DPG; do
             run ff$r${w}_$c $w $c; done; done; done ;;
    ack)   for r in 1 2; do for c in PB2:vc PB2:vc:a100 PB2:vc:a250; do
             THR=1 run fa${r}cb_$(echo $c | tr ':' '_') cb $c; done; done
           for w in av up; do for c in PB2:vc PB2:vc:a100 PB2:vc:a250; do
             THR=1 run fa1${w}_$(echo $c | tr ':' '_') $w $c; done; done ;;
  esac
done
echo ALLDONE
