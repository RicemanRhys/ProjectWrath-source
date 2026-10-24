# Step 107 Part 6, group E: read-only folders, long and non-ASCII paths, the window at 1280x800 and 4K driven by the
# controller alone, the setup's memory and time.
S=/c/Users/User/AppData/Local/Temp/claude/C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp/97b6a2da-2508-4c67-8577-613c20f000ea/scratchpad
W=C:/Users/User/AppData/Local/Temp/claude/C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp/97b6a2da-2508-4c67-8577-613c20f000ea/scratchpad
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
ISO='C:\Users\User\Desktop\Desktop Stuff\Emulators & Roms\Roms\Xbox OG\Crash Bandicoot - The Wrath of Cortex (USA).xiso.iso'
EXE="$S/t107/sw/A/crash_twoc.exe"
cd "$S/t107"
show() {
  grep -a "\[SETUP\] \(failed\|cancel\|stopped\|copied\|done in\|recovery\|test\|game_files\|journal: phase\|carry\|swapped\|chosen\|source\)\|\[SETUP-UI\] \(button\|dropped\|closed\|job\|screen\|setup screen\|found\|pad\|disc images\)\|\[START\]\|\[CFG\] \(first\|settings from\)\|\[FILES\]\|MAIN MENU OPEN" "$1" | cut -c1-300
}
on() { [ -z "$ONLY" ] || echo " $ONLY " | grep -q " $1 "; }

# E1: launcher-style (Play Crash.bat's way), the working folder read-only: refused with what to do
if on e1; then
  echo "=== e1"
  "$PY" "$S/tools/prep107.py" "$W/t107/{NA}/e1" --ro ro
  "$PY" "$S/tools/t107.py" e1 "$W/t107/{NA}/e1" "$EXE" "--cwd=$W/t107/{NA}/e1/ro" "--env=RECOMP_DEV_SETUP_DROP=$ISO" "--steps=wait:setup screen open@20;wait:job: failed@60;sleep:0.5;grab:failed;close;waitexit:20" | grep -v " grab"
  "$PY" "$S/tools/check107.py" "$W/t107/{NA}/e1/ro"
  show "$S/t107/Test é Ω/e1/e1.err.log"
  "$PY" "$S/tools/prep107.py" "$W/t107/{NA}/e1" --unro ro
fi
# E2: double-clicked in a read-only folder (as in C:\Program Files): the data folder goes to %LOCALAPPDATA%\CrashTWOC
# (a stand-in in scratch, RECOMP_DEV_LOCALAPPDATA), and the setup works there
if on e2; then
  echo "=== e2"
  "$PY" "$S/tools/prep107.py" "$W/t107/{NA}/e2" --exe "$EXE" --exedir ro --ro ro
  "$PY" "$S/tools/t107.py" e2 "$W/t107/{NA}/e2" "$W/t107/{NA}/e2/ro/crash_twoc.exe" --self "--datadir=$W/t107/{NA}/e2/lad/CrashTWOC" "--env=RECOMP_DEV_LOCALAPPDATA=$W/t107/{NA}/e2/lad" "--env=RECOMP_DEV_SETUP_DROP=$ISO" "--steps=wait:setup screen open@20;sleep:0.8;grab:home;wait:closed: game files ready@120;wait:MAIN MENU OPEN@90;end" | grep -v " grab"
  "$PY" "$S/tools/check107.py" "$W/t107/{NA}/e2/lad/CrashTWOC"
  ls "$S/t107/Test é Ω/e2/ro" "$S/t107/Test é Ω/e2/lad/CrashTWOC"
  show "$S/t107/Test é Ω/e2/e2.err.log"
  "$PY" "$S/tools/prep107.py" "$W/t107/{NA}/e2" --unro ro
fi
# E3: the image in a folder whose path is over 260 characters, of long names with letters outside ASCII
if on e3; then
  echo "=== e3"
  "$PY" "$S/tools/prep107.py" "$W/t107/{NA}/e3src" --deep 300 --copyiso "Crash (USA).iso"
  "$PY" "$S/tools/t107.py" e3 "$W/t107/{NA}/e3" "$W/t107/{NA}/e3/crash_twoc.exe" --self "--copyexe=$EXE" "--env=RECOMP_DEV_SETUP_DROP={DEEP:e3src}/Crash (USA).iso" "--steps=wait:setup screen open@20;wait:closed: game files ready@120;wait:MAIN MENU OPEN@90;end" | grep -v " grab"
  "$PY" "$S/tools/check107.py" "$W/t107/{NA}/e3" --hash
  show "$S/t107/Test é Ω/e3/e3.err.log"
fi
# E4: the game's own folder too deep for the game (over 196 characters): refused before anything is copied
if on e4; then
  echo "=== e4"
  "$PY" "$S/tools/prep107.py" "$W/t107/{NA}/e4" --deep 215 --exe "$EXE"
  "$PY" "$S/tools/t107.py" e4 "$W/t107/{NA}/e4" "{DEEP:e4}/crash_twoc.exe" --self "--env=RECOMP_DEV_SETUP_DROP=$ISO" "--steps=wait:setup screen open@20;wait:job: failed@60;sleep:0.5;grab:failed;close;waitexit:20" | grep -v " grab"
  "$PY" "$S/tools/check107.py" "{DEEP:e4}"
  "$PY" -c "import sys; sys.stdout.reconfigure(encoding='utf-8'); print(open(sys.argv[1], encoding='utf-8').read().split('scratchpad')[1])" "$S/t107/deep_e4.txt"
fi
# E5, E6: the controller alone, at 1280x800 and maximised on the 4K screen: an image found in the game's folder
for e in "e5 RECOMP_DEV_SETUP_SIZE=1280x800" "e6 RECOMP_DEV_SETUP_X=1"; do
  set -- $e
  on $1 || continue
  echo "=== $1"
  "$PY" "$S/tools/prep107.py" "$W/t107/{NA}/$1" --image "My images/Crash (USA).iso"
  MAX=; [ $1 = e6 ] && MAX="max;sleep:1;"
  "$PY" "$S/tools/t107.py" $1 "$W/t107/{NA}/$1" "$W/t107/{NA}/$1/crash_twoc.exe" --self "--copyexe=$EXE" "--env=$2" "--steps=wait:disc images found nearby@30;sleep:0.5;${MAX}grab:home;mem;tap:down;tap:down;sleep:0.3;grab:onfound;tap:a;wait:job: copying@30;sleep:2;grab:copy;mem;wait:job: verifying@60;sleep:2;mem;wait:job: done@60;sleep:0.3;grab:done;mem;wait:MAIN MENU OPEN@90;end" | grep -v " grab"
  "$PY" "$S/tools/check107.py" "$W/t107/{NA}/$1"
  show "$S/t107/Test é Ω/$1/$1.err.log"
done
echo all done
