# step 100 item 5, closer: Gold Rush's late point (254.84 4.28 265.13), the brick decal on the wall ahead
P="254.84 4.28 265.13"
G="wait:3 x:setpos.py:254.84:4.28:265.13:2 wait:3 bg:glide2.py:TAG:30:263.13:267.13:3 wait:6"
KEEP=1 LV=19 RP=1:1 bash dd.sh r1 H4 "${G//TAG/r1} sub:30 wait:14" > runs/t5b_r1.txt 2>&1
KEEP=1 LV=19 RP=1:1 bash dd.sh r2 H4 "${G//TAG/r2} dl:20 wait:40" RECOMP_FRAMERATE=60 > runs/t5b_r2.txt 2>&1
KEEP=1 LV=19 RP=1:1 bash dd.sh r3 H4 "${G//TAG/r3} sub:30 wait:14" RECOMP_D3D_BRIDGE_BAKE=0 > runs/t5b_r3.txt 2>&1
KEEP=1 LV=19 RP=1:1 bash dd.sh r4 H4 "wait:3 x:setpos.py:254.84:4.28:265.13:2 wait:6 sub:20 wait:10" > runs/t5b_r4.txt 2>&1
echo DONE > runs/t5b_done.txt
