# v96.sh [PARTS] -- step 96's checks (notes 18.99), exe folders beside this one: D (the tree + ../step96_match_diag.patch),
# F (the tree), B (the build before, for identity). Frame rate: Display, adaptive off, render thread on.
#   cuts  D: the intro 150 s (its camera cuts), the hub, the load into Sea Shell, its start, the pause; then
#         `cutsum.py runs/v96c.err.log`: every frame the old rule (the exact key) held and the new one doesn't
#   lv    D: Sea Shell swimming (swim.py), H2 Oh No and Coral playing, 3 swaps of sub-frames dumped twice or three
#         times each (runs/TAG/hostsub*: ../interp_blendscan.py, ../interp_oddscan.py)
#   ovl   F at 2560x1440 with the Detailed counter: the cut fraction raised to 0.995 so most frames are held (the
#         counter's and the overlay's held lines grabbed), then as it is
#   id    id.sh B F, idcmp.sh B F (edit M in both first)
cd $(dirname $(readlink -f $0))
W() { cygpath -w $(pwd)/$1/crash_twoc.exe; }
PZ="pad:buttons=0x10 wait:0.15 pad: wait:4 grab:pause pad:buttons=0x10 wait:0.15 pad: wait:3 grab:back"
E="RECOMP_FRAMERATE_ADAPTIVE=0 RECOMP_RENDER_THREAD=on RECOMP_HUD=edges"
sub() {  # TAG LEVEL ROOM:PAD STEPS: a D run with sub-frame dumps on demand
  mkdir -p runs/$1
  local TR=$(cygpath -m $(pwd)/runs/$1/sub.trig)
  WAIT=25 bash sx.sh $1 $2 $3 "$4" --exe=$(W D) $E RECOMP_DIAG_MATCHLOG=1 RECOMP_D3D_BRIDGE_SUBDUMP=@$TR \
    RECOMP_FB_DUMP=$(cygpath -m $(pwd)/runs/$1)/shot > runs/sx_$1.txt 2>&1
}
OV="wait:3 grab:cnt wait:2 grab:cnt2 x:esc.py wait:3 grab:ovl x:esc.py wait:3 grab:after"
for part in ${*:-cuts lv ovl id}; do
  case $part in
    cuts) INTRO_GRABS=5,150 WAIT=25 bash sx.sh v96c 34 2:2 "grab:lvl $PZ wait:10" --exe=$(W D) $E RECOMP_DIAG_MATCHLOG=1 \
            > runs/sx_v96c.txt 2>&1 ;;
    lv) SW="x:swim.py:v96s:50:10 sub:3 wait:3"
        sub v96s 34 2:2 "grab:lvl $SW $SW $SW"
        sub v96h 2 2:5 "grab:lvl x:play.py:v96h:30:10 sub:3 wait:3 x:play.py:v96h:30:10 sub:3 wait:3"
        sub v96o 6 4:4 "grab:lvl x:play.py:v96o:30:10 sub:3 wait:3 x:play.py:v96o:30:10 sub:3 wait:3" ;;
    ovl) RS=2560x1440 WAIT=25 bash sx.sh v96x 34 2:2 "$OV" --exe=$(W F) $E RECOMP_SHOW_FPS=2 \
           RECOMP_D3D_BRIDGE_INTERP_CUT=2,30,0.995 > runs/sx_v96x.txt 2>&1
         RS=2560x1440 WAIT=25 bash sx.sh v96n 34 2:2 "$OV" --exe=$(W F) $E RECOMP_SHOW_FPS=2 > runs/sx_v96n.txt 2>&1 ;;
    id) bash id.sh B F > runs/id96.txt 2>&1; bash idcmp.sh B F > runs/idcmp96.txt 2>&1 ;;
  esac
  echo "PART $part done"
done
echo ALLDONE
