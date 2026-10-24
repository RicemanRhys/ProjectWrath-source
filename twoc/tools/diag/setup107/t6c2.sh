# Step 107 Part 6, group C: the process killed hard (taskkill /F, a power cut's stand-in) at four points, then
# started again: the recovery cleans up, says the last setup didn't finish, puts game_files.old back if needed,
# and Retry succeeds. Without and with an existing game_files (with a stand-in save and a file of the player's).
S=/c/Users/User/AppData/Local/Temp/claude/C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp/97b6a2da-2508-4c67-8577-613c20f000ea/scratchpad
W=C:/Users/User/AppData/Local/Temp/claude/C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp/97b6a2da-2508-4c67-8577-613c20f000ea/scratchpad
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
ISO='C:\Users\User\Desktop\Desktop Stuff\Emulators & Roms\Roms\Xbox OG\Crash Bandicoot - The Wrath of Cortex (USA).xiso.iso'
EXE="$S/t107/sw/A/crash_twoc.exe"
cd "$S/t107"
show() {
  grep -a "\[SETUP\] \(failed\|cancel\|stopped\|copied\|done in\|recovery\|test\|game_files\|journal: phase\|carry\|swapped\)\|\[SETUP-UI\] \(button\|dropped\|closed\|job\|screen\|setup screen\)\|\[FILES\]\|MAIN MENU OPEN" "$S/t107/Test é Ω/$1/$2.err.log" | cut -c1-300
}
t() {  # TAG RUN STEPS [ENV...]: one start of the exe in {NA}/TAG (copied there once)
  local tag=$1 r=$2 steps=$3; shift 3
  if [ -n "$ONLY" ] && ! echo " $ONLY " | grep -q " $tag "; then return; fi
  local envs=()
  for e in "$@"; do envs+=("--env=$e"); done
  echo "=== $tag $r"
  "$PY" "$S/tools/t107.py" ${tag}$r "$W/t107/{NA}/$tag" "$W/t107/{NA}/$tag/crash_twoc.exe" --self "${envs[@]}" "--steps=$steps" 2>&1 | grep -v "^ *[0-9.]* grab"
  "$PY" "$S/tools/check107.py" "$W/t107/{NA}/$tag" --save crash.sav
  show $tag ${tag}$r
}
prep() { [ -n "$ONLY" ] && ! echo " $ONLY " | grep -q " $1 " && return; "$PY" "$S/tools/prep107.py" "$W/t107/{NA}/$1" --exe "$EXE" $2; }
RETRY="wait:setup screen open \(after a recovery\)@20;sleep:1;grab:recovery;key:enter"
# C1-C3: no game_files before; killed early in the copy, late in it, during the check
for c in "c1 copy@100" "c2 copy@2200" "c3 verify@1000"; do
  set -- $c
  prep $1
  t $1 a "wait:setup screen open@20;wait:test hold@60;sleep:1;grab:held;kill;end" "RECOMP_DEV_SETUP_DROP=$ISO" "RECOMP_DEV_SETUP_HOLD=$2:60000"
  t $1 b "$RETRY;wait:closed: game files ready@120;wait:MAIN MENU OPEN@90;end"
done
# C4: an existing game_files, killed between the two renames (game_files already game_files.old)
prep c4 "--gf --request"
t c4 a "wait:setup screen open@20;wait:screen: confirm@30;sleep:0.5;grab:confirm;key:down;key:enter;wait:test hold.*renames@60;sleep:1;kill;end" "RECOMP_DEV_SETUP_DROP=$ISO" "RECOMP_DEV_SETUP_HOLD=renames@0:60000"
t c4 b "$RETRY;wait:screen: confirm@30;key:down;key:enter;wait:closed: game files ready@120;wait:MAIN MENU OPEN@90;end"
t c4 c "wait:MAIN MENU OPEN@60;sleep:3;end"
# C5: an existing game_files, killed early in the copy; then "Start the game with the current files"
prep c5 "--gf --request"
t c5 a "wait:setup screen open@20;wait:screen: confirm@30;key:down;key:enter;wait:test hold: copy@60;sleep:1;kill;end" "RECOMP_DEV_SETUP_DROP=$ISO" "RECOMP_DEV_SETUP_HOLD=copy@500:60000"
t c5 b "wait:setup screen open \(after a recovery\)@20;sleep:1;grab:recovery;key:down,down,down,down;sleep:0.3;grab:start;key:enter;wait:MAIN MENU OPEN@90;end"
# C6: an existing game_files, killed during the check
prep c6 "--gf --request"
t c6 a "wait:setup screen open@20;wait:screen: confirm@30;key:down;key:enter;wait:test hold: verify@60;sleep:1;kill;end" "RECOMP_DEV_SETUP_DROP=$ISO" "RECOMP_DEV_SETUP_HOLD=verify@1500:60000"
t c6 b "$RETRY;wait:screen: confirm@30;key:down;key:enter;wait:closed: game files ready@120;wait:MAIN MENU OPEN@90;end"
echo all done
