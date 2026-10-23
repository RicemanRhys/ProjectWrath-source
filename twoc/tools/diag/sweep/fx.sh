# fx.sh TAG "STEPS" [ENV...] -- step 101: a level (LV, default 15 Fahrenheit Frenzy; RP pad, default 2:1) with the
# trigger files in runs/TAG/ (dl.trig the draw log, sub.trig sub-frame dumps and "set" lines) and only the dumps
# asked for, PASS=B (4:3, Game 60, render thread off, windowed) unless PASS is set; extra ENV (e.g.
# RECOMP_D3D_BRIDGE_VSDUMP=1, _PSPROBE=1, _TEXCHECK=1, _DUMP_RT=1, --exe=PATH) passed on.
S=$(dirname $(readlink -f $0))
cd $S
T=$1; ST=$2; shift 2
mkdir -p runs/$T
D=$(cygpath -m $(pwd)/runs/$T)
ACT_KEEP_LIVES=1 PASS=${PASS:-B} RS=${RS:-1280x720} WAIT=${WAIT:-25} SECS=${SECS:-400} bash sx.sh $T ${LV:-15} ${RP:-2:1} "$ST" \
  RECOMP_D3D_BRIDGE_DRAWLOG_TRIGGER=$D/dl.trig RECOMP_D3D_BRIDGE_SUBDUMP=@$D/sub.trig RECOMP_FB_DUMP=$D/shot RECOMP_D3D_BRIDGE_DUMP_ASKED=1 "$@"
