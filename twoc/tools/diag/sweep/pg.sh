# pg.sh -- step 84 (d): profile-guided optimisation. The instrumented build (../d84/pg) trained on cast_bug gliding,
# avalanch and the upstairs room with the render thread on (each run's counts swept by pgsw.py before it ends, since
# the game ends with TerminateProcess), then linked again with /USEPROFILE into sw/DPG.
cd $(dirname $(readlink -f $0))
R="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp"
MS="/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64"
mkdir -p PGI DPG
cp "$R/d84/pg/Release/crash_twoc.exe" PGI/ && cp "$MS/pgort140.dll" PGI/
rm -f "$R/d84/pg/Release/"*.pgc
X=$(cygpath -w $(pwd)/PGI/crash_twoc.exe)
E="--exe=$X RECOMP_HUD=edges RECOMP_RENDER_THREAD=on"
WAIT=30 bash sx.sh pgt_cb u 1 "wait:5 x:glide.py:pgt_cb:40:-3:7:3 x:pgsw.py:1" $E 2>&1 | tail -1
WAIT=30 bash sx.sh pgt_av 5 4:1 "wait:40 x:pgsw.py:2" $E 2>&1 | tail -1
PRE="upst.py pgt_up" WAIT=3 bash sx.sh pgt_up - - "wait:40 x:pgsw.py:3" $E 2>&1 | tail -1
cat runs/pgt_*.act.txt | grep -v "^[0-9.]* wait"
ls -la "$R/d84/pg/Release/"
cd "$R/d84"
SDL=$(cygpath -m "$R/twoc/build/_deps/sdl3-src"); XR=$(cygpath -m "$R/xboxrecomp")
MSYS_NO_PATHCONV=1 cmake -S twoc -B pg -DCMAKE_EXE_LINKER_FLAGS_RELEASE="/INCREMENTAL:NO /LTCG /USEPROFILE" > pg.cfg2.txt 2>&1
cmd //c "start /low /wait /b cmake --build pg --config Release --parallel" > pg.build2.txt 2>&1
grep -a "profile\|PGO\|pgc\|LNK" pg.build2.txt | head -10
cd - > /dev/null
cp "$R/d84/pg/Release/crash_twoc.exe" "$R/d84/pg/Release/crash_twoc.map" DPG/ && echo "built DPG"
echo ALLDONE
