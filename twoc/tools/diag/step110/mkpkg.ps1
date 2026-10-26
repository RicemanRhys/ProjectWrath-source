# mkpkg.ps1 -Dir DIR -From EXEDIR [-Name ProjectWrath.exe] [-NoGameFiles]
# Step 110 (notes 18.113): a package-like test folder: DIR\<Name> (a copy of EXEDIR\crash_twoc.exe), xemu_apu.dll,
# and game_files\ with default.xbe copied and Crashdat as a junction to the repo's (read only by the game; delete the
# folder with rmjunc.ps1 or `cmd /c rmdir` on the junction first, never a recursive delete through it).
param([Parameter(Mandatory)][string]$Dir, [Parameter(Mandatory)][string]$From, [string]$Name = 'ProjectWrath.exe',
      [switch]$NoGameFiles)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
New-Item -ItemType Directory -Force -Path $Dir | Out-Null
Copy-Item -LiteralPath (Join-Path $From 'crash_twoc.exe') -Destination (Join-Path $Dir $Name) -Force
Copy-Item -LiteralPath (Join-Path $From 'xemu_apu.dll') -Destination (Join-Path $Dir 'xemu_apu.dll') -Force
if (-not $NoGameFiles) {
    $gf = Join-Path $Dir 'game_files'
    New-Item -ItemType Directory -Force -Path $gf | Out-Null
    Copy-Item -LiteralPath (Join-Path $repo 'game_files\default.xbe') -Destination (Join-Path $gf 'default.xbe') -Force
    $j = Join-Path $gf 'Crashdat'
    if (-not (Test-Path -LiteralPath $j)) {
        New-Item -ItemType Junction -Path $j -Target (Join-Path $repo 'game_files\Crashdat') | Out-Null
    }
}
Write-Output "made $Dir ($Name)"
