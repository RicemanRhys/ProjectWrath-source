# ph.sh TAG EXEDIR LEVEL ROOM:PAD [ENV...] -- step 104 Part 7: photo mode in a level (LEVEL - : the hub), windowed
# 1280x720 (RS), PASS B (Game (60), 4:3) unless ENV says otherwise: grabs before, photo on, flown back and up,
# turned, frozen and flown, unfrozen, photo off (runs/TAG_*.png).
S=$(dirname $(readlink -f $0)); cd $S
T=$1; X=$(cygpath -w $(readlink -f $2)/crash_twoc.exe); L=$3; RP=$4; shift 4
ACT_KEEP_LIVES=1 PASS=B RS=${RS:-1280x720} WAIT=${WAIT:-25} SECS=${SECS:-300} bash sx.sh $T $L $RP \
  "${ST:-wait:3 grab:g0 x:kk.py:tap:f3 wait:1 grab:p0 x:kk.py:tap:s:1.0 x:kk.py:tap:e:0.6 wait:0.3 grab:p1 x:kk.py:tap:left:0.6 wait:0.3 grab:p2 x:kk.py:tap:f2 wait:1.5 grab:f0 x:kk.py:tap:up:0.5 x:kk.py:tap:w:0.8 wait:0.3 grab:f1 x:kk.py:tap:f2 wait:1.5 grab:r0 x:kk.py:tap:f3 wait:1 grab:off}" --exe=$X "$@"
