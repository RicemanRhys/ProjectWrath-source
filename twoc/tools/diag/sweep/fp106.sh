# fp106.sh EXEDIR -- step 106: the automatic rolling save's false positives in ordinary play: the hub and five levels
# (Arctic Antics 7, Drain Damage 23, Gold Rush 19, Medieval Madness 16, Crashteroids 26) with play.py 90 s each,
# Match display 16:9 1080p (PASS=1), the rolling capture with AUTO=50 (pictures only); then Smokey drives (dr3.sh x3).
# Prints each run's flags (guest swap > 3000).
cd $(dirname $(readlink -f $0))
E=$1; X=$(cygpath -w $(pwd)/$E/crash_twoc.exe)
for L in hub 7 23 19 16 26; do
  T=fp${FPP:-}_$L; rm -rf runs/$T; mkdir -p runs/$T/roll; D=$(cygpath -m $(pwd)/runs/$T)
  if [ $L = hub ]; then LV=-; RP=-; else LV=$L; RP=1:1; fi
  PASS=1 RS=1920x1080 WAIT=25 bash sx.sh $T $LV $RP "wait:2 x:play.py:$T:90" --exe=$X RECOMP_HUD=edges \
    RECOMP_D3D_BRIDGE_ROLLING=$D/roll RECOMP_D3D_BRIDGE_ROLLING_AUTO=50 RECOMP_D3D_BRIDGE_ANOMALY=1 > /dev/null 2>&1
  echo "$T: $(grep -a '\[ROLL\] flagged' runs/$T.err.log | grep -o 'guest swap [0-9]* ([0-9 of]*)[^,]*' | tr '\n' ';') | $(grep -a 'summary' runs/$T.act.txt | tail -1 | cut -c1-80)"
done
for i in 1 2 3; do FR=display AUTO=50 FULL=0 bash dr3.sh fp_d$i $E > /dev/null 2>&1; echo "fp_d$i: $(grep -a '\[ROLL\] flagged' runs/fp_d$i.err.log | grep -o 'guest swap [0-9]* ([0-9 of]*)' | tr '\n' ';')"; done
echo fp106 done
