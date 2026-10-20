# tm.sh PFX ROUNDS "CFG..." -- step 95's timing: mm.sh's interleaved runs (cast_bug's start, avalanch's, upstairs;
# standing; render thread on; native 4K at 240 Hz; Frame rate: Display; adaptive on), 45 s in each level, read by
# tsum.py from the 10-s lines (plain builds measure the same way as the PGO one). Then tsum.py PFX.
cd $(dirname $(readlink -f $0))
P=$1; R=$2; shift 2
STEPS="wait:45" bash mm.sh $P $R "$@"
python tsum.py $P
