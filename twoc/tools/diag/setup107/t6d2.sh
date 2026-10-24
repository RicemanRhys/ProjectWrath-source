# Step 107 Part 6, group D: an existing game_files replaced (the F1 menu's Choose another disc image..., the
# confirmation, the swap, the rollback when the swap fails, a file held briefly), the read-only folders, the long
# and non-ASCII paths, the window at 1280x800 and 4K with the controller only.
S=/c/Users/User/AppData/Local/Temp/claude/C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp/97b6a2da-2508-4c67-8577-613c20f000ea/scratchpad
W=C:/Users/User/AppData/Local/Temp/claude/C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp/97b6a2da-2508-4c67-8577-613c20f000ea/scratchpad
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
ISO='C:\Users\User\Desktop\Desktop Stuff\Emulators & Roms\Roms\Xbox OG\Crash Bandicoot - The Wrath of Cortex (USA).xiso.iso'
EXE="$S/t107/sw/A/crash_twoc.exe"
cd "$S/t107"
show() {
  grep -a "\[SETUP\] \(failed\|cancel\|stopped\|copied\|done in\|recovery\|test\|game_files\|journal: phase\|carry\|swapped\|chosen\|the setup screen\|opening\|renaming\)\|\[SETUP-UI\] \(button\|dropped\|closed\|job\|screen\|setup screen\|found\|pad\)\|\[OVL\] \(asks\|About\)\|\[START\]\|\[CFG\] first\|\[FILES\]\|\[RESTART\]\|MAIN MENU OPEN" "$1" | cut -c1-300
}
t() {  # TAG RUN STEPS [ENV...] (extra t107 options in OPT)
  local tag=$1 r=$2 steps=$3; shift 3
  if [ -n "$ONLY" ] && ! echo " $ONLY " | grep -q " $tag "; then return; fi
  local envs=()
  for e in "$@"; do envs+=("--env=$e"); done
  local EXE1="$W/t107/{NA}/$tag/crash_twoc.exe"; [ -n "$EXEP" ] && EXE1=$EXEP
  echo "=== $tag $r"
  "$PY" "$S/tools/t107.py" ${tag}$r "$W/t107/{NA}/$tag" "$EXE1" ${SELF---self} $OPT "${envs[@]}" "--steps=$steps" 2>&1 | grep -v "^ *[0-9.]* grab"
  local CR="$W/t107/{NA}/$tag"; [ -n "$CHKROOT" ] && CR=$CHKROOT
  "$PY" "$S/tools/check107.py" "$CR" --save crash.sav
  show "$S/t107/Test é Ω/$tag/${tag}$r.err.log"
}
prep() { [ -n "$ONLY" ] && ! echo " $ONLY " | grep -q " $1 " && return; "$PY" "$S/tools/prep107.py" "$W/t107/{NA}/$1" --exe "$EXE" $2 $3 $4; }

# D1: game_files there; the F1 menu, About tab, Choose another disc image..., Restart; the setup screen; the image;
# the replace confirmation; the swap; the game. Then the next start deletes game_files.old.
prep d1 --gf
t d1 a "wait:MAIN MENU OPEN@60;sleep:2;gkey:f1;sleep:1;tap:lb;sleep:0.5;grabg:about;tap:down;sleep:0.3;grabg:about1;tap:right;sleep:0.3;grabg:about2;tap:a;sleep:0.5;grabg:ask;tap:left;tap:a;follow:30;wait:setup screen open@30;wait:screen: confirm@30;sleep:0.5;grab:confirm;key:down;key:enter;wait:closed: game files ready@120;wait:MAIN MENU OPEN@90;end" "RECOMP_DEV_SETUP_DROP=$ISO"
t d1 b "wait:MAIN MENU OPEN@60;sleep:4;end"

# D2: the rollback: game_files.new's file held open while the swap's second rename runs, so it fails; the old files
# and the save are put back
prep d2 --gf --request
OPT= t d2 a "wait:setup screen open@20;wait:screen: confirm@30;key:down;key:enter;wait:test hold.*renames@60;lock:$W/t107/{NA}/d2/game_files.new/Crashdat/chars.dat@25;wait:job: failed@60;sleep:0.5;grab:failed;close;waitexit:20" "RECOMP_DEV_SETUP_DROP=$ISO" "RECOMP_DEV_SETUP_HOLD=renames@0:2000"
t d2 b "wait:setup screen open@20;sleep:1;grab:after;close;waitexit:20"

# D3: a file of game_files held for 3 s just as the swap starts: retried, then the swap goes through
prep d3 --gf --request
t d3 a "wait:setup screen open@20;wait:screen: confirm@30;key:down;key:enter;wait:test hold.*carry@60;lock:$W/t107/{NA}/d3/game_files/Crashdat/chars.dat@3;wait:closed: game files ready@120;wait:MAIN MENU OPEN@90;end" "RECOMP_DEV_SETUP_DROP=$ISO" "RECOMP_DEV_SETUP_HOLD=carry@0:500"
echo all done
