# v84.sh PFX EXE ICEXE BASE [EXTRA ENV...] -- step 84, one item's checks (no timing): REPLAY_CHECK with the render
# thread in sync and THREADCHECK (cast_bug gliding, avalanch, upstairs), the sub-frame check (ICEXE, a
# ../step79_subframe_check.patch build), then identity (id.sh BASE EXE: drive.py 150 s bridge off and host-only).
# EXTRA ENV goes to the checks' runs (e.g. a switch under test). Lines to runs/PFX.v84.txt.
cd $(dirname $(readlink -f $0))
P=$1; X=$2; I=$3; B=$4; shift 4
TC=RECOMP_D3D_BRIDGE_THREADCHECK=1
lines() { grep -a "render thread: on from\|THREADCHECK:\|frame slots\|watchdog\|CRASH\|vcache\|sigcheck at" runs/$1.err.log | tail -4; }
{ echo "== $P: REPLAY_CHECK, render thread sync, THREADCHECK ($X $*)"
  bash chk.sh ${P}r $(pwd)/$X RECOMP_RENDER_THREAD=sync $TC "$@"
  for t in cb av up; do lines ${P}r_$t; done
  echo "== $P: the sub-frame check ($I), render thread sync, 4 sub-frames"
  bash ichk.sh ${P}i $(pwd)/$I RECOMP_RENDER_THREAD=sync "$@"
  for t in cb av up; do lines ${P}i_$t; echo "differs-lines $(grep -ac 'differs in' runs/${P}i_$t.err.log)"; done
  echo "== $P: identity $B against $X"
  bash id.sh $B $X > /dev/null 2>&1
  bash idcmp.sh $B $X
} > runs/$P.v84.txt 2>&1
echo DONE $P
