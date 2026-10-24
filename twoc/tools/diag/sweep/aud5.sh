# aud5.sh -- step 104 Part 5: the APU's output at the main menu (fast title: sound track kept / silenced; slow
# title, B and P5), then a new game's intro (INTRO1, Cortex) with the silencing on.
S=$(dirname $(readlink -f $0)); cd $S
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
W() { cygpath -w $S/runs/$1.wav; }
X() { echo "--exe=$(cygpath -w $S/$1/crash_twoc.exe)"; }
"$PY" launch.py a5_keep 45 $(X P5) RECOMP_FAST_TITLE_SOUND=1 RECOMP_APU_OUT_WAV=$(W a5_keep) > runs/a5_keep.launch.txt 2>&1
"$PY" launch.py a5_mute 45 $(X P5) RECOMP_APU_OUT_WAV=$(W a5_mute) > runs/a5_mute.launch.txt 2>&1
"$PY" launch.py a5_B 45 $(X B) RECOMP_APU_OUT_WAV=$(W a5_B) > runs/a5_B.launch.txt 2>&1
"$PY" launch.py a5_slowB 70 --nofast $(X B) RECOMP_APU_OUT_WAV=$(W a5_slowB) > runs/a5_slowB.launch.txt 2>&1
"$PY" launch.py a5_slowP 70 --nofast $(X P5) RECOMP_APU_OUT_WAV=$(W a5_slowP) > runs/a5_slowP.launch.txt 2>&1
"$PY" launch.py a5_intro 150 $(X P5) RECOMP_APU_OUT_WAV=$(W a5_intro) > runs/a5_intro.launch.txt 2>&1 &
sleep 3
INTRO_GRABS=45 "$PY" newgame2.py a5_intro > runs/a5_intro.ng.txt 2>&1
taskkill //IM crash_twoc.exe //F > /dev/null 2>&1
wait
echo AUDDONE
