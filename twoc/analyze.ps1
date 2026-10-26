# Regenerate the analysis outputs (disasm, func_id, abi_analysis) for Crash: The
# Wrath of Cortex, seeded with twoc\seed_functions.json.
#
# The seed file lists function starts the detector cannot reach because they are
# only ever taken by pointer (interrupt routines, thread procs, callbacks, vtable
# entries); see the project notes, section 18.6. Run this, then twoc\lift.ps1.
# Outputs go to xboxrecomp/tools/*/output/ (gitignored). tools.disasm rewrites
# functions.json from scratch, so any name merge has to be re-applied afterwards.
#
# Usage, from anywhere:  powershell -File twoc\analyze.ps1
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Set-Location (Join-Path $repo 'xboxrecomp')
$py  = Join-Path $repo '.venv\Scripts\python.exe'
$xbe = Join-Path $repo 'game_files\default.xbe'
$seed = Join-Path $repo 'twoc\seed_functions.json'

# Step 110: the XBE parser's JSON first (<xbe stem>_analysis.json beside the XBE), which tools.disasm reads; a clean
# copy has none (tested: with it, the whole pipeline remakes src\recomp\gen byte for byte).
& $py -m tools.xbe_parser $xbe --json (Join-Path (Split-Path -Parent $xbe) 'default_analysis.json')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $py -m tools.disasm $xbe -v --force --seed-functions $seed
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $py -m tools.func_id $xbe -v
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $py -m tools.abi_analysis $xbe -v
exit $LASTEXITCODE
