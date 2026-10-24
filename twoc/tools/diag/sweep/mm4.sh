# mm4.sh -- step 104 Part 4: Menu scale at four sizes x four scales (Display, Extras, restart / reset / quit
# questions, the amber row), then pause off at 150%.
S=$(dirname $(readlink -f $0)); cd $S
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
E=$(cygpath -w $S/${EXE:-P4})
D6="down;down;down;down;down;down;"
for rs in 1280x800 1920x1080 1920x1200 3840x2160; do
  for sc in 50 100 150 200; do
    T=ms_${rs}_$sc
    "$PY" mtab.py $T "$E" $rs "f1;wait:1;grab:1disp;lb;lb;wait:0.6;grab:2extras;${D6}a;wait:1;grab:3askrestart;b;wait:0.8;grab:4amber;down;a;wait:0.8;grab:5askreset;b;down;right;a;wait:0.8;grab:6askquit;b;f1" RECOMP_MENU_SCALE=$sc > runs/$T.txt 2>&1
  done
  T=mp_${rs}_150
  "$PY" mtab.py $T "$E" $rs "f1;wait:1;grab:1disp;lb;lb;wait:0.6;grab:2extras;f1" RECOMP_MENU_SCALE=150 RECOMP_OVERLAY_PAUSE=0 > runs/$T.txt 2>&1
done
echo ALLDONE
