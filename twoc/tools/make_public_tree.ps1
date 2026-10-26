# make_public_tree.ps1 -- the public source tree of Project Wrath, exported from this private repository (step 110,
# notes 18.113; what may go in: twoc\docs\PUBLIC_REPO.md).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File twoc\tools\make_public_tree.ps1 -Out DIR [-Force]
#       [-IncludeSeeds] [-Commit HEAD] [-Toolkit DIR]
#
# From the private repository's committed state (-Commit, default HEAD; `git archive`, so nothing untracked, never
# the generated code twoc\src\recomp\gen, the game files or the notes' working files) it makes DIR with:
#   - only the allowed files ($allow below): twoc's hand-written source, build files, third-party code, the setup's
#     disc manifest (names, sizes and SHA-256: no game data), the regeneration scripts, the packaging scripts for the
#     Share package; never docs\option_b (a disassembly listing of the game's D3D library and its tables),
#     tools\diag (the developer harness: game memory addresses, level and position tables), tools\capturekit, the
#     test packages' targets, the notes file ("crash claude md"), or seed_functions.json (52 function addresses in
#     the game: only with -IncludeSeeds; without them the public tree can't regenerate the code exactly);
#   - the public files kept in twoc\public\ (README.md, CONTRIBUTING.md, SECURITY.md, .gitignore, docs\) at the top,
#     with LICENSE, NOTICE and THIRD_PARTY_LICENSES.txt copied from twoc\, and .github\ (the issue forms);
#   - toolkit\: the xboxrecomp commit the work is based on (upstream's, pinned) and this project's local commits on
#     top of it as patch files, their author lines set to the public identity; fetch_toolkit.ps1 makes xboxrecomp\
#     from them (a fork, a submodule or these patches all start from the same two facts);
#   - a FRESH git repository: one commit, "Initial public release of Project Wrath", author and committer
#     "RicemanRhys <NOREPLY-ADDRESS-TO-BE-FILLED>" (the owner fills the address in), no remote; nothing is pushed.
# Then it runs audit_public_tree.ps1 on DIR and fails if the audit does. Re-runnable for later releases: DIR is
# replaced only with -Force, and only if it holds this script's own marker file (.public-tree-export).
param(
    [Parameter(Mandatory)][string]$Out,
    [switch]$Force,
    [switch]$IncludeSeeds,
    [string]$Commit = 'HEAD',
    [string]$Toolkit
)
$ErrorActionPreference = 'Stop'
$twoc = Split-Path -Parent $PSScriptRoot
$repo = Split-Path -Parent $twoc
if (-not $Toolkit) { $Toolkit = Join-Path $repo 'xboxrecomp' }
$identityName = 'RicemanRhys'
$identityMail = 'NOREPLY-ADDRESS-TO-BE-FILLED'
$marker = '.public-tree-export'

# The private repository's files that may be published (paths in the repository, '/' separators; a trailing '/'
# takes the folder). Everything else stays private. twoc\docs\PUBLIC_REPO.md says why for each group.
$allow = @(
    'twoc/.gitignore', 'twoc/CMakeLists.txt', 'twoc/ISSUES_URL.txt', 'twoc/VERSION.txt', 'twoc/LICENSE',
    'twoc/NOTICE', 'twoc/THIRD_PARTY_LICENSES.txt', 'twoc/analyze.ps1', 'twoc/lift.ps1', 'twoc/run_twoc.ps1',
    'twoc/apu_dll/', 'twoc/data/disc_usa.tsv', 'twoc/src/', 'twoc/third_party/',
    'twoc/tools/build_info.cmake', 'twoc/tools/lift_flag_join.py', 'twoc/tools/lift_frndint.py',
    'twoc/tools/wrap_callers.py', 'twoc/tools/make_public_tree.ps1', 'twoc/tools/audit_public_tree.ps1',
    'twoc/tools/package/make_package.ps1', 'twoc/tools/package/make_package.bat',
    'twoc/tools/package/package_audit.ps1', 'twoc/tools/package/make_apu_source_package.ps1',
    'twoc/tools/package/third_party_licenses.py', 'twoc/tools/package/LICENSE.package.txt', 'twoc/tools/package/readme_txt.py',
    'twoc/tools/package/make_release.ps1', 'twoc/tools/package/release_text/', 'twoc/tools/package/files/',
    'twoc/tools/package/targets/Share/', '.github/'
)
if ($IncludeSeeds) { $allow += 'twoc/seed_functions.json' }
# Never, even inside an allowed folder.
$deny = @('twoc/src/recomp/gen/', 'twoc/docs/option_b/', 'twoc/tools/diag/', 'twoc/tools/capturekit/')

function Allowed([string]$p) {
    foreach ($d in $deny) { if ($p.StartsWith($d)) { return $false } }
    foreach ($a in $allow) {
        if ($a.EndsWith('/')) { if ($p.StartsWith($a)) { return $true } } elseif ($p -eq $a) { return $true }
    }
    return $false
}

# ── The output folder ─────────────────────────────────────────────────────────────────────────────────────────────
if (Test-Path -LiteralPath $Out) {
    if (-not $Force) { throw "$Out exists: -Force replaces it (only one this script made)" }
    if (-not (Test-Path -LiteralPath (Join-Path $Out $marker))) { throw "$Out wasn't made by this script (no $marker): left alone" }
    Get-ChildItem -LiteralPath $Out -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue |
        ForEach-Object { throw "a link inside $Out ($($_.FullName)): left alone" }
    Remove-Item -LiteralPath $Out -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Resolve-Path -LiteralPath $Out).Path

# ── The committed files ───────────────────────────────────────────────────────────────────────────────────────────
$sha = (& git -C $repo rev-parse $Commit).Trim()
$short = $sha.Substring(0, 7)
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("pw-public-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    $tar = Join-Path $tmp 'src.tar'
    & git -C $repo archive --format=tar -o $tar $sha
    if ($LASTEXITCODE) { throw "git archive failed" }
    $x = Join-Path $tmp 'x'
    New-Item -ItemType Directory -Path $x | Out-Null
    & tar -xf $tar -C $x
    if ($LASTEXITCODE) { throw "tar failed" }
    $files = & git -C $repo ls-tree -r --name-only $sha
    $kept = 0; $left = 0
    foreach ($f in $files) {
        if (-not (Allowed $f)) { $left++; continue }
        $src = Join-Path $x ($f -replace '/', '\')
        $dst = Join-Path $Out ($f -replace '/', '\')
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
        Copy-Item -LiteralPath $src -Destination $dst
        $kept++
    }
    Write-Output "private repository $short : $kept files exported, $left left private"

    # ── The public files at the top ───────────────────────────────────────────────────────────────────────────────
    $pub = Join-Path $x 'twoc\public'
    if (-not (Test-Path -LiteralPath $pub)) { throw "twoc\public isn't in $short (commit the public files first)" }
    Copy-Item -Path (Join-Path $pub '*') -Destination $Out -Recurse -Force
    if (Test-Path -LiteralPath (Join-Path $Out 'gitignore')) {
        Move-Item -LiteralPath (Join-Path $Out 'gitignore') -Destination (Join-Path $Out '.gitignore') -Force
    }
    foreach ($f in 'LICENSE', 'NOTICE', 'THIRD_PARTY_LICENSES.txt', 'VERSION.txt') {
        $p = Join-Path $x "twoc\$f"
        if (Test-Path -LiteralPath $p) { Copy-Item -LiteralPath $p -Destination (Join-Path $Out $f) -Force }
    }
} finally {
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

# ── The toolkit: the pinned upstream commit and the local commits as patches ────────────────────────────────────────
$tk = Join-Path $Out 'toolkit'
$pd = Join-Path $tk 'patches'
New-Item -ItemType Directory -Force -Path $pd | Out-Null
$upstreamUrl = (& git -C $Toolkit remote get-url origin).Trim()
$tkHead = (& git -C $Toolkit rev-parse HEAD).Trim()
$base = (& git -C $Toolkit merge-base origin/main HEAD).Trim()
$baseLine = (& git -C $Toolkit log -1 --format='%H %cI %s' $base).Trim()
& git -C $Toolkit format-patch --quiet --no-signature --zero-commit -N -o $pd "$base..$tkHead" | Out-Null
if ($LASTEXITCODE) { throw "git format-patch failed" }
$n = 0
foreach ($p in Get-ChildItem -LiteralPath $pd -Filter '*.patch' | Sort-Object Name) {
    $t = [IO.File]::ReadAllText($p.FullName)
    # the author line: the public identity, never the private one
    $t = [regex]::Replace($t, '(?m)^From: .*$', "From: $identityName <$identityMail>")
    [IO.File]::WriteAllText($p.FullName, $t, (New-Object Text.UTF8Encoding($false)))
    $n++
}
$sums = Get-ChildItem -LiteralPath $pd -Filter '*.patch' | Sort-Object Name |
    ForEach-Object { "{0}  {1}" -f (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLower(), $_.Name }
$pinned = @(
    "upstream: $upstreamUrl",
    "pinned commit (upstream's, the base of this project's work): $baseLine",
    "local commits on top of it: $n (patches\*.patch, in order; applied with git am)",
    "the result: the toolkit as Project Wrath builds it (its tree equals the private toolkit's commit $($tkHead.Substring(0,7)))",
    "",
    "SHA-256 of the patches:"
) + $sums
[IO.File]::WriteAllLines((Join-Path $tk 'PINNED.txt'), $pinned)
Write-Output "toolkit: upstream $($base.Substring(0,7)) + $n patches"

# ── A fresh repository: one commit, the placeholder identity, no remote ────────────────────────────────────────────
[IO.File]::WriteAllText((Join-Path $Out $marker), "made by twoc\tools\make_public_tree.ps1 from $short`r`n")
Push-Location $Out
try {
    & git init -q -b main
    if ($LASTEXITCODE) { & git init -q; & git checkout -q -b main }
    & git config user.name $identityName
    & git config user.email $identityMail
    & git config core.autocrlf false
    & git add -A
    & git rm -q --cached $marker
    $env:GIT_AUTHOR_NAME = $identityName; $env:GIT_AUTHOR_EMAIL = $identityMail
    $env:GIT_COMMITTER_NAME = $identityName; $env:GIT_COMMITTER_EMAIL = $identityMail
    & git commit -q -m "Initial public release of Project Wrath"
    if ($LASTEXITCODE) { throw "git commit failed" }
    $remotes = & git remote
    if ($remotes) { throw "a remote is set: $remotes" }
    Write-Output ("public tree: {0} files in one commit {1}, no remote" -f (& git ls-files | Measure-Object).Count,
                  (& git rev-parse --short HEAD))
} finally {
    Remove-Item Env:GIT_AUTHOR_NAME, Env:GIT_AUTHOR_EMAIL, Env:GIT_COMMITTER_NAME, Env:GIT_COMMITTER_EMAIL -ErrorAction SilentlyContinue
    Pop-Location
}

# ── The audit ─────────────────────────────────────────────────────────────────────────────────────────────────────
$auditArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'audit_public_tree.ps1'),
               '-Dir', $Out, '-Manifest', (Join-Path $twoc 'data\disc_usa.tsv'))
if ($IncludeSeeds) { $auditArgs += '-AllowSeeds' }      # (a switch through -File is passed by name only)
& powershell @auditArgs
if ($LASTEXITCODE) { throw "the audit failed: nothing to publish from $Out" }
