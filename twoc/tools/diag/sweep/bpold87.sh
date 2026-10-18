# bpold87.sh NAME TWOC_REV XBOXRECOMP_REV -- step 87: a plain build (with a temporary /MAP) of older sources:
# twoc/src and xboxrecomp/src + tests checked out at those revisions, built by bp84.sh (NOPROF=1, copied beside this
# script in a scratch folder that holds sw/) into sw/NAME, then HEAD's put back. Needs both trees' tracked sources
# unchanged (it stops otherwise). Never while a chain runs. Step 87's "before": bpold87.sh B e2d80f5 e2d7a1b.
set -e
R="/c/Users/User/Desktop/Desktop Stuff/Claude Stuff/Crash TWOC decomp"
SP=$(dirname $(readlink -f $0))
N=$1; TR=$2; XR=$3
cd "$R"
git diff --quiet -- twoc/src && git -C xboxrecomp diff --quiet -- src tests || { echo "sources not clean"; exit 1; }
git checkout $TR -- twoc/src
git -C xboxrecomp checkout $XR -- src tests
NOPROF=1 bash "$SP/bp84.sh" $N 2>&1 | grep -E " error |built" || true
git checkout HEAD -- twoc/src
git -C xboxrecomp checkout HEAD -- src tests
git status --short; git -C xboxrecomp status --short
echo "old build $N from twoc $TR, xboxrecomp $XR"
