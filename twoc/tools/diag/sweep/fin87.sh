# fin87.sh -- step 87's last chain: identity (id.sh B F2: drive.py 150 s bridge off and host-only, Frame rate: Game,
# the step's before against its final), then the timing: P0 (before) and PB4 with the bake off and on, standing, the
# render thread on, native 4K at 240 Hz, two rounds interleaved in cast_bug's start, avalanch's and upstairs; then
# cast_bug at Frame rate 120 (2 frames a game frame), P0 and PB4 with the bake on, two rounds.
cd $(dirname $(readlink -f $0))
bash id.sh B F2 > runs/id87.txt 2>&1
bash idcmp.sh B F2 >> runs/id87.txt 2>&1
echo "IDENTITY DONE"
bash mm.sh m 2 P0 PB4:RECOMP_D3D_BRIDGE_BAKE=0 PB4:RECOMP_D3D_BRIDGE_BAKE=1
WHERE=cb bash mm.sh k 2 P0:RECOMP_FRAMERATE=120 PB4:RECOMP_D3D_BRIDGE_BAKE=1,RECOMP_FRAMERATE=120
echo FINDONE
