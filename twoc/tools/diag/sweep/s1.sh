# s1.sh PFX EXE... -- step 103: Smokey and the Bandicoot (level 3, pad 1:1) late (crate 6, -8.44 0 702.23), the
# vehicle moving (A and the stick forward), ballmove.py there, then on: INTERP_T0=1, 4 swaps of sub-frames.
cd $(dirname $(readlink -f $0))
P=$1; shift
for E in "$@"; do
  T=${P}t0_$E
  RS=3840x2160 KEEP=1 LV=3 RP=1:1 WAIT=30 bash dd.sh $T $E "wait:3 pad:a=255_ly=32767 wait:2 x:ballmove.py:-8.44:1.0:702.23:2 wait:1.5 grab:a sub:4 wait:2 grab:b pad: wait:2" RECOMP_D3D_BRIDGE_INTERP_T0=1 > runs/log_$T.txt 2>&1
done
echo s1 done
