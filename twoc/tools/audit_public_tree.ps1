# audit_public_tree.ps1 -Dir DIR [-Manifest twoc\data\disc_usa.tsv] [-AllowSeeds] -- the public tree's audit (step
# 110, notes 18.113): run by make_public_tree.ps1 on its export, and on its own on anything about to be published.
#
# FAILS (exit 1) on:
#   1. game content: an .xbe, .iso, .xiso, .xbx or .sav file; a file named as one of the disc's files, or with the
#      size and SHA-256 of one (the manifest); the generated code (src\recomp\gen, recomp_funcs.h, recomp_types.h);
#   2. disassembly or listings: docs\option_b, d3d99_asm.txt, table.json, seed_functions.json (unless -AllowSeeds),
#      a file with more than 20 x86 instruction lines, or more than 5 runs of 8+ hex bytes;
#   3. anything over 5 MB;
#   4. personal details: the owner's Windows user folder (in its Windows, Git Bash and forward-slash forms), the
#      private repository's folder names, the owner's e-mail addresses or their mail domain, and any drive path into
#      a user folder (X:\Users\<name>) that isn't a placeholder (the patterns below are written so that this file
#      doesn't match itself);
#   5. saves, logs, captures, packages and build output: *.log, *.dmp, *.zip, *.7z, *.wav, *.mp4, *.bmp, *.pgd, *.pgc,
#      *.exe, *.dll, *.pdb, *.lib, *.obj, folders named UDATA, UDATA_backups, TDATA, captures, logs, dumps,
#      game_files, *Package;
#   6. tokens or keys: GitHub tokens, AWS keys, private key blocks, "api_key = ..." style secrets;
#   7. the git history (when DIR is a repository): more than one commit, a remote, an author / committer that
#      isn't the public identity ($publicId below: GitHub's noreply address, never the private e-mail), or an author /
#      committer date that isn't in UTC (+0000: the owner's time zone isn't published); with -Parent SHA (step 117,
#      make_public_tree.ps1 -OnTopOf: an update to the published repository), the history must instead be SHA's
#      (already published) and exactly one commit on top of it, HEAD's only parent being SHA;
#   8. a patch file's Date: line that isn't in UTC (+0000), for the same reason.
# WARNS (exit 0, "AUDIT PASSED WITH N WARNING(S)") on any placeholder left unfilled: the noreply address, ISSUES_URL
# (twoc\ISSUES_URL.txt's placeholder; make_package.ps1's @ISSUES_URL@ token is the packaging's, filled at every
# package and checked by package_audit.ps1) and the issue forms' repository link (the patterns below are written so
# that this file doesn't match itself).
param([Parameter(Mandatory)][string]$Dir, [string]$Manifest, [switch]$AllowSeeds, [string]$Parent)
$ErrorActionPreference = 'Stop'
$Dir = (Resolve-Path -LiteralPath $Dir).Path
if (-not $Manifest) { $Manifest = Join-Path (Split-Path -Parent $PSScriptRoot) 'data\disc_usa.tsv' }
$publicId = 'RicemanRhys <176650384+RicemanRhys@users.noreply.github.com>'   # the public tree's only identity
$fails = New-Object System.Collections.Generic.List[string]
$warns = New-Object System.Collections.Generic.List[string]
function Fail($m) { $fails.Add($m) }
$warnFiles = @{}                        # placeholder -> the files that hold it (one line each in the output)
function Warn($m, $file) {
    if (-not $warnFiles.ContainsKey($m)) { $warnFiles[$m] = New-Object System.Collections.Generic.List[string]; $warns.Add($m) }
    if ($file -and -not $warnFiles[$m].Contains($file)) { $warnFiles[$m].Add($file) }
}

# The disc's files: names (lower case) and size+hash pairs.
$discNames = @{}; $discSizes = @{}
foreach ($l in [IO.File]::ReadAllLines($Manifest)) {
    if ($l -match '^\s*#' -or -not $l.Trim()) { continue }
    $c = $l -split "`t"
    if ($c.Count -lt 3 -or $c[1] -notmatch '^\d+$') { continue }
    $leaf = ($c[0] -split '[\\/]')[-1].ToLower()
    $discNames[$leaf] = $c[0]
    $discSizes["$($c[1])"] = $true
    $discSizes["$($c[1]):$($c[2].ToLower())"] = $c[0]
}
$mine = (Split-Path -Leaf $Manifest).ToLower()

$asm = [regex]'(?m)^\s*(?:[0-9A-Fa-f]{6,8}[: ]\s*)?(?:[0-9A-Fa-f]{2} )*\s*(?:mov|push|pop|call|jmp|j[a-z]{1,3}|lea|cmp|test|xor|sub|add|ret|movzx|fld|fstp|shl|shr|inc|dec)\s+(?:e[abcd]x|e[sd]i|e[bs]p|[abcd][lh]|dword|word|byte|\[|0x[0-9a-f]+|[0-9a-f]+h\b)'
$hexrun = [regex]'(?:\b[0-9A-Fa-f]{2} ){8,}'
$personal = @(
    @{ re = '\\Users\\Use[r]\\'; what = "the owner's Windows user folder" },
    @{ re = '(?i)/c/Users/Use[r]\b'; what = "the owner's user folder (Git Bash form)" },
    @{ re = '(?i)C:/Users/Use[r]\b'; what = "the owner's user folder" },
    @{ re = '(?i)Desktop Stuf[f]|Claude Stuf[f]'; what = "the private repository's folder" },
    @{ re = '(?i)jellybrai[n]892|ricecooke[r]414|protonmai[l]\.com|proton\.m[e]\b'; what = "the owner's e-mail" },
    @{ re = '(?i)\b[A-Z]:[\\/]Users[\\/](?!(?:<user>|%USERNAME%|Someone|YourName|Public|<name>)[\\/])[^\\/\s"''<>%\[\]]+(?=[\\/\s"''<>,;)]|$)'; what = 'a drive path into a user folder' }
)
$secrets = @(
    @{ re = 'gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}'; what = 'a GitHub token' },
    @{ re = 'AKIA[0-9A-Z]{16}'; what = 'an AWS access key' },
    @{ re = '-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----'; what = 'a private key' },
    @{ re = '(?i)\b(?:api[_-]?key|secret[_-]?key|access[_-]?token)\s*[:=]\s*["'']?[A-Za-z0-9/+_\-]{20,}'; what = 'a secret' }
)
$placeholders = @(
    @{ re = 'NOREPLY-ADDRESS-TO-BE-FILLE[D]'; what = "the public identity's noreply address (to fill in)" },
    @{ re = 'ISSUES URL TO BE CHOSEN BY THE OWNE[R]'; what = 'ISSUES_URL (to fill in: twoc\ISSUES_URL.txt)' },
    @{ re = 'OWNER/REPOSITOR[Y]'; what = "the issue forms' link (the repository's owner/name, to fill in)" }
)
$badExt = '.xbe', '.iso', '.xiso', '.xbx', '.sav', '.log', '.dmp', '.zip', '.7z', '.wav', '.mp4', '.bmp', '.pgd',
          '.pgc', '.exe', '.dll', '.pdb', '.lib', '.obj', '.ilk', '.exp'
$badDirs = 'UDATA', 'UDATA_backups', 'TDATA', 'captures', 'logs', 'dumps', 'game_files', 'gen'
$textExt = '.c', '.h', '.cpp', '.hpp', '.hlsl', '.hlsli', '.txt', '.md', '.ps1', '.py', '.sh', '.cmd', '.bat',
           '.cmake', '.rc', '.manifest', '.yml', '.yaml', '.json', '.tsv', '.ini', '.patch', '.gitignore', ''

$files = Get-ChildItem -LiteralPath $Dir -Recurse -Force -File | Where-Object { $_.FullName -notlike "$Dir\.git\*" }
$n = 0
foreach ($f in $files) {
    $n++
    $rel = $f.FullName.Substring($Dir.Length + 1)
    $lname = $f.Name.ToLower()
    $ext = $f.Extension.ToLower()
    if ($f.Attributes -band [IO.FileAttributes]::ReparsePoint) { Fail "a link: $rel"; continue }
    # 1, 5: kinds of file
    if ($ext -in $badExt) { Fail "a file of a forbidden kind ($ext): $rel" }
    foreach ($seg in ($rel -split '\\' | Select-Object -SkipLast 1)) {
        if ($seg -in $badDirs -or $seg -clike '*Package') { Fail "a folder of saves, logs, captures, packages or generated code: $rel"; break }
    }
    if ($rel -match '(?i)recomp[\\/]gen[\\/]|\brecomp_funcs\.h$|\brecomp_types\.h$') { Fail "the generated code: $rel" }
    if ($lname -ne $mine -and $discNames.ContainsKey($lname)) { Fail "named as a disc file ($($discNames[$lname])): $rel" }
    if ($discSizes.ContainsKey("$($f.Length)") -and $f.Length -gt 0) {
        $h = (Get-FileHash -Algorithm SHA256 -LiteralPath $f.FullName).Hash.ToLower()
        if ($discSizes.ContainsKey("$($f.Length):$h")) { Fail "the size and SHA-256 of a disc file ($($discSizes["$($f.Length):$h"])): $rel" }
    }
    # 2: listings by name
    if ($rel -match '(?i)option_b[\\/]|d3d99_asm\.txt$|(^|\\)table\.json$') { Fail "a disassembly listing or its tables: $rel" }
    if ($lname -eq 'seed_functions.json' -and -not $AllowSeeds) { Fail "the game's function addresses (seed_functions.json; -AllowSeeds to publish them): $rel" }
    # 3: size
    if ($f.Length -gt 5MB) { Fail ("over 5 MB ({0:N1} MB): $rel" -f ($f.Length / 1MB)) }
    # contents of text files
    if ($ext -notin $textExt -and $f.Length -gt 0) { continue }
    if ($f.Length -gt 5MB) { continue }
    $t = [IO.File]::ReadAllText($f.FullName)
    $na = $asm.Matches($t).Count
    if ($na -gt 20) { Fail "disassembly ($na x86 instruction lines): $rel" }
    $nh = $hexrun.Matches($t).Count
    if ($nh -gt 5) { Fail "a byte listing ($nh runs of 8+ hex bytes): $rel" }
    foreach ($p in $personal) { if ($t -match $p.re) { Fail "$($p.what) ('$($Matches[0])'): $rel" } }
    foreach ($p in $secrets) { if ($t -match $p.re) { Fail "$($p.what): $rel" } }
    foreach ($p in $placeholders) { if ($t -match $p.re) { Warn $p.what $rel } }
    # 8: dates in patches
    if ($ext -eq '.patch') {
        foreach ($m in [regex]::Matches($t, '(?m)^Date: .*?(?=\r?$)')) {
            if ($m.Value -notmatch ' \+0000$') { Fail "a date that isn't in UTC ('$($m.Value)'): $rel" }
        }
    }
}

# 7: the history
if (Test-Path -LiteralPath (Join-Path $Dir '.git')) {
    Push-Location $Dir
    try {
        $count = [int](& git rev-list --count HEAD)
        if ($Parent) {      # step 117: one commit on top of the published main
            $ps = @((& git rev-list --parents -n 1 HEAD).Trim() -split ' ' | Select-Object -Skip 1)
            $pfull = (& git rev-parse --verify --quiet "$Parent^{commit}")
            if (-not $pfull) { Fail "the parent $Parent isn't in the history" }
            elseif ($ps.Count -ne 1 -or $ps[0] -ne $pfull.Trim()) {
                Fail "HEAD's parents are '$($ps -join ' ')', not only $Parent (one commit on top of it expected)"
            }
            $pcount = if ($pfull) { [int](& git rev-list --count $pfull.Trim()) } else { 0 }
            if ($count -ne $pcount + 1) { Fail "the history has $count commits ($pcount published + 1 expected)" }
        } elseif ($count -ne 1) { Fail "the history has $count commits (one expected)" }
        $remotes = & git remote
        if ($remotes) { Fail "a remote is set: $($remotes -join ', ')" }
        foreach ($who in (& git log --format='%an <%ae>|%cn <%ce>')) {
            foreach ($id in ($who -split '\|')) {
                if ($id -ne $publicId) { Fail "a commit identity that isn't the public one: $id" }
            }
        }
        foreach ($when in (& git log --format='%ad|%cd' --date=raw)) {
            foreach ($d in ($when -split '\|')) {
                if ($d -notmatch '^\d+ \+0000$') { Fail "a commit date that isn't in UTC: $d" }
            }
        }
        $tracked = (& git ls-files | Measure-Object).Count
        $untracked = & git status --porcelain --ignored | Where-Object { $_ -notmatch '\.public-tree-export$' }
        if ($untracked) { Fail "files in the folder that aren't in the commit: $(($untracked | Select-Object -First 5) -join '; ')" }
    } finally { Pop-Location }
}

Write-Output "audit of $Dir : $n files"
foreach ($m in $warns) {
    $fl = $warnFiles[$m]
    if ($fl.Count) { Write-Output ("  WARN  {0}: {1} file(s), e.g. {2}" -f $m, $fl.Count, (($fl | Select-Object -First 3) -join ', ')) }
    else { Write-Output "  WARN  $m" }
}
foreach ($m in $fails) { Write-Output "  FAIL  $m" }
if ($fails.Count) { Write-Output "AUDIT FAILED: $($fails.Count) problem(s)"; exit 1 }
if ($warns.Count) { Write-Output "AUDIT PASSED WITH $($warns.Count) WARNING(S) (placeholders to fill in before publishing)" }
else { Write-Output "AUDIT PASSED" }
exit 0
