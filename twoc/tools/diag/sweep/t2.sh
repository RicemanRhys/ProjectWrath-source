# t2.sh EXEDIR PFX: step 77 item 2 at 240 Hz. Upstairs standing (the frames-shown log): 120, 90 (falls back to 80),
# 90 with RECOMP_VRR=1 (the limiter on a fixed refresh), 90 with RECOMP_VRR=measure, 144 (falls back to 120),
# display; then cast_bug gliding with sub-frame dumps at 120, 90 and display (adaptive off).
cd $(dirname $(readlink -f $0))
X=$(cygpath -w $(readlink -f $1)); P=$2
E="--exe=$X\crash_twoc.exe"
up() { PRE="upst.py $1" WAIT=3 bash sx.sh $1 - - "wait:35" $E "${@:2}" 2>&1 | tail -1; }
up ${P}u120 RECOMP_FRAMERATE=120
up ${P}u90 RECOMP_FRAMERATE=90
up ${P}u90v RECOMP_FRAMERATE=90 RECOMP_VRR=1
up ${P}u90m RECOMP_FRAMERATE=90 RECOMP_VRR=measure
up ${P}u144 RECOMP_FRAMERATE=144
up ${P}udsp RECOMP_FRAMERATE=display
gl() {
  rm -rf runs/$1; mkdir -p runs/$1
  WAIT=22 bash sx.sh $1 u 1 "wait:5 x:glsub.py:$1:30:-3:7:3:12:16" $E RECOMP_FRAMERATE_ADAPTIVE=0 \
    RECOMP_FB_DUMP=$(cygpath -m $(pwd)/runs/$1)/shot RECOMP_D3D_BRIDGE_SUBDUMP=@$(cygpath -m $(pwd)/runs/$1)/sub.trig "${@:2}" 2>&1 | tail -1
}
gl ${P}g120 RECOMP_FRAMERATE=120
gl ${P}g90 RECOMP_FRAMERATE=90
gl ${P}gdsp RECOMP_FRAMERATE=display
echo ALLDONE
