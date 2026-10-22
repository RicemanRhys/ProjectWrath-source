# tx.sh TAG EXEDIR LEVEL X Y Z [ENV...] -- step 100 item 5: the level by pad 1:1, Crash put at X Y Z, then glided
# along z (Z-2 .. Z+2, 3 units/s) for 14 s while 4 swaps' sub-frames are dumped (runs/TAG/hostsub*), and a grab.
T=$1; E=$2; L=$3; X=$4; Y=$5; Z=$6; shift 6
Z0=$(python -c "print($Z-2)"); Z1=$(python -c "print($Z+2)")
KEEP=1 LV=$L RP=1:1 bash dd.sh $T $E "wait:3 x:setpos.py:$X:$Y:$Z:2 wait:3 grab:still bg:glide2.py:$T:14:$Z0:$Z1:3 wait:6 sub:4 wait:2 grab:glide wait:7" "$@"
