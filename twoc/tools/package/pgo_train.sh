# pgo_train.sh HARNESS EXE PGC_DIR PGOSWEEP_EXE -- step 95 (notes 18.98): make_package.ps1 -Pgo's training run.
# HARNESS: a scratch copy of twoc/tools/diag/sweep with root/game_files (default.xbe, a Crashdat junction, an empty
# UDATA) that make_package.ps1 made; EXE: the instrumented crash_twoc.exe (pgort140.dll beside it); PGC_DIR: its .pgd's
# folder, where the counts go (crash_twoc!N.pgc, swept by pgsw2.py while the game runs, as it ends with
# TerminateProcess). Three runs, as the measurements stand (notes 18.87, 18.90): cast_bug's start with the player
# gliding, avalanch's start, the upstairs room; Frame rate: Display, the render thread on, the HUD at the edges,
# 16:9, native size; each 40 s in the level after a new game and the walk there (the menu, intro, hub on the way).
set -e
H=$1; X=$2
export TWOC_PGC_DIR="$3" TWOC_PGOSWEEP="$4"    # (not PGOSWEEP: pgosweep.exe reads that as options, as cl reads CL)
cd "$H"
E="--exe=$X RECOMP_HUD=edges RECOMP_RENDER_THREAD=on"
export RS=native
WAIT=30 bash sx.sh pgt_cb u 1 "wait:5 x:glide.py:pgt_cb:40:-3:7:3 x:pgsw2.py:1" $E 2>&1 | tail -1
WAIT=30 bash sx.sh pgt_av 5 4:1 "wait:40 x:pgsw2.py:2" $E 2>&1 | tail -1
PRE="upst.py pgt_up" WAIT=3 bash sx.sh pgt_up - - "wait:40 x:pgsw2.py:3" $E 2>&1 | tail -1
grep -h "levels" runs/pgt_cb.err.log runs/pgt_av.err.log runs/pgt_up.err.log 2>/dev/null | grep -a "PATH" | sed 's/.*levels.//' | sort -u
cat runs/pgt_*.act.txt | grep -a "pgsw2" || true
n=$(ls "$(cygpath -u "$TWOC_PGC_DIR")" | grep -c "\.pgc$" || true)
echo "$n count files in $TWOC_PGC_DIR"
[ "$n" -ge 3 ]
