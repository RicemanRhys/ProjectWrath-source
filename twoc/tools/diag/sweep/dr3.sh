# dr3.sh TAG EXEDIR [ENV...] -- step 106: Smokey, the vehicle at (-6.5, 0.3, 515) driven by ap.py to the finish (wp_515.txt),
# the rolling capture with automatic saves (RECOMP_D3D_BRIDGE_ROLLING_AUTO, AUTO default 5) and the draws in full
# (FULL=1 default); FR=display for Match display + render thread, else Game (60). Saves in runs/TAG/roll/.
S=$(dirname $(readlink -f $0)); cd $S
T=$1; E=$2; shift 2
rm -rf runs/$T; mkdir -p runs/$T/roll
D=$(cygpath -m $(pwd)/runs/$T)
SECS=${SECS:-200} bash sm105.sh $T $E "wait:1 pad:a=255_ly=32767 wait:2.5 x:drv.py:once:-6.5:0.3:515:0 bg:ap.py:$T:${DRIVE:-22}:$(cat wp_515.txt) wait:${DRIVE:-22} wait:12" \
  RECOMP_D3D_BRIDGE_ROLLING=$D/roll RECOMP_D3D_BRIDGE_ROLLING_TRIGGER=$D/roll.trig RECOMP_D3D_BRIDGE_ROLLING_FULL=${FULL:-1} \
  RECOMP_D3D_BRIDGE_ROLLING_AUTO=${AUTO:-5} RECOMP_D3D_BRIDGE_ANOMALY=1 "$@" > runs/log_$T.txt 2>&1
grep -a "\[ROLL\]" runs/$T.err.log | grep -v "ready\|rolling capture on\|automatic saves:" | sed 's/ to .*rolling_/ rolling_/'
