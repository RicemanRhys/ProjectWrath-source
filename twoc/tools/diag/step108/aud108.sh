# aud108.sh TAG EXEDIR title|demo|hub|level [ENV=VAL...] -- step 108: the APU's own output (RECOMP_APU_OUT_WAV ->
# runs/TAG.apu.wav) and the loopback (loopcap -> runs/TAG.loop.wav) for one scenario:
#   title  the slow title (Cortex's lines, LOGOS.wav) into the main menu, 75 s
#   demo   the fast title, the menu idles into the attract demo (Arctic Antics), 110 s
#   hub    a new game (WOMBAT), the intro skipped, 45 s standing in the hub
#   level  LV (default 20 Droid Void) through gopad2 (RP, default 1:1), 45 s standing
S=$(dirname $(readlink -f $0)); cd $S
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
LC="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/twoc/tools/diag/loopcap.exe"
T=$1; E=$2; SC=$3; shift 3
X="--exe=$(cygpath -w $(readlink -f $E)/crash_twoc.exe)"
W=$(cygpath -w $S/runs/$T.apu.wav); L=$(cygpath -w $S/runs/$T.loop.wav)
rm -f runs/$T.*
case $SC in
  title) "$LC" "$L" 72 --wait-proc=crash_twoc.exe > runs/$T.lc.txt 2>&1 &
         "$PY" launch.py $T 75 --nofast "$X" RECOMP_APU_OUT_WAV="$W" "$@" > runs/$T.launch.txt 2>&1 ;;
  demo)  "$LC" "$L" 108 --wait-proc=crash_twoc.exe > runs/$T.lc.txt 2>&1 &
         "$PY" launch.py $T 110 "$X" RECOMP_APU_OUT_WAV="$W" "$@" > runs/$T.launch.txt 2>&1 ;;
  hub|level)
         "$PY" launch.py $T 400 "$X" RECOMP_APU_OUT_WAV="$W" "$@" > runs/$T.launch.txt 2>&1 &
         sleep 2
         "$PY" newgame2.py $T > runs/$T.ng.txt 2>&1
         if [ $SC = level ]; then "$PY" gopad2.py $T ${LV:-20} ${RP:-1:1} > runs/$T.gp.txt 2>&1; sleep ${WAIT:-12}; fi
         "$LC" "$L" 45 > runs/$T.lc.txt 2>&1
         taskkill //IM crash_twoc.exe //F > /dev/null 2>&1
         wait ;;
esac
wait
grep -h "APU-OUT\|\[APU\]\|\[SND\]\|exit" runs/$T.err.log runs/$T.launch.txt | tail -12
