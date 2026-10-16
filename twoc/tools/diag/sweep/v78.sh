# v78.sh: step 78's verification with the final exe (F), its sub-frame check build (ICF) and the exe before (B).
# One chain; each part's lines go to runs/v78.<part>.txt.
cd $(dirname $(readlink -f $0))
TC=RECOMP_D3D_BRIDGE_THREADCHECK=1
W() { cygpath -w $(pwd)/$1/crash_twoc.exe; }
PZ="pad:buttons=0x10 wait:0.15 pad: wait:4 grab:pause wait:4 grab:pause2"
h2() {   # TAG EXE ENV...: H2 Oh No (level 2), grabs, then the pause still
  WAIT=22 bash sx.sh $1 2 1:1 "wait:3 grab:lvl wait:6 grab:lvl2 $PZ" --exe=$(W $2) RECOMP_HUD=edges "${@:3}" 2>&1 | tail -1
}
up() { PRE="upst.py $1" WAIT=3 bash sx.sh $1 - - "wait:35" --exe=$(W $2) RECOMP_HUD=edges "${@:3}" 2>&1 | tail -1; }

{ echo "== the sub-frame check (ICF): cast_bug gliding, avalanch, upstairs"
  bash ichk.sh vic $(pwd)/ICF
  echo "== H2 Oh No and its pause still: the sub-frame check, REPLAY_CHECK"
  h2 vh2i ICF TMP_ICHECK=1 RECOMP_FRAMERATE_ADAPTIVE=0
  h2 vh2c F RECOMP_D3D_BRIDGE_REPLAY_CHECK=1 $TC
  for t in vh2i vh2c; do grep -a "replay check" runs/$t.err.log | tail -1; grep -ac "differs in" runs/$t.err.log; grep -a "THREADCHECK:\|frame slots\|CRASH" runs/$t.err.log | head -3; done
} > runs/v78.checks.txt 2>&1

{ echo "== H2 Oh No at 240 with MSAA 4x; avalanch FXAA; upstairs SMAA (THREADCHECK)"
  h2 vh2m F RECOMP_FRAMERATE=display RECOMP_ANTIALIASING=msaa4x $TC
  WAIT=22 bash sx.sh vavf 5 4:1 "wait:5 grab:a wait:10 grab:b" --exe=$(W F) RECOMP_HUD=edges RECOMP_ANTIALIASING=fxaa $TC 2>&1 | tail -1
  up vups F RECOMP_ANTIALIASING=smaa $TC
  for t in vh2m vavf vups; do grep -a "anti-aliasing:\|THREADCHECK:\|frame slots\|CRASH" runs/$t.err.log | tail -2; grep -a "interpolation sub-frames" runs/$t.err.log | tail -1; done
} > runs/v78.aa.txt 2>&1

{ echo "== frame rates upstairs standing: display (240), 120, 90 (80 here), before (B) and after (F)"
  for fr in display 120 90; do
    up vB$fr B RECOMP_FRAMERATE=$fr
    up vF$fr F RECOMP_FRAMERATE=$fr $TC
  done
  for fr in display 120 90; do for e in B F; do t=v$e$fr; echo "-- $t"; grep -a "frame rate plan:" runs/$t.err.log | tail -1 | cut -c1-160; grep -a "frames shown\|displayed" runs/$t.err.log | tail -2; grep -a "interpolation sub-frames" runs/$t.err.log | tail -1; done; done
} > runs/v78.rates.txt 2>&1

{ echo "== identity: drive.py 150 s, bridge off and host-only, B against F"
  bash id.sh B F; bash idcmp.sh B F
} > runs/v78.id.txt 2>&1

{ echo "== cost"; bash m78.sh m; } > runs/v78.cost.txt 2>&1
echo ALLDONE
