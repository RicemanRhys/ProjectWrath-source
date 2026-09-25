# Regenerate the recompiled C for Crash: The Wrath of Cortex into src/recomp/gen/.
#
# Runs the lifter, then twoc\tools\wrap_callers.py. The analysis steps (xbe_parser, disasm, func_id,
# abi_analysis) must already have produced their output under
# xboxrecomp/tools/*/output/; see the project notes, section 14.2, for those
# commands. The generated code is gitignored: never commit it.
#
# Usage, from anywhere:  powershell -File twoc\lift.ps1
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Set-Location (Join-Path $repo 'xboxrecomp')
& (Join-Path $repo '.venv\Scripts\python.exe') -m tools.recomp `
    (Join-Path $repo 'game_files\default.xbe') `
    --all --split 1000 `
    --gen-dir (Join-Path $repo 'twoc\src\recomp\gen') `
    --game-name 'Crash TWoC' `
    --exclude-manual (Join-Path $repo 'twoc\src\recomp_manual.c')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# The lifter names every reference to a wrapped function sub_X_gen, so callers
# would skip the wrapper; point them back at sub_X (notes 18.32).
& (Join-Path $repo '.venv\Scripts\python.exe') (Join-Path $repo 'twoc\tools\wrap_callers.py')
exit $LASTEXITCODE
