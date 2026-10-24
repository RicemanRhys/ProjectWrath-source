# final106.sh -- step 106's last measurements: the final detector (C2) in ordinary play (fp106.sh: hub + five levels
# 90 s each, Smokey drives x3), its hit rate on the unfixed build (C2N: C2 with the w = 0 fix reverted) in 4 Match
# display and 4 Game (60) Smokey drives, and C2's identity against F.
cd $(dirname $(readlink -f $0))
sed -i 's/^for L in hub 7 23 19 16 26; do$/for L in hub 7 23 19 16 26; do/' fp106.sh
bash fp106.sh C2 > fp106b.out 2>&1
for i in 1 2 3 4; do FR=display AUTO=50 FULL=0 bash dr3.sh n2d$i C2N > /dev/null 2>&1; echo "n2d$i: $(grep -a '\[ROLL\] flagged' runs/n2d$i.err.log | grep -o 'guest swap [0-9]* ([0-9 of]*)[^,]*,' | tr '\n' ';')"; done > det106.out
for i in 1 2 3 4; do AUTO=50 FULL=0 bash dr3.sh n26$i C2N > /dev/null 2>&1; echo "n26$i: $(grep -a '\[ROLL\] flagged' runs/n26$i.err.log | grep -o 'guest swap [0-9]* ([0-9 of]*)[^,]*,' | tr '\n' ';')"; done >> det106.out
bash id.sh F C2 > /dev/null 2>&1; bash idcmp.sh F C2 > runs/fin106_id2.txt 2>&1
echo final done
