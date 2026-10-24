# sm105.sh TAG EXEDIR "STEPS" [ENV...] -- step 105: Smokey and the Bandicoot (level 3, pad 1:1) at Game (60) (FR=display
# for Match display + render thread), 16:9, HUD at the edges, windowed RS (default 1920x1080), lives kept; trigger
# files in runs/TAG/ (sub.trig sub-frame dumps, dl.trig draw log + host dumps, rec.bin.go INTERP_REC).
S=$(dirname $(readlink -f $0))
cd $S
T=$1; X=$(cygpath -w $(readlink -f $2)/crash_twoc.exe); ST=$3; shift 3
mkdir -p runs/$T
D=$(cygpath -m $(pwd)/runs/$T)
export WGCGRAB='C:\Users\User\AppData\Local\Temp\claude\C--Users-User-Desktop-Desktop-Stuff-Claude-Stuff-Crash-TWOC-decomp\34018395-629f-40f9-9d4d-f55117e9c429\scratchpad\bin\wgcgrab.exe'
if [ "${FR:-60}" = "display" ]; then FE="RECOMP_FRAMERATE=display RECOMP_RENDER_THREAD=on RECOMP_FRAMERATE_ADAPTIVE=0"; else FE="RECOMP_FRAMERATE=60 RECOMP_RENDER_THREAD=off"; fi
ACT_KEEP_LIVES=${KEEP:-1} PASS=B RS=${RS:-1920x1080} WAIT=${WAIT:-28} SECS=${SECS:-600} bash sx.sh $T 3 1:1 "$ST" --exe=$X \
  $FE RECOMP_WIDESCREEN=1 RECOMP_HUD=edges RECOMP_FULLSCREEN=0 \
  RECOMP_D3D_BRIDGE_SUBDUMP=@$D/sub.trig RECOMP_D3D_BRIDGE_DRAWLOG_TRIGGER=$D/dl.trig \
  RECOMP_D3D_BRIDGE_INTERP_REC=$D/rec.bin RECOMP_FB_DUMP=$D/shot "$@"
