# t180.sh: step 77 item 4 with the display at 180 Hz (the user set it; the monitor offers 60, 120, 180, 240 at 4K, no 144).
# 120 (falls back to 72), 90 (falls back to 72), Game (60); cast_bug gliding with the camera log and sub-frame dumps
# at Match display and 72 (adaptive off); cast_bug gliding at Match display with adaptive on (the 144/72/60 ladder);
# the overlay at Match display.
cd $(dirname $(readlink -f $0))
X=$(cygpath -w $(pwd)/exe_item3); E="--exe=$X\crash_twoc.exe"
C=$(cygpath -w $(pwd)/exe_cam); EC="--exe=$C\crash_twoc.exe"
up() { PRE="upst.py $1" WAIT=3 bash sx.sh $1 - - "wait:35" $E "${@:2}" 2>&1 | tail -1; }
up h180dsp RECOMP_FRAMERATE=display
up h144 RECOMP_FRAMERATE=144
up h90 RECOMP_FRAMERATE=90
up h120 RECOMP_FRAMERATE=120
up h144v RECOMP_FRAMERATE=144 RECOMP_VRR=1
up h60 RECOMP_FRAMERATE=60
gl() {
  rm -rf runs/$1; mkdir -p runs/$1
  WAIT=22 bash sx.sh $1 u 1 "wait:5 x:glsub.py:$1:30:-3:7:10:12:16" $EC RECOMP_FRAMERATE_ADAPTIVE=0 TMP_CAMLOG=1 \
    RECOMP_FB_DUMP=$(cygpath -m $(pwd)/runs/$1)/shot RECOMP_D3D_BRIDGE_SUBDUMP=@$(cygpath -m $(pwd)/runs/$1)/sub.trig "${@:2}" 2>&1 | tail -1
}
gl h180g RECOMP_FRAMERATE=display
gl h90g RECOMP_FRAMERATE=90
gl h144vg RECOMP_FRAMERATE=144 RECOMP_VRR=1
bash sx.sh h180cb u 1 "x:glide.py:h180cb:40:-3:7:3" $E RECOMP_FRAMERATE=display 2>&1 | tail -1
PRE="upst.py h180ov" WAIT=3 RS=2560x1440 bash sx.sh h180ov - - "wait:8 x:esc.py wait:2 grab:ovl wait:1 x:esc.py wait:2" $E RECOMP_FRAMERATE=120 2>&1 | tail -1
echo ALLDONE
