# v79d.sh -- step 79 item 5: identity. drive.py 150 s, bridge off and host-only (Frame rate: Game), the exe before
# step 79 (B) against the final one (F) and the final one with render_thread = on in its ini (FON): Game keeps no
# thread and the old path.
cd $(dirname $(readlink -f $0))
{ bash id.sh B F FON; echo "=== B against F"; bash idcmp.sh B F; echo "=== B against FON (render_thread = on)"; bash idcmp.sh B FON
  grep -a "render thread" ../id/ho_FON.err.log | head -3; } > runs/v79d.txt 2>&1
echo ALLDONE
