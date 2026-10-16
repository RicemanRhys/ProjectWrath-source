# v79a.sh -- step 79 verification, part A, with the final exe (F) and its check builds (ICF, WDT); one chain, each
# part's lines to runs/v79a.<part>.txt.
cd $(dirname $(readlink -f $0))
TC=RECOMP_D3D_BRIDGE_THREADCHECK=1
W() { cygpath -w $(pwd)/$1/crash_twoc.exe; }
PZ="pad:buttons=0x10 wait:0.15 pad: wait:4 grab:pause wait:4 grab:pause2 pad:buttons=0x10 wait:0.15 pad: wait:4 grab:back"
h2() {   # TAG EXE ENV...: H2 Oh No (level 2), grabs, then the game's pause (its still) and back
  WAIT=22 bash sx.sh $1 2 1:1 "wait:3 grab:lvl wait:6 grab:lvl2 $PZ" --exe=$(W $2) RECOMP_HUD=edges "${@:3}" 2>&1 | tail -1
}
lines() { grep -a "render thread: on from\|render thread: off from\|THREADCHECK:\|frame slots\|watchdog\|CRASH" runs/$1.err.log | head -6; }

{ echo "== REPLAY_CHECK, the render thread in sync (the game waits for each frame), THREADCHECK: cast_bug gliding, avalanch, upstairs"
  bash chk.sh vck $(pwd)/F RECOMP_RENDER_THREAD=sync $TC
  for t in cb av up; do lines vck_$t; done
  echo "== the sub-frame check (ICF), the render thread in sync, 4 sub-frames: cast_bug gliding, avalanch, upstairs"
  bash ichk.sh vic $(pwd)/ICF RECOMP_RENDER_THREAD=sync
  for t in cb av up; do lines vic_$t; grep -ac "differs in" runs/vic_$t.err.log; done
  echo "== H2 Oh No and the game's pause (its still): REPLAY_CHECK sync + THREADCHECK; the sub-frame check sync"
  h2 vh2c F RECOMP_D3D_BRIDGE_REPLAY_CHECK=1 RECOMP_RENDER_THREAD=sync $TC
  h2 vh2i ICF TMP_ICHECK=1 RECOMP_FRAMERATE_ADAPTIVE=0 RECOMP_RENDER_THREAD=sync
  for t in vh2c vh2i; do echo "-- $t"; grep -a "replay check" runs/$t.err.log | tail -1; grep -ac "differs in" runs/$t.err.log; lines $t; done
} > runs/v79a.checks.txt 2>&1

{ echo "== H2 Oh No, the render thread on (not sync), MSAA 4x, the game's pause, THREADCHECK"
  h2 vh2t F RECOMP_RENDER_THREAD=on RECOMP_ANTIALIASING=msaa4x $TC
  lines vh2t; grep -a "interpolation sub-frames\|host render thread: on;" runs/vh2t.err.log | tail -2
  grep -a "texture uploads" runs/vh2t.err.log | tail -1
} > runs/v79a.pause.txt 2>&1

{ echo "== the watchdog: the render thread stalled 7 s after taking a frame upstairs (WDT, TMP_RT_STALL), then in a pass"
  PRE="upst.py vwd1" WAIT=3 bash sx.sh vwd1 - - "wait:20" --exe=$(W WDT) RECOMP_HUD=edges RECOMP_RENDER_THREAD=on TMP_RT_STALL=2900 $TC 2>&1 | tail -1
  grep -a "TMP\]\|watchdog" runs/vwd1.err.log | head -12
  PRE="upst.py vwd2" WAIT=3 bash sx.sh vwd2 - - "wait:20" --exe=$(W WDT) RECOMP_HUD=edges RECOMP_RENDER_THREAD=on TMP_RT_STALL_PASS=2900 $TC 2>&1 | tail -1
  grep -a "TMP\]\|watchdog" runs/vwd2.err.log | head -12
} > runs/v79a.watchdog.txt 2>&1

{ echo "== quitting while rendering: the run's own end (RECOMP_WATCHDOG_SECS, exit code 3) upstairs with the thread, 3 times"
  for i in 1 2 3; do
    SECS=70 PRE="upst.py vq$i" WAIT=3 bash sx.sh vq$i - - "wait:60" --exe=$(W F) RECOMP_HUD=edges RECOMP_RENDER_THREAD=on $TC 2>&1 | tail -1
    cat runs/vq$i.launch.txt | tail -1; lines vq$i
  done
  echo "== closing the window while rendering (WM_CLOSE: exit code 0), 2 times"
  for i in 1 2; do
    PRE="upst.py vc$i" WAIT=3 bash sx.sh vc$i - - "wait:8 x:wmclose.py wait:5" --exe=$(W F) RECOMP_HUD=edges RECOMP_RENDER_THREAD=on $TC 2>&1 | tail -1
    cat runs/vc$i.launch.txt | tail -1; grep -a "host window closed" runs/vc$i.err.log; lines vc$i
  done
} > runs/v79a.quit.txt 2>&1
echo ALLDONE
