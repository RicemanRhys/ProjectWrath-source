# The package's smoke test (notes 18.91; step 80's and 85's scratch pt.ps1, kept here).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File twoc\tools\package\smoke_test.ps1
#       -Dir COPY -Out DIR [-Secs 60] [-Bat "Play Crash.bat"] [-Log run_twoc.err.log] [-F1] [-Probe] [-Quit]
#
# Starts BAT from COPY (a scratch copy of the package, or of the DXVK test folder beside a stand-in
# LegionGoPackage) like a double-click: its own console, working directory C:\. Grabs the game window
# 3 s after it opens (the startup message) and 12 s before the end (the counter), lists the DLLs the
# game loaded and its windows, with -F1 presses F1 twice halfway (the overlay opens, then closes), with
# -Probe grabs the Vulkan probe's window too, then closes the game window, waits for the console to go
# and copies LOG (relative to COPY) into DIR. Read the log for [CFG], [FILES], "host baked draws",
# "host startup message", [OVL] F1, [VK] and "[BAT] ... ended with code 0".
# -Quit (step 97, notes 18.100): the game is closed by the settings menu's Quit game instead of its window,
# through an XInput stand-in pad (RECOMP_DEV_XINPUT_FILE, a test aid): F1, RB three times (the Saves tab),
# down to the bottom row, right to Quit game, A, Left to Yes, A; the log should end with the [QUIT] lines and
# "[BAT] ... ended with code 0". The log's [GPU] and [CPU] lines are printed too.
# Needs twoc\tools\diag\wgcgrab.exe (wgcgrab_build.bat) and Python. Never run it on the package itself
# (the runs make saves and logs in the folder they start from).
param([Parameter(Mandatory)][string]$Dir, [Parameter(Mandatory)][string]$Out, [int]$Secs = 60,
      [string]$Bat = 'Play Crash.bat', [string]$Log = 'run_twoc.err.log', [switch]$F1, [switch]$Probe, [switch]$Quit)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$diag = Join-Path (Split-Path -Parent $here) 'diag'
$grabExe = Join-Path $diag 'wgcgrab.exe'
if (-not (Test-Path $grabExe)) { throw "$grabExe missing: build it with wgcgrab_build.bat" }
New-Item -ItemType Directory -Force $Out | Out-Null
if (Get-Process crash_twoc -ErrorAction SilentlyContinue) { throw 'crash_twoc already running' }
$grab = Start-Process -FilePath $grabExe -ArgumentList '"Crash TWOC - D3D bridge (host)"', "`"$Out\g`"", '3', ([string]($Secs - 12)) -PassThru -WindowStyle Hidden -RedirectStandardOutput "$Out\grab.txt"
if ($Probe) {
    $pgrab = Start-Process -FilePath $grabExe -ArgumentList '"Crash TWOC - Vulkan probe (RECOMP_VULKAN=probe)"', "`"$Out\vk`"", '10', '30' -PassThru -WindowStyle Hidden -RedirectStandardOutput "$Out\vkgrab.txt"
}
$xi = Join-Path $Out 'xi.txt'
function Pad([string]$line, [double]$hold = 0.12) {     # one press of the stand-in pad
    foreach ($l in $line, 'buttons=0') {
        Set-Content -Encoding ascii -Path "$xi.tmp" -Value $l
        Move-Item -Force "$xi.tmp" $xi
        Start-Sleep -Milliseconds ([int]($(if ($l -eq $line) { $hold } else { 0.35 }) * 1000))
    }
}
if ($Quit) {
    Set-Content -Encoding ascii -Path $xi -Value 'buttons=0'
    $env:RECOMP_DEV_XINPUT_FILE = $xi
}
$t0 = Get-Date
$p = Start-Process -FilePath (Join-Path $Dir $Bat) -WorkingDirectory 'C:\' -PassThru
if ($F1) {
    Start-Sleep -Seconds ([int]($Secs / 2))
    & python -I "$here\windows.py" key F1 | Out-File -Append "$Out\keys.txt"
    Start-Sleep -Seconds 4
    & python -I "$here\windows.py" key F1 | Out-File -Append "$Out\keys.txt"
    Start-Sleep -Seconds ($Secs - [int]($Secs / 2) - 4)
} else {
    Start-Sleep -Seconds $Secs
}
$g = Get-Process crash_twoc -ErrorAction SilentlyContinue
if (-not $g) { "game not running after $Secs s" | Tee-Object "$Out\result.txt"; exit 1 }
"game pid $($g.Id), started from $Dir" | Out-File "$Out\result.txt"
$g.Modules | ForEach-Object { $_.FileName } | Sort-Object | Out-File "$Out\modules.txt"
$mods = Get-Content "$Out\modules.txt"
"modules: $($mods.Count); outside System32/WinSxS/DriverStore: " | Out-File -Append "$Out\result.txt"
$mods | Where-Object { $_ -notmatch '\\Windows\\(System32|WinSxS|SystemApps)\\' -and $_ -notmatch 'DriverStore' } | Out-File -Append "$Out\result.txt"
& python -I "$here\windows.py" | Out-File -Append "$Out\result.txt"
$grab.WaitForExit(30000) | Out-Null
if ($Probe) { $pgrab.WaitForExit(30000) | Out-Null }
if ($Quit) {
    & python -I "$here\windows.py" key F1 | Out-File -Append "$Out\keys.txt"
    Start-Sleep -Seconds 2
    1..3 | ForEach-Object { Pad 'white=255' }
    1..10 | ForEach-Object { Pad 'buttons=0x2' }
    Pad 'buttons=0x8'
    Pad 'a=255'
    Start-Sleep -Seconds 1
    Pad 'buttons=0x4'
    Pad 'a=255'
    "Quit game pressed (Yes)" | Out-File -Append "$Out\result.txt"
} else {
    & python -I (Join-Path $diag 'sweep\wmclose.py') | Out-File -Append "$Out\result.txt"
}
$ok = $p.WaitForExit(30000)
"console ended: $ok after $([int]((Get-Date) - $t0).TotalSeconds) s; game still running: $([bool](Get-Process crash_twoc -ErrorAction SilentlyContinue))" | Out-File -Append "$Out\result.txt"
Copy-Item (Join-Path $Dir $Log) "$Out\" -ErrorAction SilentlyContinue
$lf = Join-Path $Out (Split-Path -Leaf $Log)
if (Test-Path $lf) {
    Select-String -Path $lf -Pattern '^\[(GPU|CPU|QUIT|RESTART|BAT)\]' | ForEach-Object { $_.Line } | Out-File -Append "$Out\result.txt"
}
Get-Content "$Out\result.txt"
