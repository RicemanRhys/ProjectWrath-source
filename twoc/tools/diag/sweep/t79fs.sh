# t79fs.sh TAG EXEDIR [ENV...] -- step 79: the window under the render thread: Alt+Enter to borderless fullscreen and
# back twice (the swap chain resized on the thread), then minimised and restored; upstairs, Frame rate: Display,
# the thread on, THREADCHECK; grabs and the log's lines.
cd $(dirname $(readlink -f $0))
T=$1; X=$(cygpath -w $2)/crash_twoc.exe; shift 2
A="wait:6 grab:a x:winkeys.py:altenter wait:4 grab:fs1 x:winkeys.py:altenter wait:4 grab:win1 x:winkeys.py:altenter wait:4"
A="$A grab:fs2 x:winkeys.py:altenter wait:4 grab:win2 x:minwin.py:min wait:4 x:minwin.py:restore wait:4 grab:restored"
PRE="upst.py $T" WAIT=3 bash sx.sh $T - - "$A" --exe=$X RECOMP_HUD=edges RECOMP_RENDER_THREAD=on RECOMP_D3D_BRIDGE_THREADCHECK=1 "$@" 2>&1 | tail -1
grep -aE "host window|swap chain|Present returned|render thread: o|THREADCHECK:|watchdog|frame slots|CRASH" runs/$T.err.log | head -20
grep -a "interpolation sub-frames" runs/$T.err.log | tail -2 | cut -c1-200
