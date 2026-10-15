# id.sh A B -- drive.py 150 s, bridge off and host-only, with scratch/A/crash_twoc.exe and scratch/B/crash_twoc.exe
# (copies: no crash_twoc.ini next to them). Logs and host dumps in scratch/id.
M=/c/Users/User/AppData/Local/Temp/claude/C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp/7f3c0c0f-65ce-4410-8df9-e214ed7ded3f/scratchpad
mkdir -p $M/id; cd $M/id
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
D="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/twoc/tools/diag/drive.py"
for v in "$@"; do
  rm -rf dump_${v}_ho; mkdir -p dump_${v}_ho
  "$PY" "$D" off_$v 150 --exe="$(cygpath -w $M/sw/$v/crash_twoc.exe)" --outdir=. > off_$v.drive.txt 2>&1
  "$PY" "$D" ho_$v 150 --host-only --dump="$(cygpath -w $M/id/dump_${v}_ho)" --exe="$(cygpath -w $M/sw/$v/crash_twoc.exe)" --outdir=. > ho_$v.drive.txt 2>&1
done
echo all done
