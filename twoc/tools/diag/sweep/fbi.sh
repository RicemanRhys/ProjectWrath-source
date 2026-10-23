# fbi.sh -- step 102 part 4: the new game's intro cutscene with a draw-log swap every 6 s (for fursum.py)
cd $(dirname $(readlink -f $0))
mkdir -p runs/fbi
( sleep 45; for i in $(seq 1 40); do echo 1 > runs/fbi/dl.trig; sleep 6; done ) &
INTRO_GRABS=10,60,120,180,240 LV=- SECS=420 bash fx.sh fbi "wait:1 dl:1 wait:3" > runs/log_fbi.txt 2>&1
wait
echo fbi done
