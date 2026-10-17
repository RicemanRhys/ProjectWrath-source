# fc.sh -- step 84's last chain: the cache emptied often (HF1, a 4 MB limit, check mode, the hold check) in cast_bug
# gliding and avalanch; the final build's checks (F2, link-time optimisation: REPLAY_CHECK sync + THREADCHECK, the
# sub-frame check with ICF2, identity F1 against F2) and the cache's default (identity B against F1); then the final
# timing: P (before), PB2 with the cache on, PF2 (final), two rounds.
cd $(dirname $(readlink -f $0))
W() { cygpath -w $(pwd)/$1/crash_twoc.exe; }
{ for w in cb av; do
    T=vfl_$w
    if [ $w = cb ]; then
      WAIT=22 bash sx.sh $T u 1 "wait:5 x:glide.py:$T:30:-3:7:3" --exe=$(W HF1) RECOMP_HUD=edges RECOMP_RENDER_THREAD=on RECOMP_D3D_BRIDGE_VCACHE=check RECOMP_DEV_VCACHE_MB=4 2>&1 | tail -1
    else
      WAIT=22 bash sx.sh $T 5 4:1 "wait:30" --exe=$(W HF1) RECOMP_HUD=edges RECOMP_RENDER_THREAD=on RECOMP_D3D_BRIDGE_VCACHE=check RECOMP_DEV_VCACHE_MB=4 2>&1 | tail -1
    fi
    grep -a "levels\\\\" runs/$T.err.log | tail -1; grep -a "vertex cache\|TMP\] hold" runs/$T.err.log | tail -3
  done
} > runs/fc.flush.txt 2>&1
bash v84.sh vf F2 ICF2 F1
bash id.sh B F1 > /dev/null 2>&1; bash idcmp.sh B F1 > runs/fc.idF1.txt 2>&1
. ./fin.sh none > /dev/null 2>&1   # run() only
for r in 1 2; do for w in cb av up; do for c in P PB2:vc PF2; do
  run fz$r${w}_$(echo $c | tr ':' '_') $w $c >> fc.log; done; done; done
echo ALLDONE >> fc.log
