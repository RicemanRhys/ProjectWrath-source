# furbatch.sh -- step 102 part 4: every level's start with two draw logs (WOMBAT, 4:3 Game 60), for fursum.py
cd $(dirname $(readlink -f $0))
for L in ${LEVELS:-0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 36 39 40 41 43}; do
  LV=$L RP=1:1 WAIT=25 SECS=300 bash fx.sh fb$L "wait:1 dl:1 wait:12 dl:1 wait:3" > runs/log_fb$L.txt 2>&1
done
echo batch done
