# fin105.sh -- step 105's checks of X (the haze band's depth bias) against F (step 104's build): the sub-frame bake
# check (XC = X + ../step105_bake_check.patch, TMP_ICHECK=1) and REPLAY_CHECK (render thread in sync) in cast_bug
# gliding, avalanch, upstairs and at Smokey's spot (the vehicle pinned at -7.1 0.06 714); cast_bug's timing
# (2 rounds, F and X interleaved). Summaries in runs/fin105_*.txt.
cd $(dirname $(readlink -f $0))
bash ichk.sh i105 XC RECOMP_FRAMERATE=display RECOMP_RENDER_THREAD=sync > runs/fin105_ichk.txt 2>&1
bash chk.sh r105 X RECOMP_FRAMERATE=display RECOMP_RENDER_THREAD=sync > runs/fin105_chk.txt 2>&1
PIN="wait:1 pad:a=255_ly=32767 wait:2.5 bg:ballmove.py:-7.1:0.06:714:70 wait:1.5 pad: wait:50"
FR=display bash sm105.sh i105_sm XC "$PIN" TMP_ICHECK=1 RECOMP_RENDER_THREAD=sync > /dev/null 2>&1
FR=display bash sm105.sh r105_sm X "$PIN" RECOMP_D3D_BRIDGE_REPLAY_CHECK=1 RECOMP_RENDER_THREAD=sync > /dev/null 2>&1
for t in i105_sm r105_sm; do echo "== $t"; grep -a "replay check" runs/$t.err.log | tail -2; grep -ac "differs" runs/$t.err.log; grep -a "steps nearer" runs/$t.err.log | tail -1 | grep -o "blended over.*"; done > runs/fin105_sm.txt
WHERE=cb bash tm.sh tm105 2 F X > runs/fin105_tm.txt 2>&1
echo fin105 done
