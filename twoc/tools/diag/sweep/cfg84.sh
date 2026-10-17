# cfgd.sh -- step 84 (d): configure and build the build-flag variants of the snapshot d84/twoc (the tree plus the
# profile patch) at low priority: b0 (as twoc/build: /O2 /Ob2), lt (link-time optimisation: /GL, /LTCG), av
# (/arch:AVX2), la (both). /fp stays precise (the default). Each exe and a /MAP to sw/D0, DLT, DAV, DLA.
R="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp"
SP=$(dirname $(readlink -f $0))
cd "$R/d84"
SDL=$(cygpath -m "$R/twoc/build/_deps/sdl3-src")
XR=$(cygpath -m "$R/xboxrecomp")
python -c "
p='twoc/CMakeLists.txt'; s=open(p,'rb').read()
if b'/LARGEADDRESSAWARE /MAP' not in s:
    s=s.replace(b'        /LARGEADDRESSAWARE', b'        /LARGEADDRESSAWARE /MAP', 1)
open(p,'wb').write(s)"
cfg() {  # DIR CFLAGS LINKFLAGS LIBFLAGS
  MSYS_NO_PATHCONV=1 cmake -S twoc -B $1 -G "Visual Studio 17 2022" -A x64 -DFETCHCONTENT_SOURCE_DIR_SDL3="$SDL" \
    -DXBOXRECOMP_DIR="$XR" \
    -DCMAKE_C_FLAGS_RELEASE="/O2 /Ob2 /DNDEBUG $2" -DCMAKE_CXX_FLAGS_RELEASE="/O2 /Ob2 /DNDEBUG $2" \
    -DCMAKE_EXE_LINKER_FLAGS_RELEASE="/INCREMENTAL:NO $3" -DCMAKE_STATIC_LINKER_FLAGS_RELEASE="$4" > $1.cfg.txt 2>&1 \
    && echo "configured $1" || echo "configure FAILED $1"
}
build() {  # DIR NAME
  local t0=$(date +%s)
  cmd //c "start /low /wait /b cmake --build $1 --config Release --parallel" > $1.build.txt 2>&1
  local t1=$(date +%s)
  if [ -f $1/Release/crash_twoc.exe ]; then
    mkdir -p "$SP/sw/$2"; cp $1/Release/crash_twoc.exe $1/Release/crash_twoc.map "$SP/sw/$2/"
    echo "built $2 in $((t1 - t0)) s"
  else
    echo "build FAILED $2 ($((t1 - t0)) s)"; grep -a " error " $1.build.txt | head -5
  fi
}
cfg b0 "" "" ""
cfg lt "/GL" "/LTCG" "/LTCG"
cfg av "/arch:AVX2" "" ""
cfg la "/GL /arch:AVX2" "/LTCG" "/LTCG"
build b0 D0
build lt DLT
build av DAV
build la DLA
echo ALLDONE
