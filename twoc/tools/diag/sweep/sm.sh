# sm.sh PFX EXE... -- step 103: smoothness (the step 62/66/67 checks), Match display 240 Hz, render thread on, 1280x720:
# Banzai Bonsai (garden, 12): Coco standing at the start spinning twice with the gong enemy behind her (step 67's
# spin), 60 swaps of sub-frames + 60 INTERP_REC frames; Arctic Antics (7): Crash running, jumping and spinning
# (30 swaps of sub-frames + 30 INTERP_REC frames).
cd $(dirname $(readlink -f $0))
P=$1; shift
for E in "$@"; do
  T=${P}bz_$E
  RS=1280x720 KEEP=1 LV=12 RP=1:1 bash dd.sh $T $E "wait:2 rec:60 sub:60 wait:0.3 pad:x=255 wait:0.15 pad: wait:0.6 pad:x=255 wait:0.15 pad: wait:4" > runs/log_$T.txt 2>&1
  T=${P}aa_$E
  RS=1280x720 KEEP=1 LV=7 RP=1:1 bash dd.sh $T $E "wait:2 pad:ly=32767 wait:1 rec:30 sub:30 wait:0.2 pad:ly=32767_a=255 wait:0.2 pad:ly=32767 wait:0.3 pad:ly=32767_x=255 wait:0.15 pad:lx=20000_ly=20000 wait:2 pad: wait:2" > runs/log_$T.txt 2>&1
done
echo sm done
