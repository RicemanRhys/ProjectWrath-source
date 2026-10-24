# dr2.sh TAG EXEDIR START_X START_Y START_Z "WAYPOINTS" [ENV...] -- step 106: as dr.sh, but the rolling capture saved
# NSAVE times (default 3), every ROLLSECS (default 8) less 0.3 s, so a drive of NSAVE * ~8 s is kept in pieces.
S=$(dirname $(readlink -f $0)); cd $S
T=$1; E=$2; X=$3; Y=$4; Z=$5; W=$6; shift 6
mkdir -p runs/$T/roll
D=$(cygpath -m $(pwd)/runs/$T)
RS_=${ROLLSECS:-8}; G=$(awk "BEGIN{print $RS_-0.3}")
ST="wait:1 pad:a=255_ly=32767 wait:2.5 x:drv.py:once:$X:$Y:$Z:0 bg:ap.py:$T:${DRIVE:-26}:$W ${PRE_ST:-}"
for i in $(seq ${NSAVE:-3}); do ST="$ST wait:$G x:touch.py:runs/$T/roll.trig"; done
SECS=${SECS:-200} bash sm105.sh $T $E "$ST wait:4 ${EXTRA:-}" \
  RECOMP_D3D_BRIDGE_ROLLING=$D/roll RECOMP_D3D_BRIDGE_ROLLING_TRIGGER=$D/roll.trig RECOMP_D3D_BRIDGE_ROLLING_SECS=$RS_ \
  RECOMP_D3D_BRIDGE_ANOMALY=1 "$@" > runs/log_$T.txt 2>&1
grep -a "\[ROLL\] saved" runs/$T.err.log | sed 's/ to .*rolling_/ rolling_/'
