# make_release.ps1 -- a release of Project Wrath, prepared in a scratch folder (step 110, Part 3, notes 18.113).
# It uploads nothing and changes nothing outside -Out.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File twoc\tools\package\make_release.ps1 -Out DIR
#       [-Force] [-NoPgo] [-Commit HEAD] [-GameFiles <repo>\game_files]
#
# The version is twoc\VERSION.txt's at -Commit (the one place it is written; the build puts the same into the exe's
# file version and the About tab). In DIR it makes:
#   ProjectWrath-<version>-win64.zip   the Share package (no game files) in a folder ProjectWrath-<version>
#   xemu_apu-source_*.zip              the audio DLL's complete source (LGPL), as in the zip's Source\
#   SHA256SUMS.txt                     both zips' SHA-256
#   RELEASE_NOTES.md                   from release_text\RELEASE_NOTES.template.md, "What's new" from
#                                      release_text\CHANGES.md's section for the version
#   release.txt                        what was built from what (commits, the generated code's hash, the audit)
#   work\                              the clean checkout and its builds (kept for a look; delete it when done)
# How:
#   1. a clean checkout: `git clone --no-hardlinks` of this repository at -Commit and of the toolkit
#      (xboxrecomp\) at its current commit, into DIR\work\src (nothing of this repository's working tree, its
#      builds or its untracked files is used, but for the two below);
#   2. the generated code (twoc\src\recomp\gen: never in git) copied from this working tree (lift.ps1 remakes it from
#      game_files\default.xbe; tested byte-identical from a clean copy, notes 18.113);
#   3. make_package.ps1 -Target Share -Pgo in the clean checkout (its own build_pgo: instrumented, trained through
#      the private harness on -GameFiles' disc files, linked again with the counts; /MT, the audio DLL beside it),
#      or with -NoPgo a plain Release build there; SDL from this tree's build\_deps (no download);
#   4. the exe's file version checked against VERSION.txt; the folder zipped; the zip unpacked and audited with
#      package_audit.ps1 (the two placeholders, the copyright name and ISSUES_URL, may warn); the sums and the notes.
param(
    [Parameter(Mandatory)][string]$Out,
    [switch]$Force,
    [switch]$NoPgo,
    [string]$Commit = 'HEAD',
    [string]$GameFiles
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$twoc = Split-Path -Parent (Split-Path -Parent $here)
$repo = Split-Path -Parent $twoc
$marker = '.release-folder'
if (-not $GameFiles) { $GameFiles = Join-Path $repo 'game_files' }
$t0 = Get-Date

# ── The version ───────────────────────────────────────────────────────────────────────────────────────────────────
$sha = (& git -C $repo rev-parse $Commit).Trim()
$verText = (& git -C $repo show "${sha}:twoc/VERSION.txt") -join "`n"
$ver = ($verText -split "`n" | Where-Object { $_ -match '^\s*\d+\.\d+\.\d+\s*$' } | Select-Object -First 1)
if (-not $ver) { throw "twoc\VERSION.txt at $sha has no MAJOR.MINOR.PATCH line" }
$ver = $ver.Trim()
$name = "ProjectWrath-$ver"
Write-Output "Project Wrath $ver from $($sha.Substring(0, 7))"

# ── The output folder ─────────────────────────────────────────────────────────────────────────────────────────────
if (Test-Path -LiteralPath $Out) {
    if (-not $Force) { throw "$Out exists: -Force replaces it (only a folder this script made)" }
    if (-not (Test-Path -LiteralPath (Join-Path $Out $marker))) { throw "$Out wasn't made by this script: left alone" }
    Get-ChildItem -LiteralPath $Out -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue |
        ForEach-Object { cmd /c rmdir "$($_.FullName)" }
    Remove-Item -LiteralPath $Out -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Resolve-Path -LiteralPath $Out).Path
[IO.File]::WriteAllText((Join-Path $Out $marker), "made by twoc\tools\package\make_release.ps1`r`n")
$work = Join-Path $Out 'work'
$src = Join-Path $work 'src'
New-Item -ItemType Directory -Force -Path $work | Out-Null

# ── 1. The clean checkout ─────────────────────────────────────────────────────────────────────────────────────────
$tkHead = (& git -C (Join-Path $repo 'xboxrecomp') rev-parse HEAD).Trim()
& git clone --quiet --no-hardlinks --no-checkout $repo $src
if ($LASTEXITCODE) { throw 'git clone (the repository) failed' }
& git -C $src -c advice.detachedHead=false checkout --quiet $sha
if ($LASTEXITCODE) { throw "checkout $sha failed" }
& git clone --quiet --no-hardlinks --no-checkout (Join-Path $repo 'xboxrecomp') (Join-Path $src 'xboxrecomp')
if ($LASTEXITCODE) { throw 'git clone (the toolkit) failed' }
& git -C (Join-Path $src 'xboxrecomp') -c advice.detachedHead=false checkout --quiet $tkHead
if ($LASTEXITCODE) { throw "toolkit checkout $tkHead failed" }
Write-Output "clean checkout: $src (twoc $($sha.Substring(0, 7)), xboxrecomp $($tkHead.Substring(0, 7)))"

# ── 2. The generated code ─────────────────────────────────────────────────────────────────────────────────────────
$gen = Join-Path $twoc 'src\recomp\gen'
if (-not (Test-Path -LiteralPath (Join-Path $gen 'recomp_funcs.h'))) { throw "no generated code in ${gen}: run twoc\analyze.ps1 and twoc\lift.ps1 first" }
$genDst = Join-Path $src 'twoc\src\recomp\gen'
New-Item -ItemType Directory -Force -Path $genDst | Out-Null
Copy-Item -Path (Join-Path $gen '*') -Destination $genDst
$genSums = Get-ChildItem -LiteralPath $genDst -File | Sort-Object Name |
    ForEach-Object { "{0}  {1}" -f (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLower(), $_.Name }
$genHash = [BitConverter]::ToString((New-Object Security.Cryptography.SHA256Managed).ComputeHash(
    [Text.Encoding]::UTF8.GetBytes(($genSums -join "`n")))).Replace('-', '').ToLower()
Write-Output "generated code: $(@($genSums).Count) files copied (their hashes' hash $($genHash.Substring(0, 16)))"

# ── 3. The package ────────────────────────────────────────────────────────────────────────────────────────────────
$sdl = Join-Path $twoc 'build\_deps\sdl3-src'
$pkgDir = Join-Path $work $name
$mp = Join-Path $src 'twoc\tools\package\make_package.ps1'
$mpArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $mp, '-Target', 'Share', '-Out', $pkgDir,
            '-Venv', (Join-Path $repo '.venv'))
if (Test-Path -LiteralPath (Join-Path $sdl 'CMakeLists.txt')) { $mpArgs += @('-SdlSource', $sdl) }
if ($NoPgo) {
    $b = Join-Path $src 'twoc\build'
    $cfg = @('-S', (Join-Path $src 'twoc'), '-B', $b, '-G', 'Visual Studio 17 2022', '-A', 'x64')
    if (Test-Path -LiteralPath (Join-Path $sdl 'CMakeLists.txt')) { $cfg += "-DFETCHCONTENT_SOURCE_DIR_SDL3=$sdl" }
    & cmake @cfg | Out-File (Join-Path $work 'build.txt')
    if ($LASTEXITCODE) { throw 'cmake configure failed' }
    & cmake --build $b --config Release --parallel | Out-File -Append (Join-Path $work 'build.txt')
    if ($LASTEXITCODE) { throw "the build failed: $work\build.txt" }
    $mpArgs += @('-Exe', (Join-Path $b 'Release\crash_twoc.exe'))
} else {
    $mpArgs += @('-Pgo', '-GameFiles', $GameFiles)
}
& powershell @mpArgs *> (Join-Path $work 'make_package.txt')
if ($LASTEXITCODE) { Get-Content (Join-Path $work 'make_package.txt') | Select-Object -Last 20; throw "make_package.ps1 failed: $work\make_package.txt" }
Write-Output "package: $pkgDir"

# ── 4. The version, the zip, the audit, the sums, the notes ───────────────────────────────────────────────────────
$exe = Join-Path $pkgDir 'ProjectWrath.exe'
$vi = (Get-Item -LiteralPath $exe).VersionInfo
if ($vi.ProductVersion -ne $ver -or -not $vi.FileVersion.StartsWith("$ver,")) {
    throw "the exe's version ($($vi.ProductVersion) / $($vi.FileVersion)) isn't VERSION.txt's $ver"
}
Write-Output "exe file version: $($vi.FileVersion); product version $($vi.ProductVersion)"
$zip = Join-Path $Out "$name-win64.zip"
Compress-Archive -LiteralPath $pkgDir -DestinationPath $zip -CompressionLevel Optimal
$audDir = Join-Path $work 'unzipped'
Expand-Archive -LiteralPath $zip -DestinationPath $audDir
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $src 'twoc\tools\package\package_audit.ps1') `
    -Dir (Join-Path $audDir $name) *> (Join-Path $work 'audit.txt')
$auditOk = $LASTEXITCODE -eq 0
$auditLast = (Get-Content (Join-Path $work 'audit.txt') | Where-Object { $_ -match 'AUDIT' } | Select-Object -Last 1)
if (-not $auditOk) { Get-Content (Join-Path $work 'audit.txt'); throw "the audit failed on the zip's contents" }
$apu = Get-ChildItem -LiteralPath (Join-Path $pkgDir 'Source') -Filter 'xemu_apu-source_*.zip' | Select-Object -First 1
Copy-Item -LiteralPath $apu.FullName -Destination $Out
$sums = foreach ($f in @($zip, (Join-Path $Out $apu.Name))) {
    "{0}  {1}" -f (Get-FileHash -Algorithm SHA256 -LiteralPath $f).Hash.ToLower(), (Split-Path -Leaf $f)
}
[IO.File]::WriteAllLines((Join-Path $Out 'SHA256SUMS.txt'), [string[]]$sums)

$changes = Get-Content -LiteralPath (Join-Path $src 'twoc\tools\package\release_text\CHANGES.md')
$in = $false; $body = @()
foreach ($l in $changes) {
    if ($l -match '^## ') { $in = ($l.Trim() -eq "## $ver"); continue }
    if ($in) { $body += $l }
}
$body = ($body -join "`n").Trim()
if (-not $body) { $body = "- (No notes for $ver in release_text\CHANGES.md.)" }
$pkgTxt = Get-Content -LiteralPath (Join-Path $pkgDir 'PACKAGE.txt')
function Hash([string]$p) { (Get-FileHash -Algorithm SHA256 -LiteralPath $p).Hash.ToLower() }
$notes = [IO.File]::ReadAllText((Join-Path $src 'twoc\tools\package\release_text\RELEASE_NOTES.template.md'))
$fill = @{
    '{{VERSION}}' = $ver; '{{ZIP}}' = (Split-Path -Leaf $zip); '{{ZIP_SHA256}}' = (Hash $zip);
    '{{SIZE_MB}}' = ('{0:N1}' -f ((Get-Item -LiteralPath $zip).Length / 1MB)); '{{CHANGES}}' = $body;
    '{{EXE_SHA256}}' = (Hash $exe); '{{DLL_SHA256}}' = (Hash (Join-Path $pkgDir 'xemu_apu.dll'));
    '{{COMMIT}}' = "Project Wrath $($sha.Substring(0, 7))"; '{{TOOLKIT}}' = "xboxrecomp $($tkHead.Substring(0, 7))";
    '{{APU_ZIP}}' = $apu.Name
}
foreach ($k in $fill.Keys) { $notes = $notes.Replace($k, $fill[$k]) }
[IO.File]::WriteAllText((Join-Path $Out 'RELEASE_NOTES.md'), $notes, (New-Object Text.UTF8Encoding($false)))

$rel = @(
    "Project Wrath $ver, prepared $(Get-Date -Format 'yyyy-MM-dd HH:mm') (nothing uploaded)",
    "from: twoc $sha, xboxrecomp $tkHead (clean clones in work\src)",
    "generated code: copied from this working tree, $(@($genSums).Count) files, their hashes' hash $genHash",
    "build: $(if ($NoPgo) { 'Release, no PGO' } else { 'PGO (make_package.ps1 -Pgo in the clean checkout)' })",
    "exe file version: $($vi.FileVersion); product version: $($vi.ProductVersion)",
    "zip: $(Split-Path -Leaf $zip), $((Get-Item -LiteralPath $zip).Length) bytes",
    "audit of the zip's contents: $auditLast",
    "took $([int]((Get-Date) - $t0).TotalMinutes) min", "", 'SHA256SUMS.txt:') + $sums + @('', 'PACKAGE.txt:') + $pkgTxt
[IO.File]::WriteAllLines((Join-Path $Out 'release.txt'), [string[]]$rel)
$rel | Select-Object -First 9
