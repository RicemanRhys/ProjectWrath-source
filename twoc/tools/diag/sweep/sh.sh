# sh.sh TAG... -- contact sheet per tag: sheets/TAG.png (every png of the run, in name order but menu/name/intro last)
cd $(dirname $(readlink -f $0))
PY="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp/.venv/Scripts/python.exe"
for T in "$@"; do
  F=$(ls runs/${T}_*.png 2>/dev/null | grep -v "_menu_\|_name_\|_intro0" )
  [ -z "$F" ] && { echo "no pngs $T"; continue; }
  "$PY" cs.py sheets/$T.png ${COLS:-5} ${W:-300} $F
done
