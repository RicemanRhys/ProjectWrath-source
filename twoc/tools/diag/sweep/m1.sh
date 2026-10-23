# m1.sh PFX "EXE[:ENV=V,...]..." -- step 103: Medieval Madness (level 16, pad 1:1) at the user's capture spot (Crash in
# his ball on the wall-top path at 20.5 15.3 333.0: ballmove.py after a short run-up), then rolling gently along the
# path (lx -12000): 3 swaps of sub-frames + 8 INTERP_REC frames; a second run with INTERP_T0=1 (4 swaps).
cd $(dirname $(readlink -f $0))
P=$1; shift
A="wait:3 pad:lx=16000_ly=-32767 wait:0.5 pad: x:ballmove.py:20.5:15.3:333.0:3 wait:1.5 grab:a pad:lx=-12000 wait:0.5"
for spec in "$@"; do
  E=${spec%%:*}; ENVS=""; [ "$spec" != "$E" ] && ENVS=$(echo ${spec#*:} | tr ',' ' ')
  N=$(basename $E)$(echo "$ENVS" | tr -dc 'A-Z0-9' | tail -c 6)
  T=${P}st_$N
  RS=3840x2160 KEEP=1 LV=16 RP=1:1 bash dd.sh $T $E "$A sub:3 rec:8 wait:1.5 pad: grab:b wait:3" $ENVS > runs/log_$T.txt 2>&1
  T=${P}t0_$N
  RS=3840x2160 KEEP=1 LV=16 RP=1:1 bash dd.sh $T $E "$A sub:4 wait:1.5 pad: grab:b wait:3" RECOMP_D3D_BRIDGE_INTERP_T0=1 $ENVS > runs/log_$T.txt 2>&1
done
echo m1 done
