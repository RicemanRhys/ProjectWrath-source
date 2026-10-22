# dd.sh TAG EXEDIR "STEPS" [ENV...] -- step 100 item 1: Drain Damage (level 23 via pad 2:1), standing, lives kept at 9,
# Match display 240 Hz, render thread on, 16:9, HUD edges margin 0, windowed RS (default 1920x1080); trigger files in
# runs/TAG/ (sub.trig sub-frame dumps, dl.trig draw log, rec.bin.go INTERP_REC).
S=$(dirname $(readlink -f $0))
cd $S
T=$1; X=$(cygpath -w $(readlink -f $2)/crash_twoc.exe); ST=$3; shift 3
mkdir -p runs/$T
D=$(cygpath -m $(pwd)/runs/$T)
export WGCGRAB='C:\Users\User\AppData\Local\Temp\claude\C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp\34018395-629f-40f9-9d4d-f55117e9c429\scratchpad\bin\wgcgrab.exe'
ACT_KEEP_LIVES=${KEEP:-1} PASS=A RS=${RS:-1920x1080} WAIT=${WAIT:-25} SECS=${SECS:-600} bash sx.sh $T ${LV:-23} ${RP:-2:1} "$ST" --exe=$X \
  RECOMP_FULLSCREEN=0 RECOMP_RENDER_THREAD=on RECOMP_HUD_MARGIN=0 \
  RECOMP_D3D_BRIDGE_SUBDUMP=@$D/sub.trig RECOMP_D3D_BRIDGE_DRAWLOG_TRIGGER=$D/dl.trig \
  RECOMP_D3D_BRIDGE_INTERP_REC=$D/rec.bin RECOMP_FB_DUMP=$D/shot "$@"
