# sv.sh -- step 84: the vertex cache's sweep. The 25 main levels (main.txt), each loaded from its pad and stood in
# 40 s, with HCK (released blocks never freed, a replayed draw from one counted) and RECOMP_D3D_BRIDGE_VCACHE=check
# (every program draw also converted the old way and compared): Frame rate: Display at 16:9, the render thread on,
# THREADCHECK. One line a level to runs/sv.txt.
cd $(dirname $(readlink -f $0))
X=$(cygpath -w $(pwd)/HCK/crash_twoc.exe)
: > runs/sv.txt
while read L RP; do
  [ -z "$L" ] && continue
  T=sv$(printf %02d $L)
  WAIT=25 bash sx.sh $T $L $RP "wait:40" --exe=$X RECOMP_HUD=edges RECOMP_RENDER_THREAD=on RECOMP_D3D_BRIDGE_VCACHE=check \
    RECOMP_D3D_BRIDGE_THREADCHECK=1 < /dev/null > /dev/null 2>&1
  lv=$(grep -a 'levels\\' runs/$T.err.log | tail -1 | sed 's/.*levels\\//')
  vc=$(grep -a "host vertex cache:" runs/$T.err.log | tail -1 | sed 's/.*cache: //')
  hd=$(grep -a "TMP\] hold check" runs/$T.err.log | tail -1 | sed 's/.*check: //')
  bad=$(grep -ac "D3DB\] THREADCHECK:\|CRASH\|vertex cache CHECK\|block was released" runs/$T.err.log)
  echo "$T $RP $lv | $vc | $hd | bad lines $bad" >> runs/sv.txt
done < main.txt
echo ALLDONE >> runs/sv.txt
