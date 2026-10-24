# Step 107 Part 6, group A: the Share exe with no game_files, started like a double-click, set up five ways.
S=/c/Users/User/AppData/Local/Temp/claude/C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp/97b6a2da-2508-4c67-8577-613c20f000ea/scratchpad
W=C:/Users/User/AppData/Local/Temp/claude/C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp/97b6a2da-2508-4c67-8577-613c20f000ea/scratchpad
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
ISO='C:\Users\User\Desktop\Desktop Stuff\Emulators & Roms\Roms\Xbox OG\Crash Bandicoot - The Wrath of Cortex (USA).xiso.iso'
TWIN='C:\Users\User\Desktop\Desktop Stuff\Emulators & Roms\Roms\Xbox OG\Crash Twinsanity (USA, Asia).xiso.iso'
EXE="$S/t107/sw/A/crash_twoc.exe"
cd "$S/t107"
run() {  # TAG STEPS [ENV...]
  local tag=$1 steps=$2; shift 2
  if [ -n "$ONLY" ] && ! echo " $ONLY " | grep -q " $tag "; then return; fi
  local envs=()
  for e in "$@"; do envs+=("--env=$e"); done
  echo "=== $tag"
  "$PY" "$S/tools/t107.py" $tag "$W/t107/{NA}/$tag" "$W/t107/{NA}/$tag/crash_twoc.exe" --self "--copyexe=$EXE" "${envs[@]}" "--steps=$steps" 2>&1 | grep -v "^ *[0-9.]* grab"
  "$PY" "$S/tools/check107.py" "$W/t107/{NA}/$tag" $CHK
  grep -a "\[SETUP\] failed\|\[SETUP-UI\] \(button\|typed\|dropped\|image chosen\|folder chosen\|found\)\|\[SETUP\] \(copied\|done in\|source\|game_files checked\)\|\[FILES\]" "$S/t107/Test é Ω/$tag/$tag.err.log" | cut -c1-260
}
# A1: a copy of the extracted game_files, chosen as the folder (the pad: Down, A; the folder dialog)
CHK=--hash run a1 "wait:setup screen open@20;sleep:1.5;tap:down;tap:a;dialog:$W/fx/gf copy;wait:job: (copying|verifying)@30;sleep:1;grab:work;wait:closed: game files ready@120;wait:MAIN MENU OPEN@90;sleep:2;grabg:menu;end"
# A2: the real image, through the file dialog (the pad: A)
CHK=--hash run a2 "wait:setup screen open@20;sleep:1;grab:home;tap:a;dialog:$ISO;wait:job: copying@30;sleep:1.5;grab:copy;wait:job: verifying@60;grab:verify;wait:job: done@60;sleep:0.3;grab:done;wait:MAIN MENU OPEN@90;sleep:2;grabg:menu;end"
# A3: the synthetic Redump-style image, dropped onto the window
CHK=--hash run a3 "wait:setup screen open@20;wait:job: copying@30;sleep:1;grab:copy;wait:closed: game files ready@120;wait:MAIN MENU OPEN@90;end" "RECOMP_DEV_SETUP_DROP=$W/fx/redump.iso"
# A4: the real image as a typed path ("&" in it): Down Down to the box, Enter to type, the path, Enter
CHK= run a4 "wait:setup screen open@20;sleep:1.5;key:down,down;sleep:0.3;key:enter;type:$ISO;sleep:0.3;grab:typed;key:enter;wait:job: copying@30;wait:closed: game files ready@120;wait:MAIN MENU OPEN@90;end"
# A5: Twinsanity: refused, naming the game
CHK= run a5 "wait:setup screen open@20;wait:job: failed@30;sleep:0.5;grab:refused;tap:b;sleep:0.5;grab:back;close;waitexit:20;end" "RECOMP_DEV_SETUP_DROP=$TWIN"
echo all done
