# v2.sh -- step 84's checks for the cache's 4 ways and shared blocks (b2), the interpolation recording (c2) and the
# signature in H2 Oh No: VB (b) check mode in cast_bug again (its first run never loaded the level); VB2 check mode;
# HCK (blocks never freed, a replayed draw from a released one counted) with the render thread on; ICK (the old
# camera scan and key loop beside the new); REPLAY_CHECK + the sub-frame check with the cache on (sync); SC in H2 Oh
# No with the game's pause.
cd $(dirname $(readlink -f $0))
W() { cygpath -w $(pwd)/$1/crash_twoc.exe; }
TC=RECOMP_D3D_BRIDGE_THREADCHECK=1
three() {   # PFX EXE ENV...: cast_bug gliding, avalanch, upstairs, the render thread on
  P=$1; X=$2; shift 2
  WAIT=22 bash sx.sh ${P}_cb u 1 "wait:5 x:glide.py:${P}_cb:30:-3:7:3" --exe=$(W $X) RECOMP_HUD=edges RECOMP_RENDER_THREAD=on "$@" 2>&1 | tail -1
  WAIT=22 bash sx.sh ${P}_av 5 4:1 "wait:30" --exe=$(W $X) RECOMP_HUD=edges RECOMP_RENDER_THREAD=on "$@" 2>&1 | tail -1
  PRE="upst.py ${P}_up" WAIT=3 bash sx.sh ${P}_up - - "wait:30" --exe=$(W $X) RECOMP_HUD=edges RECOMP_RENDER_THREAD=on "$@" 2>&1 | tail -1
}
loads() { for t in "$@"; do grep -a "levels\\\\" runs/$t.err.log | tail -1 | sed "s/^/$t: /"; done; }
PZ="pad:buttons=0x10 wait:0.15 pad: wait:4 grab:pause wait:4 grab:pause2 pad:buttons=0x10 wait:0.15 pad: wait:4 grab:back"
{ echo "== VB (b) check mode, cast_bug gliding again"
  P=vcc2; WAIT=26 bash sx.sh ${P}_cb u 1 "wait:5 x:glide.py:${P}_cb:30:-3:7:3" --exe=$(W VB) RECOMP_HUD=edges RECOMP_RENDER_THREAD=on RECOMP_D3D_BRIDGE_VCACHE=check 2>&1 | tail -1
  loads vcc2_cb; grep -a "vertex cache:\|vertex cache CHECK" runs/vcc2_cb.err.log | tail -2
  echo "== VB2 check mode (4 ways, shared blocks)"
  three vk2 VB2 RECOMP_D3D_BRIDGE_VCACHE=check
  loads vk2_cb vk2_av; for t in cb av up; do grep -a "vertex cache:\|vertex cache CHECK" runs/vk2_$t.err.log | tail -2; done
  echo "== HCK: blocks' lifetimes with the render thread on, the cache on, THREADCHECK"
  three hck HCK RECOMP_D3D_BRIDGE_VCACHE=1 $TC
  loads hck_cb hck_av; for t in cb av up; do grep -a "TMP\] hold\|vertex cache:\|THREADCHECK\|CRASH" runs/hck_$t.err.log | tail -3; done
  echo "== ICK: the camera scan and the object key, old beside new"
  three ick ICK
  loads ick_cb ick_av; for t in cb av up; do grep -a "TMP\] ic check" runs/ick_$t.err.log | tail -1; done
  echo "== SC in H2 Oh No and its pause"
  WAIT=22 bash sx.sh sgh 2 1:1 "wait:3 grab:lvl wait:6 grab:lvl2 $PZ" --exe=$(W SC) RECOMP_HUD=edges RECOMP_RENDER_THREAD=on RECOMP_D3D_BRIDGE_SIGCHECK=1 2>&1 | tail -1
  loads sgh; grep -a "sigcheck" runs/sgh.err.log | tail -2
} > runs/v2.pre.txt 2>&1
bash v84.sh vb2 VB2 ICB2 B RECOMP_D3D_BRIDGE_VCACHE=1
echo ALLDONE
