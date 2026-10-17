# vabc.sh -- step 84's checks for items a, b, c (no timing): the signature check (SC: old FNV beside the new hash,
# agreeing on every change), the vertex cache's check mode (VB, every draw against the old conversion) in cast_bug
# gliding, avalanch and upstairs, then v84.sh for each item (REPLAY_CHECK sync + THREADCHECK, the sub-frame check,
# identity against B).
cd $(dirname $(readlink -f $0))
W() { cygpath -w $(pwd)/$1/crash_twoc.exe; }
three() {   # PFX EXE ENV...: cast_bug gliding, avalanch, upstairs, the render thread on
  P=$1; X=$2; shift 2
  WAIT=22 bash sx.sh ${P}_cb u 1 "wait:5 x:glide.py:${P}_cb:30:-3:7:3" --exe=$(W $X) RECOMP_HUD=edges RECOMP_RENDER_THREAD=on "$@" 2>&1 | tail -1
  WAIT=22 bash sx.sh ${P}_av 5 4:1 "wait:30" --exe=$(W $X) RECOMP_HUD=edges RECOMP_RENDER_THREAD=on "$@" 2>&1 | tail -1
  PRE="upst.py ${P}_up" WAIT=3 bash sx.sh ${P}_up - - "wait:30" --exe=$(W $X) RECOMP_HUD=edges RECOMP_RENDER_THREAD=on "$@" 2>&1 | tail -1
}
{ echo "== the signature check (SC)"
  three sgc SC RECOMP_D3D_BRIDGE_SIGCHECK=1
  for t in cb av up; do grep -a "sigcheck" runs/sgc_$t.err.log | tail -2; done
  echo "== the vertex cache's check mode (VB, VCACHE=check), and check0 (no cache)"
  three vcc VB RECOMP_D3D_BRIDGE_VCACHE=check
  for t in cb av up; do grep -a "vertex cache:\|vertex cache CHECK" runs/vcc_$t.err.log | tail -2; done
  three vc0 VB RECOMP_D3D_BRIDGE_VCACHE=check0
  for t in cb av up; do grep -a "vertex cache:\|vertex cache CHECK" runs/vc0_$t.err.log | tail -2; done
} > runs/vabc.pre.txt 2>&1
bash v84.sh va A ICA B
bash v84.sh vb VB ICB B RECOMP_D3D_BRIDGE_VCACHE=1
bash v84.sh vc C ICC B
echo ALLDONE
