# xl.sh TAG EXEDIR [ENV...] -- step 100 item 4: Arctic Antics, 99 wumpa poked, run forward; at the extra life 30 grabs 0.1 s apart
T=$1; E=$2; shift 2
KEEP=0 LV=7 RP=1:1 bash dd.sh $T $E "wait:8 poke:56299D:1:99 bg:xlife.py:$T:25:none.txt:0:xl:30:0.1 pad:ly=-32767 wait:6 pad: wait:6" "$@"
