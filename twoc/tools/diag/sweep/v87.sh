# v87.sh [PARTS] -- step 87's checks with the bake on (RECOMP_D3D_BRIDGE_BAKE=1), exe folders F1 (final) and BC
# (F1 + ../step87_bake_check.patch: TMP_ICHECK=1 draws every baked pass the ordinary way first and compares).
#   rc: REPLAY_CHECK with 2 passes (the second baked), render thread sync, THREADCHECK: cast_bug gliding, avalanch,
#       upstairs, H2 Oh No and its pause still
#   sf: the sub-frame check, 4 sub-frames (adaptive off), render thread on, then off: the same four places
#   aa: the sub-frame check with MSAA 4x (cast_bug gliding), FXAA (avalanch), SMAA (upstairs), render thread on
# One summary to runs/v87_<part>.txt.
cd $(dirname $(readlink -f $0))
W() { cygpath -w $(pwd)/$1/crash_twoc.exe; }
TC=RECOMP_D3D_BRIDGE_THREADCHECK=1
BK=RECOMP_D3D_BRIDGE_BAKE=1
PZ="pad:buttons=0x10 wait:0.15 pad: wait:4 grab:pause wait:4 grab:pause2 pad:buttons=0x10 wait:0.15 pad: wait:4 grab:back"
cb() { WAIT=30 bash sx.sh $1 u 1 "wait:5 x:glide.py:$1:40:-3:7:3" --exe=$(W $2) RECOMP_HUD=edges "${@:3}" > /dev/null 2>&1; }
av() { WAIT=30 bash sx.sh $1 5 4:1 "wait:40" --exe=$(W $2) RECOMP_HUD=edges "${@:3}" > /dev/null 2>&1; }
up() { PRE="upst.py $1" WAIT=3 bash sx.sh $1 - - "wait:40" --exe=$(W $2) RECOMP_HUD=edges "${@:3}" > /dev/null 2>&1; }
h2() { WAIT=22 bash sx.sh $1 2 1:1 "wait:3 grab:lvl wait:6 grab:lvl2 $PZ" --exe=$(W $2) RECOMP_HUD=edges "${@:3}" > /dev/null 2>&1; }
sum() {  # TAG
  local L=runs/$1.err.log
  echo "-- $1: $(grep -a 'levels\\' $L | tail -1 | sed 's/.*levels\\//')"
  grep -a "replay check: [0-9]* frames" $L | tail -1
  echo "   'differs in' lines: $(grep -ac 'differs in' $L); bad lines: $(grep -ac 'THREADCHECK:\|CRASH\|frame slots\|watchdog' $L)"
  grep -a "host baked draws: [0-9]" $L | tail -1
  grep -a "interpolation sub-frames\|anti-aliasing:" $L | tail -1
}
for part in ${*:-rc sf aa}; do
  case $part in
    rc) for p in cb av up h2; do $p v87r_$p F1 RECOMP_D3D_BRIDGE_REPLAY_CHECK=1 RECOMP_D3D_BRIDGE_REPLAY=2 RECOMP_RENDER_THREAD=sync $TC $BK; done
        { for p in cb av up h2; do sum v87r_$p; done; } > runs/v87_rc.txt ;;
    sf) for rt in on off; do for p in cb av up h2; do
          $p v87s${rt}_$p BC TMP_ICHECK=1 RECOMP_FRAMERATE_ADAPTIVE=0 RECOMP_RENDER_THREAD=$rt $TC $BK; done; done
        { for rt in on off; do for p in cb av up h2; do sum v87s${rt}_$p; done; done; } > runs/v87_sf.txt ;;
    aa) cb v87a_cb BC TMP_ICHECK=1 RECOMP_FRAMERATE_ADAPTIVE=0 RECOMP_RENDER_THREAD=on RECOMP_ANTIALIASING=msaa4x $TC $BK
        av v87a_av BC TMP_ICHECK=1 RECOMP_FRAMERATE_ADAPTIVE=0 RECOMP_RENDER_THREAD=on RECOMP_ANTIALIASING=fxaa $TC $BK
        up v87a_up BC TMP_ICHECK=1 RECOMP_FRAMERATE_ADAPTIVE=0 RECOMP_RENDER_THREAD=on RECOMP_ANTIALIASING=smaa $TC $BK
        { for p in cb av up; do sum v87a_$p; done; } > runs/v87_aa.txt ;;
  esac
  echo "PART $part done"
done
echo ALLDONE
