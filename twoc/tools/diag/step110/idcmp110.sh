# idcmp110.sh M A B -- compares id110.sh's runs of A and B (sweep/idcmp.sh's checks, in M/s/id)
M=$1; A=$2; B=$3
R="$(cd "$(dirname "$(readlink -f "$0")")/../../../.." && pwd)"
cd "$M/s/id"
hostlines() { grep -aE 'host device|host render target [0-9]+ for|host renders|pacing|frame rate plan|host interpolation (on|off)' "$1" | sed 's/window [0-9A-F]*/window X/; s/dump_[A-Za-z0-9]*_ho/dump_X_ho/g'; }
for m in off ho; do
  echo "=== $m"
  python "$R/twoc/tools/diag/compare_runs.py" ${m}_$A.err.log ${m}_$B.err.log | grep -a "distinct CdRom\|file-open sequence\|last file"
  for v in $A $B; do
    f=${m}_$v.err.log
    echo "$v: PATH $(grep -ac '\[PATH\]' $f) CdRom $(grep -aci 'open.*cdrom0' $f) CRASH $(grep -ac '\[CRASH\]' $f) exit: $(tail -1 ${m}_$v.drive.txt)"
    grep -a '\[SAVES\]' $f | cut -c1-160
  done
  diff <(grep -a '\[PATH\]' ${m}_$A.err.log) <(grep -a '\[PATH\]' ${m}_$B.err.log) > /dev/null && echo "PATH lines identical" || { echo "PATH lines DIFFER"; diff <(grep -a '\[PATH\]' ${m}_$A.err.log) <(grep -a '\[PATH\]' ${m}_$B.err.log) | head -10; }
  diff <(grep -ai 'open.*cdrom0' ${m}_$A.err.log | sed 's/^.*cdrom0/cdrom0/I') <(grep -ai 'open.*cdrom0' ${m}_$B.err.log | sed 's/^.*cdrom0/cdrom0/I') > /dev/null && echo "CdRom opens identical in order ($(grep -aci 'open.*cdrom0' ${m}_$A.err.log))" || echo "CdRom opens DIFFER"
  diff <(hostlines ${m}_$A.err.log) <(hostlines ${m}_$B.err.log) > /dev/null && echo "host device/target lines identical ($(hostlines ${m}_$A.err.log | wc -l))" || diff <(hostlines ${m}_$A.err.log) <(hostlines ${m}_$B.err.log) | head -6
done
echo "=== dumps"
n=0; for f in $(ls dump_${A}_ho | grep '^host' | sort | head -8); do cmp -s dump_${A}_ho/$f dump_${B}_ho/$f && n=$((n+1)) || echo "differs: $f"; done; echo "$n of 8 first host dumps byte-identical"; ls dump_${A}_ho | wc -l; ls dump_${B}_ho | wc -l
