# The package audit (step 107, notes 18.110): before a package goes to anyone, check that it holds
#   1. no game file: no file whose name, or size and SHA-256, is one of the disc's (twoc\data\disc_usa.tsv, the
#      manifest), and no disc image or XBE at all;
#   2. no trace of this PC: the Windows user name in a path (\Users\<name>), the home folder's path, or the PC's
#      name, in any file's name or contents (text or binary, 8-bit and UTF-16);
#   3. no stray logs, saves or dumps (*.log, *.dmp, *.sav, *.xbx, *.pdb, *.zip, crash_twoc.muted, logs\, UDATA\,
#      UDATA_backups\, TDATA\), and no crash_twoc.ini in the Share package (its first start writes one).
# It fails loudly (red lines, exit code 1) on any of them.
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
function Fail([string]$s) { $fails.Add($s); Write-Host "FAIL: $s" -ForegroundColor Red }
function Note([string]$s) { $notes.Add($s); Write-Host "  $s" }

$target = 'unknown'
$pk = Join-Path $Dir 'PACKAGE.txt'
if (Test-Path -LiteralPath $pk) {
    $m = Select-String -LiteralPath $pk -Pattern '^Target: (\w+)' | Select-Object -First 1
    if ($m) { $target = $m.Matches[0].Groups[1].Value }
}
$share = $target -eq 'Share'
Write-Host "Auditing $Dir (target $target)"

# The manifest: names, sizes, hashes.
$names = @{}; $sizes = @{}; $hashes = @{}; $nman = 0
foreach ($l in Get-Content -LiteralPath $Manifest) {
    if ($l.StartsWith('#') -or -not $l.Trim()) { continue }
    $p = $l.Split("`t")
    $nman++
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
    if ($f.PSIsContainer) {
        if ($f.Name -in 'logs', 'UDATA', 'UDATA_backups', 'TDATA', 'captures') { Fail "a folder of logs or saves: $rel" }
        continue
    }
    if ($f.Extension -in '.log', '.dmp', '.sav', '.xbx', '.pdb', '.zip', '.map' -or $f.Name -eq 'crash_twoc.muted') { Fail "a stray log, save or dump: $rel" }
    if ($share -and $f.Name -eq 'crash_twoc.ini') { Fail "crash_twoc.ini in a Share package (the first start writes one): $rel" }
    if ($f.Name -like 'game_files.*') { Fail "a setup leftover: $rel" }
}

$size = ($files | Measure-Object Length -Sum).Sum
Write-Host ("{0} files, {1:N1} MB" -f $files.Count, ($size / 1MB))
if ($fails.Count) {
    Write-Host ""
    Write-Host "AUDIT FAILED: $($fails.Count) problem(s) in $Dir" -ForegroundColor Red
    foreach ($s in $fails) { Write-Host "  $s" -ForegroundColor Red }
    exit 1
}
Write-Host "AUDIT PASSED: $Dir" -ForegroundColor Green
exit 0
