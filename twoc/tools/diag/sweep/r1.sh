# r1.sh -- step 82 item 1: the adaptive drop test with the render thread, at 240 Hz, native 4K, 16:9, HUD at the edges,
# Frame rate: Display, adaptive on. N = the new test, O = the old one (same build otherwise). 40 s measured.
cd $(dirname $(readlink -f $0))
N=--exe=$(cygpath -w $(pwd)/N/crash_twoc.exe)
O=--exe=$(cygpath -w $(pwd)/O/crash_twoc.exe)
TH="RECOMP_RENDER_THREAD=on"
export STEPS="wait:40"
bash m79.sh n_cb cb $N $TH
bash m79.sh n_cbb cb $N $TH RECOMP_DEV_GAME_BURN_MS=12
bash m79.sh o_cbb cb $O $TH RECOMP_DEV_GAME_BURN_MS=12
bash m79.sh n_cbj cb $N $TH RECOMP_DEV_GAME_BURN_MS=8,8
bash m79.sh o_cbj cb $O $TH RECOMP_DEV_GAME_BURN_MS=8,8
bash m79.sh n_cbo cb $N RECOMP_RENDER_THREAD=off
bash m79.sh n_av av $N $TH
bash m79.sh n_up up $N $TH
# a new plan mid-run (Frame rate 120, then Display): the restart lines
mkdir -p runs/n_pl
TR=$(cygpath -m $(pwd)/runs/n_pl)/sub.trig
STEPS="wait:12 x:st.py:n_pl:framerate_120 wait:12 x:st.py:n_pl:framerate_display wait:12" bash m79.sh n_pl cb $N $TH RECOMP_DEV_GAME_BURN_MS=0 RECOMP_D3D_BRIDGE_SUBDUMP=@$TR
python iv82.py n_cb n_cbb o_cbb n_cbj o_cbj n_cbo n_av n_up n_pl > runs/r1.txt 2>&1
echo r1 finished
