# Step 107 Part 6, group B: failures. Each: a clear message, no half-made game_files, a retry possible.
S=/c/Users/User/AppData/Local/Temp/claude/C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp/97b6a2da-2508-4c67-8577-613c20f000ea/scratchpad
W=C:/Users/User/AppData/Local/Temp/claude/C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp/97b6a2da-2508-4c67-8577-613c20f000ea/scratchpad
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
ISO='C:\Users\User\Desktop\Desktop Stuff\Emulators & Roms\Roms\Xbox OG\Crash Bandicoot - The Wrath of Cortex (USA).xiso.iso'
EXE="$S/t107/sw/A/crash_twoc.exe"
cd "$S/t107"
show() {  # TAG [ROOT]
  local d=${2:-"$S/t107/Test é Ω/$1"}
  grep -a "\[SETUP\] failed\|\[SETUP\] \(cancel\|stopped\|cancelled\|copied\|done in\|recovery\|test\|game_files\|journal: phase\)\|\[SETUP-UI\] \(button\|typed\|dropped\|closed\|the window\|job\|screen\)\|\[START\]\|\[FILES\]" "$d/$1.err.log" | cut -c1-300
}
run() {  # TAG STEPS [ENV...]
  local tag=$1 steps=$2; shift 2
  if [ -n "$ONLY" ] && ! echo " $ONLY " | grep -q " $tag "; then return; fi
  local envs=()
  for e in "$@"; do envs+=("--env=$e"); done
  echo "=== $tag"
  "$PY" "$S/tools/t107.py" $tag "$W/t107/{NA}/$tag" "$W/t107/{NA}/$tag/crash_twoc.exe" --self "--copyexe=$EXE" "${envs[@]}" "--steps=$steps" 2>&1 | grep -v "^ *[0-9.]* grab"
  "$PY" "$S/tools/check107.py" "$W/t107/{NA}/$tag"
  show $tag
}
FAILED="wait:setup screen open@20;wait:job: failed@60;sleep:0.5"
BACK="tap:b;sleep:0.5;wait:screen: home@5"
# B1: an empty folder, then (the retry) the real image typed
run b1 "$FAILED;grab:failed;$BACK;key:down,down;key:enter;type:$ISO;key:enter;wait:closed: game files ready@120;wait:MAIN MENU OPEN@90;end" "RECOMP_DEV_SETUP_DROP=$W/fx/empty"
# B2: another game's folder (Twinsanity's default.xbe)
run b2 "$FAILED;grab:failed;$BACK;close;waitexit:20" "RECOMP_DEV_SETUP_DROP=$W/fx/other_game"
# B3: a random file
run b3 "$FAILED;grab:failed;$BACK;close;waitexit:20" "RECOMP_DEV_SETUP_DROP=$W/fx/random.iso"
# B4: a truncated image, then (the retry) the real image typed
run b4 "$FAILED;grab:failed;$BACK;key:down,down;key:enter;type:$ISO;key:enter;wait:closed: game files ready@120;wait:MAIN MENU OPEN@90;end" "RECOMP_DEV_SETUP_DROP=$W/fx/truncated.iso"
# B5: another version, another region (a doctored default.xbe)
run b5a "$FAILED;grab:failed;close;waitexit:20" "RECOMP_DEV_SETUP_DROP=$W/fx/v2_xbe"
run b5b "$FAILED;grab:failed;close;waitexit:20" "RECOMP_DEV_SETUP_DROP=$W/fx/pal_xbe"
# B7: no disk space (reported free space), then a full disk part-way (the 500th file's write)
run b7a "$FAILED;grab:failed;close;waitexit:20" "RECOMP_DEV_SETUP_DROP=$ISO" "RECOMP_DEV_SETUP_FREE_MB=100"
run b7b "$FAILED;grab:failed;close;waitexit:20" "RECOMP_DEV_SETUP_DROP=$ISO" "RECOMP_DEV_SETUP_FAIL=diskfull@500"
# B9: Cancel part-way (a slow disk: 40 MB/s), with the pad's B; then the image again: completes
run b9 "wait:setup screen open@20;wait:job: copying@30;sleep:3;grab:copying;tap:b;wait:job: cancelled@30;sleep:0.5;grab:cancelled;$BACK;key:down,down;key:enter;type:$ISO;key:enter;wait:job: copying@30;wait:closed: game files ready@200;end" "RECOMP_DEV_SETUP_DROP=$ISO" "RECOMP_DEV_SETUP_MBPS=40"
# B11: the window closed part-way
run b11 "wait:setup screen open@20;wait:job: copying@30;sleep:3;close;waitexit:30" "RECOMP_DEV_SETUP_DROP=$ISO" "RECOMP_DEV_SETUP_MBPS=40"
# B12: a damaged image (one byte of chars.dat changed)
run b12 "$FAILED;grab:failed;close;waitexit:20" "RECOMP_DEV_SETUP_DROP=$W/fx/damaged.iso"
echo all done
