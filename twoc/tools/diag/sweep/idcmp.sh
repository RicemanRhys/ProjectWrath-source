# idcmp.sh A B -- compares id.sh's runs of A and B
M=/c/Users/User/AppData/Local/Temp/claude/C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp/7f3c0c0f-65ce-4410-8df9-e214ed7ded3f/scratchpad
R="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp"
PY="$R/.venv/Scripts/python.exe"
A=$1; B=$2
cd $M/id
hostlines() { grep -aE 'host device|host render target [0-9]+ for|host renders|pacing|frame rate plan|host interpolation (on|off)' "$1" | sed 's/window [0-9A-F]*/window X/'; }
for m in off ho; do
  echo "=== $m"
  "$PY" "$R/twoc/tools/diag/compare_runs.py" ${m}_$A.err.log ${m}_$B.err.log | tail -6
  for v in $A $B; do
    f=${m}_$v.err.log
    echo "$v: PATH $(grep -ac '\[PATH\]' $f) CdRom $(grep -aci 'cdrom0' $f) CRASH $(grep -ac '\[CRASH\]' $f) CFG/OVL/WIDE/AUDIO/XA2-5.1 $(grep -acE '\[(CFG|OVL|WIDE|AUDIO)\]|\[XA2\] 5\.1' $f) exit: $(tail -1 ${m}_$v.drive.txt)"
    grep -a '\[XA2\]\|\[APU\] Using' $f | head -3
  done
  diff <(grep -a '\[PATH\]' ${m}_$A.err.log) <(grep -a '\[PATH\]' ${m}_$B.err.log) > /dev/null && echo "PATH lines identical" || echo "PATH lines DIFFER"
  diff <(grep -ai 'open.*cdrom0' ${m}_$A.err.log | sed 's/^.*cdrom0/cdrom0/I') <(grep -ai 'open.*cdrom0' ${m}_$B.err.log | sed 's/^.*cdrom0/cdrom0/I') > /dev/null && echo "CdRom opens identical in order ($(grep -aci 'open.*cdrom0' ${m}_$A.err.log))" || echo "CdRom opens DIFFER"
  diff <(hostlines ${m}_$A.err.log) <(hostlines ${m}_$B.err.log) > /dev/null && echo "host device/target lines identical ($(hostlines ${m}_$A.err.log | wc -l))" || diff <(hostlines ${m}_$A.err.log) <(hostlines ${m}_$B.err.log) | head -6
done
echo "=== dumps"
n=0; for f in $(ls dump_${A}_ho | grep '^host' | sort | head -8); do cmp -s dump_${A}_ho/$f dump_${B}_ho/$f && n=$((n+1)) || echo "differs: $f"; done; echo "$n of 8 first host dumps byte-identical"; ls dump_${A}_ho | wc -l; ls dump_${B}_ho | wc -l
