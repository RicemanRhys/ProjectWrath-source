# fin106.sh -- step 106's checks of C (the w = 0 fix + the automatic rolling saves) against F (HEAD before): identity,
# REPLAY_CHECK (C) and the sub-frame bake check (XC = C + ../step105_bake_check.patch) in cast_bug, avalanch, upstairs
# and Smokey (the drive z 515 -> the finish, and step 105's pinned spot), timing (cast_bug, 2 rounds), the wall fix
# (bis105.sh, g60_105.sh), six places side by side. Summaries in runs/fin106_*.txt.
cd $(dirname $(readlink -f $0))
bash id.sh F C > /dev/null 2>&1; bash idcmp.sh F C > runs/fin106_id.txt 2>&1
bash chk.sh r106 C RECOMP_FRAMERATE=display RECOMP_RENDER_THREAD=sync > runs/fin106_chk.txt 2>&1
bash ichk.sh i106 XC RECOMP_FRAMERATE=display RECOMP_RENDER_THREAD=sync > runs/fin106_ichk.txt 2>&1
DRV="wait:1 pad:a=255_ly=32767 wait:2.5 x:drv.py:once:-6.5:0.3:515:0 bg:ap.py:TAG:22:$(cat wp_515.txt) wait:24"
PIN="wait:1 pad:a=255_ly=32767 wait:2.5 bg:ballmove.py:-7.1:0.06:714:70 wait:1.5 pad: wait:50"
FR=display bash sm105.sh r106_dr C "${DRV//TAG/r106_dr}" RECOMP_D3D_BRIDGE_REPLAY_CHECK=1 RECOMP_RENDER_THREAD=sync > /dev/null 2>&1
FR=display bash sm105.sh i106_dr XC "${DRV//TAG/i106_dr}" TMP_ICHECK=1 RECOMP_RENDER_THREAD=sync > /dev/null 2>&1
FR=display bash sm105.sh r106_sm C "$PIN" RECOMP_D3D_BRIDGE_REPLAY_CHECK=1 RECOMP_RENDER_THREAD=sync > /dev/null 2>&1
FR=display bash sm105.sh i106_sm XC "$PIN" TMP_ICHECK=1 RECOMP_RENDER_THREAD=sync > /dev/null 2>&1
for t in r106_dr i106_dr r106_sm i106_sm; do echo "== $t"; grep -a "replay check\|interpolation check\|sub-frame check" runs/$t.err.log | tail -2; grep -ac "differs" runs/$t.err.log; grep '^ap' runs/$t.act.txt | tail -1; done > runs/fin106_sm.txt
WHERE=cb bash tm.sh tm106 2 F C > runs/fin106_tm.txt 2>&1
for E in F C; do bash bis105.sh b$E $E; done > runs/fin106_bis.txt 2>&1
for E in F C; do bash g60_105.sh g$E $E; done > runs/fin106_g60.txt 2>&1
EXES="F C" bash sh105.sh > runs/fin106_sh.txt 2>&1
echo fin106 done
