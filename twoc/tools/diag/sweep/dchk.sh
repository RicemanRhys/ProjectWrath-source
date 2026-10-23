# dchk.sh EXEDIR BIAS [LEVELS] -- step 102 part 2: the decal bias's own effect: Crash standing (the camera still), 3 swaps
# of sub-frame dumps with the bias 0, then 3 with BIAS (trigger-file "set idecal"); dsub.py compares them.
cd $(dirname $(readlink -f $0))
E=$1; B=$2
for L in ${3:-27 34 12 7 19 15 11 5}; do T=dc$L
  ST="wait:8 x:st.py:$T:idecal_0 wait:1 sub:3 wait:5 x:st.py:$T:idecal_$B wait:1 sub:3 wait:5"
  KEEP=1 LV=$L RP=1:1 WAIT=20 bash dd.sh $T $E "$ST" > runs/log_$T.txt 2>&1
done
echo dchk done
