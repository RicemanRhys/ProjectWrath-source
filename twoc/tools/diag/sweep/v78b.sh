# v78b.sh: step 78's verification again on the final code (exe F2, its sub-frame check build ICF2, before B).
cd $(dirname $(readlink -f $0))
TC=RECOMP_D3D_BRIDGE_THREADCHECK=1
W() { cygpath -w $(pwd)/$1/crash_twoc.exe; }
PZ="pad:buttons=0x10 wait:0.15 pad: wait:4 grab:pause wait:4 grab:pause2"
h2() { WAIT=22 bash sx.sh $1 2 1:1 "wait:3 grab:lvl wait:6 grab:lvl2 $PZ" --exe=$(W $2) RECOMP_HUD=edges "${@:3}" 2>&1 | tail -1; }
up() { PRE="upst.py $1" WAIT=3 bash sx.sh $1 - - "wait:35" --exe=$(W $2) RECOMP_HUD=edges "${@:3}" 2>&1 | tail -1; }
sum() { for t in "$@"; do echo "-- $t"; grep -a "replay check" runs/$t.err.log | tail -1; echo "differs lines: $(grep -ac 'differs in' runs/$t.err.log)";
        grep -a "THREADCHECK:\|frame slots\|CRASH\|read from guest memory at the replay [1-9]" runs/$t.err.log | head -3; done; }

{ echo "== the sub-frame check (ICF2)"
  bash ichk.sh wic $(pwd)/ICF2 > /dev/null
  h2 wh2i ICF2 TMP_ICHECK=1 RECOMP_FRAMERATE_ADAPTIVE=0
  sum wic_cb wic_av wic_up wh2i
  echo "== REPLAY_CHECK (F2, THREADCHECK)"
  bash chk.sh wc $(pwd)/F2 $TC > /dev/null
  h2 wh2c F2 RECOMP_D3D_BRIDGE_REPLAY_CHECK=1 $TC
  sum wc_cb wc_av wc_up wh2c
} > runs/v78b.checks.txt 2>&1

{ echo "== the HUD at the edges with frames not recorded (Frame rate: Game, 16:9): H2 Oh No"
  h2 wh2g F2 RECOMP_FRAMERATE=60 $TC
  echo "== H2 Oh No 240 MSAA 4x; avalanch FXAA; upstairs SMAA"
  h2 wh2m F2 RECOMP_FRAMERATE=display RECOMP_ANTIALIASING=msaa4x $TC
  WAIT=22 bash sx.sh wavf 5 4:1 "wait:5 grab:a wait:10 grab:b" --exe=$(W F2) RECOMP_HUD=edges RECOMP_ANTIALIASING=fxaa $TC 2>&1 | tail -1
  up wups F2 RECOMP_ANTIALIASING=smaa $TC
  for t in wh2g wh2m wavf wups; do echo "-- $t"; grep -a "anti-aliasing:\|THREADCHECK:\|frame slots\|CRASH\|overlay pass" runs/$t.err.log | tail -2; grep -a "interpolation sub-frames" runs/$t.err.log | tail -1; done
  echo "== live settings, the overlay (THREADCHECK)"
  bash t78s.sh ws $(pwd)/F2 $TC
  bash t78o.sh wo $(pwd)/F2 $TC
} > runs/v78b.feat.txt 2>&1

{ echo "== frame rates upstairs standing"
  for e in B F2 F2 B B F2; do i=$((i+1)); up wr120$e$i $e RECOMP_FRAMERATE=120; done
  up wrdsp F2 RECOMP_FRAMERATE=display $TC
  up wr90 F2 RECOMP_FRAMERATE=90 $TC
  python shown.py 2 runs/wr120*.err.log; python shown.py 1 runs/wrdsp.err.log; python shown.py 3 runs/wr90.err.log
} > runs/v78b.rates.txt 2>&1

{ echo "== identity B F2"; bash id.sh B F2; bash idcmp.sh B F2; } > runs/v78b.id.txt 2>&1
echo ALLDONE
