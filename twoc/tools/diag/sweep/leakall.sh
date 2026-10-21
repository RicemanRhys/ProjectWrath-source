# leakall.sh -- step 99 part 4 item 3: the endurance run, 20 entries of Sea Shell and of cast_bug, 30 + 30 min standing
cd "$(dirname "$0")"
bash leak.sh lk_end endur 60
bash leak.sh lk_ss enter 34 20
bash leak.sh lk_cb enter cb 20
bash leak.sh lk_st stand cb 30
echo LEAKALL DONE
