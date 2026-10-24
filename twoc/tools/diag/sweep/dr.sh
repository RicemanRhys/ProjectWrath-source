# dr.sh TAG EXEDIR START_X START_Y START_Z "WAYPOINTS" [ENV...] -- step 106: Smokey, the vehicle teleported onto the road at
# START and driven by ap.py through WAYPOINTS (X,Z:X,Z...) for DRIVE s (default 7), the rolling capture on
# (ROLLSECS, default 8 s of pictures) and saved by its trigger file at the end into runs/TAG/roll/. FR=display for
# Match display + render thread (sm105.sh). Positions in runs/TAG.act.txt ('ap' lines).
S=$(dirname $(readlink -f $0)); cd $S
T=$1; E=$2; X=$3; Y=$4; Z=$5; W=$6; shift 6
mkdir -p runs/$T/roll
D=$(cygpath -m $(pwd)/runs/$T)
SECS=${SECS:-200} bash sm105.sh $T $E "wait:1 pad:a=255_ly=32767 wait:2.5 x:drv.py:once:$X:$Y:$Z:0 bg:ap.py:$T:${DRIVE:-7}:$W wait:${DRIVE:-7} x:touch.py:runs/$T/roll.trig wait:6 ${EXTRA:-}" \
  RECOMP_D3D_BRIDGE_ROLLING=$D/roll RECOMP_D3D_BRIDGE_ROLLING_TRIGGER=$D/roll.trig RECOMP_D3D_BRIDGE_ROLLING_SECS=${ROLLSECS:-8} \
  RECOMP_D3D_BRIDGE_ANOMALY=1 "$@" > runs/log_$T.txt 2>&1
grep -a "\[ROLL\]" runs/$T.err.log | tail -2
