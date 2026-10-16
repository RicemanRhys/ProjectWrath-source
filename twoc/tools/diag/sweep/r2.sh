# r2.sh -- step 82 items 2-3: the frame-rate counter and the overlay's Measured line in cast_bug (2560x1440 so the
# overlay fits), render thread on: as it is, with the game side 12 ms slower (slow), and at Game (60); the power line.
cd $(dirname $(readlink -f $0))
F=--exe=$(cygpath -w $(pwd)/F/crash_twoc.exe)
TH="RECOMP_RENDER_THREAD=on RECOMP_SHOW_FPS=1"
A="wait:12 grab:fps x:esc.py wait:2 grab:ovl x:esc.py wait:3 grab:after"
export RS=2560x1440
STEPS="$A" bash m79.sh c_fps cb $F $TH
STEPS="$A" bash m79.sh c_slow cb $F $TH RECOMP_DEV_GAME_BURN_MS=12
STEPS="$A" PASS=2 bash m79.sh c_g60 cb $F $TH
for t in c_fps c_slow c_g60; do echo "== $t"; grep -aE "frame-rate counter|host power|\[OVL\]" runs/$t.err.log | sort | uniq -c | sort -rn | head -6; done > runs/r2.txt
echo r2 finished
