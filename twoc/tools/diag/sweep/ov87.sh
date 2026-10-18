# ov87.sh -- step 87: the settings overlay held, then running with a live change (t78o.sh), under the sub-frame check
# (BC, the bake on, render thread on, THREADCHECK); then H2 Oh No's sub-frame check with the render thread off again
# (its first try never loaded the level: the walk to the pad stalled with one thread in check mode).
cd $(dirname $(readlink -f $0))
bash t78o.sh v87o $(pwd)/BC TMP_ICHECK=1 RECOMP_FRAMERATE_ADAPTIVE=0 RECOMP_RENDER_THREAD=on RECOMP_D3D_BRIDGE_BAKE=1 \
  RECOMP_D3D_BRIDGE_THREADCHECK=1 > runs/v87_ov.txt 2>&1
for m in h r; do grep -a "replay check: [0-9]* frames\|host baked draws: [0-9]" runs/v87o$m.err.log | tail -2; \
  echo "differs lines $(grep -ac 'differs in' runs/v87o$m.err.log)"; done >> runs/v87_ov.txt
PZ="pad:buttons=0x10 wait:0.15 pad: wait:4 grab:pause wait:4 grab:pause2 pad:buttons=0x10 wait:0.15 pad: wait:4 grab:back"
WAIT=40 bash sx.sh v87soff_h2b 2 1:1 "wait:3 grab:lvl wait:6 grab:lvl2 $PZ" --exe=$(cygpath -w $(pwd)/BC/crash_twoc.exe) \
  RECOMP_HUD=edges TMP_ICHECK=1 RECOMP_FRAMERATE_ADAPTIVE=0 RECOMP_RENDER_THREAD=off RECOMP_D3D_BRIDGE_THREADCHECK=1 \
  RECOMP_D3D_BRIDGE_BAKE=1 > /dev/null 2>&1
{ grep -a 'levels\' runs/v87soff_h2b.err.log | tail -1; grep -a "replay check: [0-9]* frames\|host baked draws: [0-9]" runs/v87soff_h2b.err.log | tail -2;
  echo "differs lines $(grep -ac 'differs in' runs/v87soff_h2b.err.log)"; } >> runs/v87_ov.txt
echo ALLDONE >> runs/v87_ov.txt
