# t79ini.sh TAG EXEDIR -- step 79 item 3: the ini. An exe copy in EXEDIR with no ini: open the overlay (held), click
# the Render thread combo and On, close (saves crash_twoc.ini there), then start again with that ini and check the
# log says on. Needs ../winkeys.py copied beside it. CX, CY: the combo in the window's 225%-scaled pixels; CY2 its On row.
cd $(dirname $(readlink -f $0))
T=$1; D=$2; X=$(cygpath -w $D)/crash_twoc.exe
W=winkeys.py
WK=winkeys.py
rm -f $D/crash_twoc.ini
A="wait:6 x:esc.py wait:3 x:$WK:rect grab:open x:$WK:click:${CX:-248}:${CY:-978} wait:1 grab:list"
A="$A x:$WK:click:${CX:-248}:${CY2:-1028} wait:1 grab:picked x:esc.py wait:3 grab:after"
RS=2560x1440 PRE="upst.py $T" WAIT=3 bash sx.sh $T - - "$A" --exe=$X RECOMP_HUD=edges 2>&1 | tail -1
grep -aE "\[OVL\]|\[CFG\]|render thread" runs/$T.err.log | head -12
echo "--- the ini"; cat $D/crash_twoc.ini 2>/dev/null
echo "--- again with it"
RS=2560x1440 bash sx.sh ${T}b - - "wait:5" --exe=$X 2>&1 | tail -1
grep -aE "render thread|\[CFG\]" runs/${T}b.err.log | head -5
rm -f $D/crash_twoc.ini
