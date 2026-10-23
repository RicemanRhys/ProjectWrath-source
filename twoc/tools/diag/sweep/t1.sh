# t1.sh PFX "EXE[:ENV=V,...]..." -- step 103: Gold Rush's late spot (the user's capture: Crash at 268.5 6.05 267.4),
# Match display 240 Hz, render thread on, 16:9, 4K render size windowed: standing (3 swaps of sub-frames, 6 INTERP_REC
# frames), then gliding +-2 along z (3 swaps); then a second run with INTERP_T0=1 gliding (4 swaps: sub-frame 1 at t = 0).
cd $(dirname $(readlink -f $0))
P=$1; shift
for spec in "$@"; do
  E=${spec%%:*}; ENVS=""; [ "$spec" != "$E" ] && ENVS=$(echo ${spec#*:} | tr ',' ' ')
  N=$(basename $E)$(echo "$ENVS" | tr -dc 'A-Z0-9' | tail -c 6)
  T=${P}st_$N
  RS=3840x2160 KEEP=1 LV=19 RP=1:1 bash dd.sh $T $E "wait:3 x:setpos.py:268.5:6.05:267.4:2 wait:5 sub:3 wait:4 rec:6 wait:3 bg:glide2.py:$T:20:265.4:269.4:3 wait:5 sub:3 wait:6" $ENVS > runs/log_$T.txt 2>&1
  T=${P}t0_$N
  RS=3840x2160 KEEP=1 LV=19 RP=1:1 bash dd.sh $T $E "wait:3 x:setpos.py:268.5:6.05:267.4:2 wait:3 bg:glide2.py:$T:20:265.4:269.4:3 wait:5 sub:4 wait:6" RECOMP_D3D_BRIDGE_INTERP_T0=1 $ENVS > runs/log_$T.txt 2>&1
done
echo t1 done
