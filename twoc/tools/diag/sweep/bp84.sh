# bp.sh NAME [PATCH...] -- build the working tree plus ../step84_game_profile.patch (and any more patches, repo
# paths) with a temporary /MAP into sw/NAME (exe + map), then take the patches and /MAP out again. NOPROF=1: without
# the profile patch (a plain build, still with its map).
set -e
R="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp"
SP=$(dirname $(readlink -f $0))
N=$1; shift
cd "$R"
PATCHES="$@"
[ "${NOPROF:-0}" = 1 ] || PATCHES="twoc/tools/diag/step84_game_profile.patch $PATCHES"
for p in $PATCHES; do git apply $p; done
cp twoc/CMakeLists.txt "$SP/CMakeLists.keep"
python -c "
p='twoc/CMakeLists.txt'; s=open(p,'rb').read()
s=s.replace(b'        /LARGEADDRESSAWARE', b'        /LARGEADDRESSAWARE /MAP', 1)
open(p,'wb').write(s)"
ok=0
(cd twoc && cmd //c "start /low /wait /b cmake --build build --config Release --parallel" 2>&1 | grep -E " error |warning C|crash_twoc.vcxproj ->") && ok=1 || true
cp "$SP/CMakeLists.keep" twoc/CMakeLists.txt
for p in $(echo $PATCHES | tr ' ' '\n' | tac); do git apply -R $p; done
mkdir -p "$SP/sw/$N"
cp twoc/build/Release/crash_twoc.exe twoc/build/Release/crash_twoc.map "$SP/sw/$N/"
rm -f twoc/build/Release/crash_twoc.map
git status --short
echo built $N ok=$ok
