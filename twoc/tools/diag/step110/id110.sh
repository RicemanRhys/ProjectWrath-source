# id110.sh M V... -- step 110 (notes 18.113): the default run's identity, drive110.py 150 s bridge off and host-only
# for each variant V: M/s/V/ holds the exe (crash_twoc.exe or ProjectWrath.exe) and xemu_apu.dll, M/s/V/game_files
# its game files (mkpkg.ps1). Before each run the variant's saves are reset: game_files\UDATA, TDATA, UDATA_backups
# and the data folder M/s/idd/V (RECOMP_DATA_DIR, used by a ProjectWrath.exe of step 110) deleted, so every run
# starts with no save. Logs and host dumps in M/s/id. Compare with idcmp110.sh.
M=$1; shift
S="$M/s"
H=$(dirname "$(readlink -f "$0")")
PY=python
mkdir -p "$S/id" "$S/idd"; cd "$S/id"
for v in "$@"; do
  exe="$S/$v/crash_twoc.exe"; [ -f "$exe" ] || exe="$S/$v/ProjectWrath.exe"
  for m in off ho; do
    rm -rf "$S/$v/game_files/UDATA" "$S/$v/game_files/TDATA" "$S/$v/game_files/UDATA_backups" "$S/idd/$v"
    if [ $m = off ]; then
      "$PY" "$H/drive110.py" off_$v 150 --exe="$(cygpath -w "$exe")" --root="$(cygpath -w "$S/$v")" --outdir=. \
        "RECOMP_DATA_DIR=$(cygpath -w "$S/idd/$v")" > off_$v.drive.txt 2>&1
    else
      rm -rf dump_${v}_ho; mkdir -p dump_${v}_ho
      "$PY" "$H/drive110.py" ho_$v 150 --host-only --dump="$(cygpath -w "$S/id/dump_${v}_ho")" --exe="$(cygpath -w "$exe")" \
        --root="$(cygpath -w "$S/$v")" --outdir=. "RECOMP_DATA_DIR=$(cygpath -w "$S/idd/$v")" > ho_$v.drive.txt 2>&1
    fi
  done
done
echo all done
