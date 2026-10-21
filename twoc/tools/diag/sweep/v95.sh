# v95.sh PFX EXE CHK [PARTS] -- step 95's checks of one cut, exe folders EXE (the cut's plain build) and CHK (the
# cut + ../step87_bake_check.patch). THREADCHECK on throughout.
#   rc: REPLAY_CHECK with 2 passes (the second baked), render thread sync: cast_bug gliding, avalanch, upstairs
#   sf: the sub-frame check (TMP_ICHECK=1: every baked pass drawn the ordinary way first and compared), 4 sub-frames,
#       adaptive off, render thread on: the same three places
#   h2: both in H2 Oh No with its pause still
# One summary to runs/PFX_<part>.sum (sx.sh deletes runs/TAG.*: the summaries are named apart).
cd $(dirname $(readlink -f $0))
P=$1; E=$2; C=$3; shift 3
W() { cygpath -w $(pwd)/$1/crash_twoc.exe; }
TC=RECOMP_D3D_BRIDGE_THREADCHECK=1
PZ="pad:buttons=0x10 wait:0.15 pad: wait:4 grab:pause wait:4 grab:pause2 pad:buttons=0x10 wait:0.15 pad: wait:4 grab:back"
cb() { WAIT=30 bash sx.sh $1 u 1 "wait:5 x:glide.py:$1:40:-3:7:3" --exe=$(W $2) RECOMP_HUD=edges "${@:3}" > /dev/null 2>&1; }
av() { WAIT=30 bash sx.sh $1 5 4:1 "wait:40" --exe=$(W $2) RECOMP_HUD=edges "${@:3}" > /dev/null 2>&1; }
up() { PRE="upst.py $1" WAIT=3 bash sx.sh $1 - - "wait:40" --exe=$(W $2) RECOMP_HUD=edges "${@:3}" > /dev/null 2>&1; }
h2() { WAIT=22 bash sx.sh $1 2 1:1 "wait:3 grab:lvl wait:6 grab:lvl2 $PZ" --exe=$(W $2) RECOMP_HUD=edges "${@:3}" > /dev/null 2>&1; }
sum() {  # TAG
  local L=runs/$1.err.log
  echo "-- $1: $(grep -a 'Crashdat.levels' $L | tail -1 | sed 's/.*levels.//')"
  grep -a "replay check: [0-9]* frames" $L | tail -1
  echo "   'differs in' lines: $(grep -ac 'differs in' $L); bad lines: $(grep -a 'D3DB\] THREADCHECK:\|\[CRASH\]\|frame slots\|watchdog' $L | grep -avc '^\[MEM\]')"
  grep -a "host baked draws: [0-9]" $L | tail -1
  grep -a "host recorded constants" $L | tail -1
  grep -a "interpolation sub-frames" $L | tail -1 | cut -c1-200
}
for part in ${*:-rc sf}; do
  case $part in
    rc) for p in cb av up; do $p ${P}r_$p $E RECOMP_D3D_BRIDGE_REPLAY_CHECK=1 RECOMP_D3D_BRIDGE_REPLAY=2 RECOMP_RENDER_THREAD=sync $TC; done
        { for p in cb av up; do sum ${P}r_$p; done; } > runs/${P}_rc.sum ;;
    sf) for p in cb av up; do $p ${P}s_$p $C TMP_ICHECK=${IC:-1} RECOMP_FRAMERATE_ADAPTIVE=0 RECOMP_RENDER_THREAD=on $TC; done
        { for p in cb av up; do sum ${P}s_$p; done; } > runs/${P}_sf.sum ;;
    h2) h2 ${P}r_h2 $E RECOMP_D3D_BRIDGE_REPLAY_CHECK=1 RECOMP_D3D_BRIDGE_REPLAY=2 RECOMP_RENDER_THREAD=sync $TC
        h2 ${P}s_h2 $C TMP_ICHECK=${IC:-1} RECOMP_FRAMERATE_ADAPTIVE=0 RECOMP_RENDER_THREAD=on $TC
        { sum ${P}r_h2; sum ${P}s_h2; } > runs/${P}_h2.sum ;;
  esac
  echo "PART $part done"
done
echo ALLDONE
