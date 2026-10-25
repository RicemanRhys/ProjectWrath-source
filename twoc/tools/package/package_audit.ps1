# The package audit (step 107, notes 18.110): before a package goes to anyone, check that it holds
#   1. no game file: no file whose name, or size and SHA-256, is one of the disc's (twoc\data\disc_usa.tsv, the
#      manifest), and no disc image or XBE at all;
#   2. no trace of this PC: the Windows user name in a path (\Users\<name>), the home folder's path, or the PC's
#      name, in any file's name or contents (text or binary, 8-bit and UTF-16);
#   3. no stray logs, saves or dumps (*.log, *.dmp, *.sav, *.xbx, *.pdb, *.zip, *.muted, logs\, UDATA\,
#      UDATA_backups\, TDATA\), and no ini of the game's in the Share package (its first start writes one);
#   4. the licences (step 108): LICENSE.txt is there and is the MIT licence for this project's own code, and
#      THIRD_PARTY_LICENSES.txt is there. While LICENSE.txt's copyright line still holds the placeholder
#      "[NAME TO BE CHOSEN BY THE OWNER]" it WARNS loudly (yellow), without failing: the owner replaces it in
#      twoc\LICENSE (the one place) before sharing;
#   5. the audio library (step 108): xemu_apu.dll beside every game exe (ProjectWrath.exe since step 109; the
#      developer's crash_twoc.exe), and in the Share package its source
#      zip in Source\ (each zip there is unpacked and audited the same way: its own game-file, PC, licence checks).
#   6. no Microsoft runtime DLL (step 108: the runtime is linked static): no vcruntime*.dll, msvcp*.dll,
#      concrt*.dll, vccorlib*.dll, ucrtbase.dll or api-ms-win-crt-*.dll anywhere in a package.
# It fails loudly (red lines, exit code 1) on any of 1-6.
# What it audits: a package (PACKAGE.txt), the whole-program source folder (make_source_package.ps1: twoc\LICENSE)
# or the audio library's source folder (make_apu_source_package.ps1: xemu_apu_dll.c, LICENSES\, NOTICE.txt).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File twoc\tools\package\package_audit.ps1 -Dir PACKAGE
#
# The test packages (PACKAGE.txt "Target: LegionGo" or "Nitro5") carry game_files\ on purpose, for the user's own
# devices: for them the game-file check is skipped inside game_files\ (and says so), and done everywhere else.
# A file of the same size as a disc file but another SHA-256 is a coincidence, listed but not a failure.
# Windows PowerShell 5.1; ASCII only.
param([Parameter(Mandatory)][string]$Dir, [string]$Manifest)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not $Manifest) { $Manifest = Join-Path (Split-Path -Parent (Split-Path -Parent $here)) 'data\disc_usa.tsv' }
$Dir = (Resolve-Path -LiteralPath $Dir).Path
$fails = New-Object System.Collections.Generic.List[string]
$notes = New-Object System.Collections.Generic.List[string]
$warns = New-Object System.Collections.Generic.List[string]
function Fail([string]$s) { $fails.Add($s); Write-Host "FAIL: $s" -ForegroundColor Red }
function Warn([string]$s) { $warns.Add($s); Write-Host "WARNING: $s" -ForegroundColor Yellow }
function Note([string]$s) { $notes.Add($s); Write-Host "  $s" }

$target = 'unknown'
$pk = Join-Path $Dir 'PACKAGE.txt'
if (Test-Path -LiteralPath $pk) {
    $m = Select-String -LiteralPath $pk -Pattern '^Target: (\w+)' | Select-Object -First 1
    if ($m) { $target = $m.Matches[0].Groups[1].Value }
}
$share = $target -eq 'Share'
$apuSrc = $target -eq 'unknown' -and (Test-Path -LiteralPath (Join-Path $Dir 'xemu_apu_dll.c'))
if ($apuSrc) { $target = 'the audio library''s source' }
Write-Host "Auditing $Dir (target $target)"

# The manifest: names, sizes, hashes.
$names = @{}; $sizes = @{}; $hashes = @{}; $paths = @{}; $nman = 0
foreach ($l in Get-Content -LiteralPath $Manifest) {
    if ($l.StartsWith('#') -or -not $l.Trim()) { continue }
    $p = $l.Split("`t")
    $nman++
    $paths[$p[0].Replace('/', '\').ToLowerInvariant()] = [long]$p[1]
    $names[(Split-Path -Leaf $p[0].Replace('/', '\')).ToLowerInvariant()] = $p[0]
    $sizes[[long]$p[1]] = $true
    $hashes[$p[2].ToLowerInvariant()] = $p[0]
}
Write-Host ("Manifest: {0} disc files, {1} different contents ({2})" -f $nman, $hashes.Count, $Manifest)

$all = @(Get-ChildItem -LiteralPath $Dir -Recurse -Force)
$files = @($all | Where-Object { -not $_.PSIsContainer })
$gfDir = Join-Path $Dir 'game_files'
$hasGf = Test-Path -LiteralPath $gfDir
if ($hasGf -and $share) { Fail "game_files\ is in a Share package" }
$skipGf = $hasGf -and -not $share
if ($skipGf) { Note "game_files\ is there on purpose (a $target test package for the user's own devices): the game-file check is skipped inside it" }
function InGf($f) { $skipGf -and $f.FullName.StartsWith($gfDir + '\', [StringComparison]::OrdinalIgnoreCase) }

# 1. Game files.
$checked = 0; $coinc = 0
foreach ($f in $files) {
    if (InGf $f) { continue }
    $rel = $f.FullName.Substring($Dir.Length + 1)
    $leaf = $f.Name.ToLowerInvariant()
    if ($f.Extension -in '.xbe', '.iso', '.xiso') { Fail "an Xbox program or disc image: $rel" }
    if ($names.ContainsKey($leaf)) { Fail "a file named like a disc file ($($names[$leaf])): $rel" }
    if ($sizes.ContainsKey([long]$f.Length)) {
        $h = (Get-FileHash -Algorithm SHA256 -LiteralPath $f.FullName).Hash.ToLowerInvariant()
        $checked++
        if ($hashes.ContainsKey($h)) { Fail "a disc file's contents ($($hashes[$h])): $rel" }
        else { $coinc++; Note "same size as a disc file, other contents (a coincidence): $rel ($($f.Length) bytes)" }
    }
}
Write-Host ("1. game files: {0} files looked at{1}, {2} hashed for a matching size, {3} coincidences" -f
            @($files | Where-Object { -not (InGf $_) }).Count, $(if ($skipGf) { ' (outside game_files\)' } else { '' }), $checked, $coinc)

# 2. This PC: the user name in a path, the home folder, the PC's name.
$user = $env:USERNAME; $pc = $env:COMPUTERNAME; $home_ = $env:USERPROFILE
$needles = @()
if ($home_) { $needles += $home_; $needles += $home_.Replace('\', '/') }
if ($user) { $needles += "\Users\$user\"; $needles += "/Users/$user/"; $needles += "\Users\$user`""; $needles += "Users\\$user" }
$pcNeedle = if ($pc -and $pc.Length -ge 3) { $pc } else { $null }
Write-Host "2. this PC: looking for the home folder, \Users\<user name>\ and the PC's name in names and contents"
$latin = [Text.Encoding]::GetEncoding(28591)
foreach ($f in $all) {
    if (InGf $f) { continue }
    $rel = $f.FullName.Substring($Dir.Length + 1)
    if ($user -and $f.Name -like "*$user*" -and $user.Length -ge 5) { Fail "the user name in a file name: $rel" }
    if ($pcNeedle -and $f.Name -like "*$pcNeedle*") { Fail "the PC's name in a file name: $rel" }
    if ($f.PSIsContainer) { continue }
    $b = [IO.File]::ReadAllBytes($f.FullName)
    $t8 = $latin.GetString($b)
    $t16 = [Text.Encoding]::Unicode.GetString($b)
    foreach ($t in $t8, $t16) {
        foreach ($n in $needles) {
            $i = $t.IndexOf($n, [StringComparison]::OrdinalIgnoreCase)
            if ($i -ge 0) {
                $ctx = $t.Substring([Math]::Max(0, $i - 20), [Math]::Min(80, $t.Length - [Math]::Max(0, $i - 20))) -replace '[^\x20-\x7e]', '.'
                Fail "the user's folder ($n) in ${rel}: ...$ctx..."
                break
            }
        }
        if ($pcNeedle) {
            $m = [regex]::Match($t, '(?i)(?<![A-Za-z0-9_-])' + [regex]::Escape($pcNeedle) + '(?![A-Za-z0-9_-])')
            if ($m.Success) {
                $i = $m.Index
                $ctx = $t.Substring([Math]::Max(0, $i - 20), [Math]::Min(80, $t.Length - [Math]::Max(0, $i - 20))) -replace '[^\x20-\x7e]', '.'
                Fail "the PC's name ($pcNeedle) in ${rel}: ...$ctx..."
            }
        }
    }
}

# 3. Logs, saves, dumps.
Write-Host "3. logs, saves and dumps"
foreach ($f in $all) {
    if ((InGf $f) -and -not $f.PSIsContainer -and $f.Extension -notin '.sav', '.xbx', '.log') { continue }
    $rel = $f.FullName.Substring($Dir.Length + 1)
    if ((InGf $f) -and -not $f.PSIsContainer) {     # a disc file of the test package's game_files (the disc has a
        $g = $f.FullName.Substring($gfDir.Length + 1).ToLowerInvariant()   # result.log of its own): not a stray
        if ($paths.ContainsKey($g) -and $paths[$g] -eq $f.Length) { continue }
    }
    if ($f.PSIsContainer) {
        if ($f.Name -in 'logs', 'UDATA', 'UDATA_backups', 'TDATA', 'captures') { Fail "a folder of logs or saves: $rel" }
        continue
    }
    $srcZip = $f.Extension -eq '.zip' -and $rel -like 'Source\*.zip' -and $rel.Split('\').Count -eq 2   # step 108: audited below
    if (-not $srcZip -and ($f.Extension -in '.log', '.dmp', '.sav', '.xbx', '.pdb', '.zip', '.map', '.muted')) { Fail "a stray log, save or dump: $rel" }
    if ($share -and $f.Name -in 'ProjectWrath.ini', 'crash_twoc.ini') { Fail "$($f.Name) in a Share package (the first start writes one): $rel" }
    if ($f.Name -like 'game_files.*') { Fail "a setup leftover: $rel" }
}

# 4. The licences (step 108). A source package (no PACKAGE.txt) carries twoc\LICENSE instead of LICENSE.txt.
Write-Host "4. licences"
$licPath = Join-Path $Dir 'LICENSE.txt'
if ($target -eq 'unknown' -and -not (Test-Path -LiteralPath $licPath)) { $licPath = Join-Path $Dir 'twoc\LICENSE' }
if ($apuSrc) {      # the audio library's source: the LGPL's text, the notice, and this project's MIT text for its own files
    $licPath = Join-Path $Dir 'LICENSES\MIT-ProjectWrath.txt'     # step 109 (MIT-twoc.txt before)
    if (-not (Test-Path -LiteralPath $licPath)) { $licPath = Join-Path $Dir 'LICENSES\MIT-twoc.txt' }
    $lg = Join-Path $Dir 'LICENSES\LGPL-2.1.txt'
    if (-not (Test-Path -LiteralPath $lg)) { Fail "no LICENSES\LGPL-2.1.txt" }
    else {
        $lt = [IO.File]::ReadAllText($lg)
        if ($lt -notmatch 'GNU LESSER GENERAL PUBLIC LICENSE' -or $lt -notmatch 'Version 2\.1') { Fail "LICENSES\LGPL-2.1.txt isn't the LGPL 2.1" }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $Dir 'NOTICE.txt'))) { Fail "no NOTICE.txt (xemu's attribution, what was changed)" }
}
if (-not (Test-Path -LiteralPath $licPath)) { Fail "no LICENSE.txt (this project's own code: MIT)" }
else {
    $lic = [IO.File]::ReadAllText($licPath)
    if ($lic -notmatch '(?m)^MIT License\r?$' -or $lic -notmatch 'Permission is hereby granted, free of charge') {
        Fail "$(Split-Path -Leaf $licPath) doesn't hold the MIT licence"
    }
    $cr = [regex]::Match($lic, '(?m)^Copyright \(c\) [^\r\n]*')
    if (-not $cr.Success) { Fail "$(Split-Path -Leaf $licPath) has no copyright line" }
    elseif ($cr.Value -match '\[NAME TO BE CHOSEN BY THE OWNER\]') {
        Warn "the copyright line still reads `"$($cr.Value)`""
        Write-Host "  ******************************************************************************" -ForegroundColor Yellow
        Write-Host "  *  NOT READY TO SHARE: replace [NAME TO BE CHOSEN BY THE OWNER] in twoc\LICENSE *" -ForegroundColor Yellow
        Write-Host "  *  (the one place it is written), then make the packages again.              *" -ForegroundColor Yellow
        Write-Host "  ******************************************************************************" -ForegroundColor Yellow
    } else { Note "copyright line: $($cr.Value)" }
}
$tpl = if ($target -eq 'unknown') { Join-Path $Dir 'twoc\THIRD_PARTY_LICENSES.txt' } else { Join-Path $Dir 'THIRD_PARTY_LICENSES.txt' }
if (-not $apuSrc -and -not (Test-Path -LiteralPath $tpl)) { Fail "no THIRD_PARTY_LICENSES.txt" }

# 5. The audio library (step 108): xemu_apu.dll beside the exe; in the Share package its source zip in Source\,
# unpacked into a temporary folder and audited by this same script.
Write-Host "5. the audio library"
foreach ($exe in @($files | Where-Object { $_.Name -in 'ProjectWrath.exe', 'crash_twoc.exe' })) {
    $dll = Join-Path $exe.DirectoryName 'xemu_apu.dll'
    $rel = $exe.FullName.Substring($Dir.Length + 1)
    if (-not (Test-Path -LiteralPath $dll)) { Fail "no xemu_apu.dll beside $rel (the game doesn't start without it)" }
    else { Note "xemu_apu.dll beside $rel" }
}
$zips = @($files | Where-Object { $_.Extension -eq '.zip' -and $_.FullName.Substring($Dir.Length + 1) -like 'Source\*.zip' })
if ($share -and -not @($zips | Where-Object { $_.Name -like 'xemu_apu-source*.zip' }).Count) {
    Fail "no Source\xemu_apu-source*.zip (the audio library's source, which goes with its DLL)"
}
foreach ($z in $zips) {
    $zt = Join-Path $env:TEMP ('twoc_audit_' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    try {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [IO.Compression.ZipFile]::ExtractToDirectory($z.FullName, $zt)
        $top = @(Get-ChildItem -LiteralPath $zt)
        $inner = if ($top.Count -eq 1 -and $top[0].PSIsContainer) { $top[0].FullName } else { $zt }
        Write-Host "   --- $($z.Name), unpacked: ---"
        & powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Dir $inner -Manifest $Manifest
        if ($LASTEXITCODE) { Fail "the zip Source\$($z.Name) failed its own audit (above)" }
        Write-Host "   --- end of $($z.Name) ---"
    } finally { if (Test-Path -LiteralPath $zt) { Remove-Item -Recurse -Force -LiteralPath $zt } }
}

# 6. No Microsoft runtime DLL (step 108).
Write-Host "6. no Visual C++ runtime DLLs"
foreach ($f in $files) {
    if ($f.Name -match '^(vcruntime|msvcp|concrt|vccorlib).*\.dll$|^ucrtbase\.dll$|^api-ms-win-crt-.*\.dll$') {
        Fail "a Microsoft runtime DLL (the runtime is linked static, step 108): $($f.FullName.Substring($Dir.Length + 1))"
    }
}

$size = ($files | Measure-Object Length -Sum).Sum
Write-Host ("{0} files, {1:N1} MB" -f $files.Count, ($size / 1MB))
if ($fails.Count) {
    Write-Host ""
    Write-Host "AUDIT FAILED: $($fails.Count) problem(s) in $Dir" -ForegroundColor Red
    foreach ($s in $fails) { Write-Host "  $s" -ForegroundColor Red }
    exit 1
}
if ($warns.Count) {
    Write-Host ""
    Write-Host "AUDIT PASSED WITH $($warns.Count) WARNING(S): $Dir" -ForegroundColor Yellow
    foreach ($s in $warns) { Write-Host "  $s" -ForegroundColor Yellow }
    exit 0
}
Write-Host "AUDIT PASSED: $Dir" -ForegroundColor Green
exit 0
