# sv87.sh -- step 87: the bake's sweep in check mode. The 25 main levels (main.txt), each loaded from its pad and
# stood in 40 s, with BC (../step87_bake_check.patch: every baked pass drawn the ordinary way first and compared,
# TMP_ICHECK=1), the bake on, Frame rate: Display at 16:9, 4 sub-frames (adaptive off), the render thread on,
# THREADCHECK. One line a level to runs/sv87.txt.
cd $(dirname $(readlink -f $0))
X=$(cygpath -w $(pwd)/BC/crash_twoc.exe)
: > runs/sv87.txt
while read L RP; do
  [ -z "$L" ] && continue
  T=sv87_$(printf %02d $L)
  WAIT=25 bash sx.sh $T $L $RP "wait:40" --exe=$X RECOMP_HUD=edges RECOMP_RENDER_THREAD=on RECOMP_D3D_BRIDGE_BAKE=1 \
    TMP_ICHECK=1 RECOMP_FRAMERATE_ADAPTIVE=0 RECOMP_D3D_BRIDGE_THREADCHECK=1 < /dev/null > /dev/null 2>&1
  lv=$(grep -a 'levels\\' runs/$T.err.log | tail -1 | sed 's/.*levels\\//')
  rc=$(grep -a "replay check: [0-9]* frames" runs/$T.err.log | tail -1 | sed 's/.*check: //')
  bk=$(grep -a "host baked draws: [0-9]" runs/$T.err.log | tail -1 | sed 's/.*draws: //')
  df=$(grep -ac "differs in" runs/$T.err.log)
  bad=$(grep -ac "D3DB\] THREADCHECK:\|CRASH\|frame slots\|watchdog" runs/$T.err.log)
  echo "$T $RP $lv | $rc | differs lines $df | bad lines $bad | $bk" >> runs/sv87.txt
done < main.txt
echo ALLDONE >> runs/sv87.txt
