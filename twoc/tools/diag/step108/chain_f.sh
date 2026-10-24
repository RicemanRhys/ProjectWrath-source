# chain_f.sh -- step 108 feature checks, before (B) and after (N): HRTF off, 5.1 forced, music 0, effects 0
# (attract demo), and Droid Void with the drone mute on and off.
cd $(dirname $(readlink -f $0))
for E in B N; do
  bash aud108.sh ${E}_f_hrtf0 $E demo RECOMP_FRAMERATE=60 RECOMP_HRTF=0 > /dev/null 2>&1
  bash aud108.sh ${E}_f_51 $E demo RECOMP_FRAMERATE=60 RECOMP_SPEAKERS=5.1 RECOMP_APU_FORCE_51=1 > /dev/null 2>&1
  bash aud108.sh ${E}_f_mus0 $E demo RECOMP_FRAMERATE=60 RECOMP_MUSIC_VOLUME=0 > /dev/null 2>&1
  bash aud108.sh ${E}_f_fx0 $E demo RECOMP_FRAMERATE=60 RECOMP_EFFECTS_VOLUME=0 > /dev/null 2>&1
  LV=20 bash aud108.sh ${E}_f_dvon $E level RECOMP_FRAMERATE=60 RECOMP_MUTE_DROID_DRONE=1 > /dev/null 2>&1
  LV=20 bash aud108.sh ${E}_f_dvoff $E level RECOMP_FRAMERATE=60 > /dev/null 2>&1
done
echo CHAIN F DONE
