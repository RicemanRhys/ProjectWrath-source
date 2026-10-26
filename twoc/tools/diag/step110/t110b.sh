# t110b.sh M [TEST...] -- step 110 (notes 18.113): the saves-outside tests that drive the game (synthetic saves,
# scratch folders, the developer's crash_twoc.exe from M/s/N with RECOMP_SAVES_OUTSIDE=1 and RECOMP_DATA_DIR, the
# same code as the package's ProjectWrath.exe; its window title is the one the step-107 and sweep harnesses find):
#   choose   F1 > About > Choose another disc image... > Restart: the setup screen (the user's disc image dropped,
#            read only), the replace confirmation, the copy, the swap, the game; then the next start (game_files.old
#            deleted). The data folder's synthetic save must come through unchanged and be the one used.
#   verify   F1 > About > Verify game files, the saves unchanged.
#   save     a new game (WOMBAT, the sweep's newgame2.py), the hub's Load/Save console, SAVE GAME in slot a: the save in
#            the data folder, nothing in game_files; then a fresh start, LOAD GAME (load83.sh): grabs.
#   savekill the same save again (OVERWRITE) with a hard kill as the game opens crash.sav to write; the next start:
#            the save is the old one or the new one (it loads), the backups intact.
#   bakkill  a save with each backup copy stretched to 6 s (RECOMP_DEV_SAVE_COPY_DELAY_MS), a hard kill during the
#            copy after the save; the next start: the backups as they were plus nothing half-made.
# Needs: M/s/N (the build), M/tools/t107.py (a copy of ../setup107/t107.py: it writes beside itself), M/s/sw (a copy of
# ../sweep with exe/ and root/, step110's notes), M/s/gfcopy (the disc files, a real copy for the swap).
M=$1; shift
S="$M/s"; W=$(cygpath -w "$M")
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
H=$(dirname "$(readlink -f "$0")")
ISO='C:\Users\User\Desktop\Desktop Stuff\Emulators & Roms\Roms\Xbox OG\Crash Bandicoot - The Wrath of Cortex (USA).xiso.iso'
export WGCGRAB="$(cygpath -w "$H/../wgcgrab.exe")"
want() { [ $# -eq 0 ] || [ -z "$ONLY" ] || echo " $ONLY " | grep -q " $1 "; }
ONLY="$*"
res() { echo "$1" | tee -a "$S/t/results_b.txt"; }
h() { sha256sum "$1" 2>/dev/null | cut -c1-16; }
synth() { "$PY" -c "import sys; sys.path.insert(0, r'$(cygpath -w "$H")'); import t110; t110.synth(sys.argv[1], int(sys.argv[2]))" "$1" "$2" "$M"; }

if want choose; then
  D="$S/t/choose"; rm -rf "$D/data"; mkdir -p "$D/data/saves"
  [ -d "$D/root/game_files/Crashdat" ] || { mkdir -p "$D/root"; cp -r "$S/gfcopy" "$D/root/game_files"; }
  rm -rf "$D/root/game_files.old" "$D/root/game_files.new"
  cp "$S/N/crash_twoc.exe" "$S/N/xemu_apu.dll" "$D/root/"
  synth "$(cygpath -w "$D/data/saves")" 4 > /dev/null
  SAV="$D/data/saves/UDATA/56550003/4D5C926D6357/crash.sav"; H0=$(h "$SAV")
  "$PY" "$M/tools/t107.py" ch1 "$(cygpath -w "$D/root")" "$(cygpath -w "$D/root/crash_twoc.exe")" \
    "--env=RECOMP_SAVES_OUTSIDE=1" "--env=RECOMP_DATA_DIR=$(cygpath -w "$D/data")" "--env=RECOMP_DEV_SETUP_DROP=$ISO" \
    "--steps=wait:MAIN MENU OPEN@60;sleep:2;gkey:f1;sleep:1;tap:lb;sleep:0.5;grabg:about;tap:down;sleep:0.3;tap:right;sleep:0.3;tap:a;sleep:0.5;grabg:ask;tap:left;tap:a;follow:30;wait:setup screen open@30;wait:screen: confirm@30;sleep:0.5;key:down;key:enter;wait:closed: game files ready@120;wait:MAIN MENU OPEN@90;sleep:3;end"
  L="$D/root/ch1.err.log"
  grep -a "\[SAVES\]\|\[SAVE\] at start\|\[RESTART\]\|\[SETUP\] carry\|swapped\|MAIN MENU" "$L" | cut -c1-200
  n=$(grep -ac "\[SAVES\] outside the game's folder" "$L")
  [ "$n" -ge 2 ] && res "PASS choose  both processes (before and after the restart) used the data folder ($n)" || res "FAIL choose  [SAVES] outside lines: $n"
  [ "$(h "$SAV")" = "$H0" ] && res "PASS choose  the data folder's save unchanged through the replace" || res "FAIL choose  the save changed"
  [ "$(grep -ac 'at start: the save equals the backup' "$L")" -ge 2 ] && res "PASS choose  the restarted game found the same save" || res "FAIL choose  the save not found after the restart"
  [ ! -e "$D/root/game_files/UDATA" ] && res "PASS choose  nothing of the saves in the new game_files" || res "FAIL choose  UDATA in game_files"
  "$PY" "$M/tools/t107.py" ch2 "$(cygpath -w "$D/root")" "$(cygpath -w "$D/root/crash_twoc.exe")" \
    "--env=RECOMP_SAVES_OUTSIDE=1" "--env=RECOMP_DATA_DIR=$(cygpath -w "$D/data")" "--steps=wait:MAIN MENU OPEN@60;sleep:4;end"
  sleep 3
  [ ! -e "$D/root/game_files.old" ] && res "PASS choose  the next start deleted game_files.old" || res "FAIL choose  game_files.old still there"
  [ "$(h "$SAV")" = "$H0" ] && res "PASS choose  the save still unchanged" || res "FAIL choose  the save changed (2)"
fi

if want verify; then
  D="$S/t/verify"; rm -rf "$D/data"; mkdir -p "$D/data/saves"
  [ -d "$D/root" ] || powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$H/mkpkg.ps1")" -Dir "$(cygpath -w "$D/root")" -From "$(cygpath -w "$S/N")" -Name crash_twoc.exe > /dev/null
  synth "$(cygpath -w "$D/data/saves")" 5 > /dev/null
  B0=$(cd "$D/data/saves" && find . -type f | sort | xargs sha256sum | sha256sum | cut -c1-16)
  "$PY" "$M/tools/t107.py" vf1 "$(cygpath -w "$D/root")" "$(cygpath -w "$D/root/crash_twoc.exe")" \
    "--env=RECOMP_SAVES_OUTSIDE=1" "--env=RECOMP_DATA_DIR=$(cygpath -w "$D/data")" \
    "--steps=wait:MAIN MENU OPEN@60;sleep:2;gkey:f1;sleep:1;tap:lb;sleep:0.5;tap:down;sleep:0.3;tap:a;wait:game files are the disc|differ@120;sleep:1;grabg:verified;end"
  L="$D/root/vf1.err.log"
  grep -a "About: Verify\|\[SETUP\].*verif\|checked in\|differ" "$L" | cut -c1-200 | tail -3
  B1=$(cd "$D/data/saves" && find . -type f | sort | xargs sha256sum | sha256sum | cut -c1-16)
  grep -aq "About: Verify game files" "$L" && res "PASS verify  Verify game files ran" || res "FAIL verify  no Verify"
  [ "$B0" = "$B1" ] && res "PASS verify  the saves unchanged" || res "FAIL verify  the saves changed"
fi

sw() {  # TAG SCRIPT [ARGS] -- a sweep script in the copy, the data folder D, the game left running
  local tag=$1; shift
  (cd "$S/sw" && SECS=600 bash "$@" RECOMP_SAVES_OUTSIDE=1 "RECOMP_DATA_DIR=$(cygpath -w "$D")" $EXTRA)
}
closegame() { "$PY" "$S/sw/wmclose.py" > /dev/null; for i in $(seq 30); do tasklist //FI "IMAGENAME eq crash_twoc.exe" | grep -q crash_twoc || return 0; sleep 1; done; taskkill //F //IM crash_twoc.exe > /dev/null; }
ctl() { (cd "$S/sw" && "$PY" ctl.py "$@" > /dev/null); }
# launch TAG [ENV...]: the game with the data folder D, the main menu, then LOAD/SAVE > LOAD GAME > the slot > YES
loadgame() {
  local tag=$1; shift
  (cd "$S/sw" && rm -f runs/$tag.* runs/${tag}_*; SECS=600 "$PY" launch.py $tag 600 RECOMP_SAVES_OUTSIDE=1 \
     "RECOMP_DATA_DIR=$(cygpath -w "$D")" "$@" > runs/$tag.launch.txt 2>&1 &
   for i in $(seq 90); do grep -aq "MAIN MENU OPEN" runs/$tag.err.log 2>/dev/null && break; sleep 1; done; sleep 1)
  ctl $tag press down wait:0.5 a wait:3 a wait:2 a wait:2
  ctl $tag grab ${tag}_confirm
  ctl $tag press up a wait:15
  ctl $tag grab ${tag}_loaded
}
# savenow TAG: walk onto the hub's Load/Save pad, SAVE GAME, slot a, YES to overwrite, YES (save83.sh's presses)
savenow() {
  local tag=$1
  echo "ly=32767" > "$S/sw/runs/$tag.pad.txt"; sleep 1.5; echo > "$S/sw/runs/$tag.pad.txt"; sleep 3
  ctl $tag press up*3 a wait:2 a wait:2 up a wait:2
  ctl $tag grab ${tag}_ask
  ctl $tag press up a
}

if want save; then
  D="$S/t/save/data"; rm -rf "$S/t/save"; mkdir -p "$D"; rm -rf "$S/sw/root/game_files/UDATA" "$S/sw/root/game_files/TDATA" "$S/sw/root/game_files/UDATA_backups"
  sw sv1 save83.sh sv1 a | tail -8
  sleep 4; closegame
  SAV="$D/saves/UDATA/56550003/4D5C926D6357/crash.sav"
  [ -f "$SAV" ] && res "PASS save    the game's save is in the data folder ($(stat -c %s "$SAV") bytes)" || res "FAIL save    no crash.sav in the data folder"
  [ ! -e "$S/sw/root/game_files/UDATA" ] && res "PASS save    nothing in the game folder's game_files" || res "FAIL save    UDATA made in game_files"
  ls "$D/saves/UDATA_backups" | grep -q "^20" && res "PASS save    a backup after the save" || res "FAIL save    no backup"
  [ "$(grep -ac "the game's write of 4D5C926D6357.*put in place" "$S/sw/runs/sv1.err.log")" -ge 2 ] && res "PASS save    both save files written to a copy and put in place whole" || res "FAIL save    the atomic write lines missing"
  [ ! -e "$D/saves/.saving" ] && res "PASS save    nothing left in .saving" || res "FAIL save    .saving left"
  cp "$SAV" "$S/t/save/after_save.sav"
  loadgame ld1
  grep -a "\[SAVE\] the game opens" "$S/sw/runs/ld1.err.log" | tail -3
  grep -aq "the game opens 4D5C926D6357.crash.sav (to read" "$S/sw/runs/ld1.err.log" && res "PASS save    LOAD GAME read the save back from the data folder (grabs runs/ld1_confirm, ld1_loaded)" || res "FAIL save    LOAD GAME didn't read crash.sav"
  closegame
fi

if want savekill; then
  D="$S/t/save/data"
  SAV="$D/saves/UDATA/56550003/4D5C926D6357/crash.sav"; META="$D/saves/UDATA/56550003/4D5C926D6357/SaveMeta.xbx"
  OLD=$(h "$SAV"); OLDM=$(h "$META")
  BK0=$(cd "$D/saves/UDATA_backups" && find . -type f | sort | xargs sha256sum)
  loadgame sk1 RECOMP_DEV_SAVE_WRITE_HOLD_MS=8000
  ( for i in $(seq 300); do grep -aq "test hold (RECOMP_DEV_SAVE_WRITE_HOLD_MS)" "$S/sw/runs/sk1.err.log" && break; sleep 0.1; done
    sleep 1; taskkill //F //IM crash_twoc.exe > /dev/null; echo killed ) &
  savenow sk1; wait
  grep -a "\[SAVE\]\|\[SAVES\]" "$S/sw/runs/sk1.err.log" | tail -4 | cut -c1-160
  echo "crash.sav before $OLD after $(h "$SAV"); SaveMeta before $OLDM after $(h "$META")"
  [ "$(h "$SAV")" = "$OLD" ] && [ "$(h "$META")" = "$OLDM" ] && res "PASS savekill killed while the written save waited to be put in place: the save is the old one, whole" || res "FAIL savekill the save changed"
  ls "$D/saves/.saving" 2>/dev/null
  BK1=$(cd "$D/saves/UDATA_backups" && find . -type f | sort | xargs sha256sum)
  [ "$BK0" = "$(echo "$BK1" | grep -F -f <(echo "$BK0" | cut -c67-) )" ] && res "PASS savekill the backups intact after the kill" || res "FAIL savekill a backup changed"
  loadgame sk2
  grep -aq "half-written save file" "$S/sw/runs/sk2.err.log" && [ ! -e "$D/saves/.saving" ] && res "PASS savekill the next start deleted the half-done copy" || res "FAIL savekill .saving not cleaned"
  grep -aq "crash.sav (to read" "$S/sw/runs/sk2.err.log" && res "PASS savekill the next start loads the save (grab runs/sk2_loaded)" || res "FAIL savekill no load"
  # and the same save without the kill: the new one, whole
  savenow sk2; sleep 4
  [ "$(h "$SAV")" != "$OLD" ] && grep -aq "the game's write of 4D5C926D6357.crash.sav put in place" "$S/sw/runs/sk2.err.log" && res "PASS savekill a save not interrupted: the new one in place" || res "FAIL savekill the save didn't change"
  closegame
fi

if want bakkill; then
  D="$S/t/save/data"
  BK0=$(cd "$D/saves/UDATA_backups" && find . -type f | sort | xargs sha256sum)
  loadgame bk1 RECOMP_DEV_SAVE_COPY_DELAY_MS=6000
  ( for i in $(seq 300); do grep -aq "closed a save file it wrote" "$S/sw/runs/bk1.err.log" && break; sleep 0.1; done
    sleep 4.5; taskkill //F //IM crash_twoc.exe > /dev/null; echo killed ) &
  savenow bk1; wait
  grep -a "\[SAVE\]" "$S/sw/runs/bk1.err.log" | tail -4
  BK1=$(cd "$D/saves/UDATA_backups" && find . -type f | sort | xargs sha256sum)
  [ "$BK0" = "$(echo "$BK1" | grep -F -f <(echo "$BK0" | cut -c67-) )" ] && res "PASS bakkill killed during the backup after a save: the backups that were there are intact" || res "FAIL bakkill a backup changed"
  ls "$D/saves/UDATA_backups"
  loadgame bk2
  [ ! -e "$D/saves/UDATA_backups/_in progress" ] && res "PASS bakkill nothing half-made left after the next start" || res "FAIL bakkill _in progress left"
  grep -aq "crash.sav (to read" "$S/sw/runs/bk2.err.log" && res "PASS bakkill the save loads" || res "FAIL bakkill no load"
  closegame
fi
if want savepkg; then
  # the package's layout (the developer's exe as before): the game's save in game_files\UDATA, also written to a
  # copy and put in place whole
  R="$S/sw/root/game_files"; rm -rf "$R/UDATA" "$R/TDATA" "$R/UDATA_backups"
  (cd "$S/sw" && SECS=600 bash save83.sh sp1 a) | tail -3
  sleep 4; closegame
  [ -f "$R/UDATA/56550003/4D5C926D6357/crash.sav" ] && res "PASS savepkg the save in game_files\\UDATA (the package's layout)" || res "FAIL savepkg no save in game_files"
  [ "$(grep -ac "the game's write of 4D5C926D6357.*put in place" "$S/sw/runs/sp1.err.log")" -ge 2 ] && res "PASS savepkg both save files written to a copy and put in place whole" || res "FAIL savepkg no atomic write lines"
  [ ! -e "$R/.saving" ] && res "PASS savepkg nothing left in game_files\\.saving" || res "FAIL savepkg .saving left"
  rm -rf "$R/UDATA" "$R/TDATA" "$R/UDATA_backups"
fi
echo all done
