# fetch_toolkit.ps1 [-Dir xboxrecomp] -- makes the xboxrecomp toolkit beside twoc\ as Project Wrath builds it:
# upstream (https://github.com/sp00nznet/xboxrecomp, MIT; its APU files LGPL-2.1, from xemu) at the commit pinned in
# PINNED.txt, with this project's local commits applied from patches\ (git am, in order). Needs Git. Nothing of the
# game is in the toolkit or the patches.
param([string]$Dir)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$root = Split-Path -Parent $here
if (-not $Dir) { $Dir = Join-Path $root 'xboxrecomp' }
$pinned = Get-Content -LiteralPath (Join-Path $here 'PINNED.txt')
$url = ($pinned | Where-Object { $_ -like 'upstream: *' }) -replace '^upstream: ', ''
$commit = (($pinned | Where-Object { $_ -like 'pinned commit*' }) -split ': ', 2)[1].Split(' ')[0]
if (Test-Path -LiteralPath $Dir) { throw "$Dir exists already: delete it first, or pass -Dir" }
& git clone --no-checkout $url $Dir
if ($LASTEXITCODE) { throw "git clone failed" }
Push-Location $Dir
try {
    & git checkout -q -b projectwrath $commit
    if ($LASTEXITCODE) { throw "the pinned commit $commit isn't in $url" }
    $patches = Get-ChildItem -LiteralPath (Join-Path $here 'patches') -Filter '*.patch' | Sort-Object Name
    if ($patches) {
        & git -c user.name=fetch_toolkit -c user.email=fetch_toolkit am --keep-cr --committer-date-is-author-date @($patches.FullName)
        if ($LASTEXITCODE) { & git am --abort; throw "a patch didn't apply: see above" }
    }
    Write-Output ("xboxrecomp ready in {0}: upstream {1} + {2} patches (branch projectwrath)" -f $Dir, $commit.Substring(0, 7), $patches.Count)
} finally { Pop-Location }
