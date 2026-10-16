# chord.sh TAG v|p|x EXEDIR [ENV...] -- step 82 item 4: the controller chord in the hub, with an SDL virtual Xbox pad
# (v), an SDL virtual PS5 pad (p) or XInput user 0's stand-in (x). Start alone (the game's pause opens and closes),
# View then Menu held 1 s (the overlay opens, Start held back), D-pad + A (Pause toggled), RB / LB (tabs), B (closes),
# View + a short Menu tap (the game gets Start after it), the chord to open and again to close, View let go first.
# Grabs at each point (RS=2560x1440 so the overlay fits); then the log's lines.
cd $(dirname $(readlink -f $0))
T=$1; M=$2; D=$(readlink -f $3); X=$(cygpath -w $D)/crash_twoc.exe; shift 3
rm -f $D/crash_twoc.ini $D/crash_twoc.muted
VP=$(cygpath -w $(pwd)/runs/$T.vpad.txt); XI=$(cygpath -w $(pwd)/runs/$T.xi.txt)
case $M in
  v) K=xbox; W=v; B0=$K; BK=${K}_back=1; BS=${K}_back=1_start=1; ST=${K}_start=1; DN=${K}_down=1; UP=${K}_up=1; A=${K}_a=1
     BB=${K}_b=1; RB=${K}_rb=1; LB=${K}_lb=1; EV="RECOMP_DEV_PAD_FILE=$VP" ;;
  p) K=ps5; W=v; B0=$K; BK=${K}_back=1; BS=${K}_back=1_start=1; ST=${K}_start=1; DN=${K}_down=1; UP=${K}_up=1; A=${K}_a=1
     BB=${K}_b=1; RB=${K}_rb=1; LB=${K}_lb=1; EV="RECOMP_DEV_PAD_FILE=$VP" ;;
  x) W=x; B0=buttons=0; BK=buttons=0x20; BS=buttons=0x30; ST=buttons=0x10; DN=buttons=0x2; UP=buttons=0x1; A=a=255; BB=b=255
     RB=white=255; LB=black=255; EV="RECOMP_DEV_XINPUT_FILE=$XI" ;;
esac
python vp.py $T $W $B0 > /dev/null
V="x:vp.py:$T:$W"
S="x:st.py:$T:showfps_1 wait:3 grab:h0 $V:$ST wait:0.15 $V:$B0 wait:2.5 grab:a_pause $V:$ST wait:0.15 $V:$B0 wait:2.5 grab:a_unpause"
S="$S $V:$BK wait:0.3 $V:$BS wait:1.0 $V:$B0 wait:1 grab:b_ovl"
S="$S $V:$DN wait:0.15 $V:$B0 wait:0.5 grab:c_dn1 $V:$UP wait:0.15 $V:$B0 wait:0.5 grab:c_up $V:$A wait:0.15 $V:$B0 wait:0.6 grab:c_a"
S="$S $V:$RB wait:0.15 $V:$B0 wait:0.6 grab:c_rb $V:$LB wait:0.15 $V:$B0 wait:0.6 grab:c_lb"
S="$S $V:$BB wait:0.15 $V:$B0 wait:1.5 grab:d_closed"
S="$S $V:$BK wait:0.2 $V:$BS wait:0.3 $V:$BK wait:0.3 $V:$B0 wait:2.5 grab:e_tap $V:$ST wait:0.15 $V:$B0 wait:2.5 grab:e_unpause"
S="$S $V:$BK wait:0.2 $V:$BS wait:1.0 $V:$B0 wait:1 grab:f_open $V:$BS wait:1.0 $V:$B0 wait:1.5 grab:f_closed"
S="$S $V:$BS wait:0.3 $V:$ST wait:0.3 $V:$B0 wait:2.5 grab:g_pause $V:$ST wait:0.15 $V:$B0 wait:2.5 grab:g_unpause"
mkdir -p runs/$T; TR=$(cygpath -m $(pwd)/runs/$T)/sub.trig
RS=2560x1440 bash sx.sh $T - - "$S" --exe=$X $EV RECOMP_D3D_BRIDGE_SUBDUMP=@$TR "$@" 2>&1 | tail -2
grep -aE "\[XPP\] (port 0 input|View|port 0 after)|\[OVL\]|\[PAD\]|\[DEV\] (XInput|virtual)" runs/$T.err.log | tail -60
echo "-- ini:"; cat $D/crash_twoc.ini 2>/dev/null | grep -v "^#"
grep -a "vp \|grab" runs/$T.act.txt | head -80 > runs/$T.steps.txt
