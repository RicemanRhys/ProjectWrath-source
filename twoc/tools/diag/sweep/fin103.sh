# fin103.sh -- step 103's checks of F1 (the W(t) fix) against B (step 102's build): the sub-frame bake check (ICK =
# F1 + step101_bake_check.patch, TMP_ICHECK=1) and REPLAY_CHECK (render thread in sync) in cast_bug gliding, avalanch,
# upstairs; the smoothness runs (sm.sh); cast_bug's timing (2 rounds); the default run's identity.
cd $(dirname $(readlink -f $0))
bash ichk.sh i103 ICK RECOMP_FRAMERATE=display RECOMP_RENDER_THREAD=sync > runs/log_i103.txt 2>&1
bash chk.sh r103 F1 RECOMP_FRAMERATE=display RECOMP_RENDER_THREAD=sync > runs/log_r103.txt 2>&1
bash sm.sh s103 B F1 > runs/log_s103.txt 2>&1
WHERE=cb bash tm.sh tm103 2 B F1 > runs/log_tm103.txt 2>&1
bash id.sh B F1 > runs/log_id.txt 2>&1
bash idcmp.sh B F1 > runs/idcmp.txt 2>&1
echo fin103 done
