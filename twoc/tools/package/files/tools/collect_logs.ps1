# Collect logs.bat (Legion Go package, twoc/tools/package): zips the game's logs, the settings,
# the tester's notes, a system summary (CPU, GPU, Windows, power, the display's modes and refresh
# rate) and vrrprobe's display test into LegionGo_logs_<time>.zip next to "Play Crash.bat".
# Windows PowerShell 5.1; ASCII only (5.1 reads a script without a BOM as the ANSI code page).
$ErrorActionPreference = 'Continue'
$pkg   = Split-Path -Parent $PSScriptRoot
$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$work  = Join-Path $env:TEMP "twoc_logs_$stamp"
$zip   = Join-Path $pkg "LegionGo_logs_$stamp.zip"
New-Item -ItemType Directory -Force -Path $work | Out-Null

if (Get-Process crash_twoc -ErrorAction SilentlyContinue) {
    Write-Host "Crash is still running. Close the game first (its log is only complete once it has ended),"
    Write-Host "then run Collect logs again."
    exit 1
}

Write-Host "Collecting into $zip ..."

# 1. The game's files: the last run's logs, the earlier runs' (logs\), the settings, the notes.
foreach ($f in 'run_twoc.err.log', 'run_twoc.out.log', 'xbox_kernel.log', 'crash_twoc.ini', 'Test notes.txt', 'PACKAGE.txt') {
    $p = Join-Path $pkg $f
    if (Test-Path -LiteralPath $p) { Copy-Item -LiteralPath $p -Destination $work }
}
if (Test-Path (Join-Path $pkg 'logs')) {
    Copy-Item -Recurse -Path (Join-Path $pkg 'logs') -Destination (Join-Path $work 'logs')
}

# 2. A system summary.
$sys = New-Object System.Collections.Generic.List[string]
function Add([string]$s) { $sys.Add($s) }
function Section([string]$s) { $sys.Add(''); $sys.Add("== $s ==") }
Add "Collected $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz') on $env:COMPUTERNAME, package folder $pkg"
try {
    $cs = Get-CimInstance Win32_ComputerSystem
    $bios = Get-CimInstance Win32_BIOS
    $os = Get-CimInstance Win32_OperatingSystem
    $cv = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    Section 'Machine'
    Add "$($cs.Manufacturer) $($cs.Model) ($($cs.SystemFamily)), BIOS $($bios.SMBIOSBIOSVersion)"
    Add ("Memory: {0:N1} GB" -f ($cs.TotalPhysicalMemory / 1GB))
    Section 'Windows'
    Add "$($os.Caption) $($cv.DisplayVersion), version $($os.Version), build $($cv.CurrentBuild).$($cv.UBR), $($os.OSArchitecture)"
    Add "Last boot: $($os.LastBootUpTime)"
} catch { Add "machine/Windows query failed: $_" }
try {
    Section 'CPU'
    foreach ($c in Get-CimInstance Win32_Processor) {
        Add "$($c.Name.Trim())"
        Add "  $($c.NumberOfCores) cores, $($c.NumberOfLogicalProcessors) logical processors, max $($c.MaxClockSpeed) MHz, now $($c.CurrentClockSpeed) MHz, load $($c.LoadPercentage)%"
    }
} catch { Add "CPU query failed: $_" }
try {
    Section 'GPU'
    foreach ($g in Get-CimInstance Win32_VideoController) {
        $l = "$($g.Name): driver $($g.DriverVersion) ($($g.DriverDate))"
        if ($g.CurrentRefreshRate) { $l += ", $($g.CurrentHorizontalResolution)x$($g.CurrentVerticalResolution) at $($g.CurrentRefreshRate) Hz (Windows' list: $($g.MinRefreshRate)-$($g.MaxRefreshRate) Hz)" }
        else { $l += ', no display on it' }
        Add $l
    }
} catch { Add "GPU query failed: $_" }
try {
    Section 'Power'
    Add-Type -AssemblyName System.Windows.Forms
    $ps = [System.Windows.Forms.SystemInformation]::PowerStatus
    $line = if ($ps.PowerLineStatus -eq 'Online') { 'plugged in' } elseif ($ps.PowerLineStatus -eq 'Offline') { 'on battery' } else { "power line $($ps.PowerLineStatus)" }
    Add ("{0}, battery {1:P0} ({2})" -f $line, $ps.BatteryLifePercent, $ps.BatteryChargeStatus)
    Add "Power plan: $((powercfg /getactivescheme) -join ' ')"
    # Windows 11's Power mode (Settings, System, Power & battery) is an overlay on the plan.
    $modes = @{ '961cc777-2547-4f9d-8174-7d86181b8a7a' = 'Best power efficiency';
                '00000000-0000-0000-0000-000000000000' = 'Balanced';
                'ded574b5-45a0-4f42-8737-46345c09c238' = 'Best performance' }
    $pk = Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Power\User\PowerSchemes' -ErrorAction SilentlyContinue
    foreach ($n in 'ActiveOverlayAcPowerScheme', 'ActiveOverlayDcPowerScheme') {
        $v = $pk.$n
        if ($v) { $name = $modes[$v]; if (-not $name) { $name = $v }; Add "Power mode ($(if ($n -like '*Ac*') { 'plugged in' } else { 'on battery' })): $name" }
    }
} catch { Add "power query failed: $_" }
try {
    Section 'Busiest processes now (CPU seconds since they started)'
    Get-Process | Sort-Object CPU -Descending | Select-Object -First 15 | ForEach-Object {
        Add ("  {0,-32} {1,10:N1} s  {2,8:N0} MB" -f $_.ProcessName, $_.CPU, ($_.WorkingSet64 / 1MB))
    }
} catch { Add "process list failed: $_" }
$sys | Set-Content -Encoding UTF8 (Join-Path $work 'system.txt')

# 3. vrrprobe: the display as it describes itself (modes, rotation, scale, EDID range), then
#    two short tests of how frames presented at 90 fps reach the screen (a grey screen with a
#    moving bar, about 6 s each): on a fixed 144 Hz refresh they land 6.9 or 13.9 ms apart, with
#    variable refresh every 11.1 ms.
$probe = Join-Path $PSScriptRoot 'vrrprobe.exe'
$out = Join-Path $work 'vrrprobe.txt'
if (Test-Path $probe) {
    Write-Host "Testing the display: the screen goes grey with a moving bar twice, about 6 seconds each."
    $runs = @(@('0', '0'), @('90', '6'), @('90', '6', '--tear'))
    foreach ($a in $runs) {
        Add-Content -Encoding UTF8 $out "### vrrprobe.exe $($a -join ' ')"
        $r = @(& $probe @a 2>&1 | ForEach-Object { "$_" })
        if ($a[1] -ne '0') {     # the display's description is in the first run: from "presenting" on
            $i = 0
            while ($i -lt $r.Count -and $r[$i] -notlike 'presenting*') { $i++ }
            if ($i -lt $r.Count) { $r = $r[$i..($r.Count - 1)] }
        }
        Add-Content -Encoding UTF8 $out (($r -join "`r`n") + "`r`n")
    }
} else {
    Set-Content -Encoding UTF8 $out "vrrprobe.exe not found in $PSScriptRoot"
}

# 4. The zip.
if (Test-Path $zip) { Remove-Item $zip }
# Entry by entry: 5.1's Compress-Archive (and .NET Framework's CreateFromDirectory) write
# backslashes into the names, which other unzippers keep as part of the file name.
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$za = [IO.Compression.ZipFile]::Open($zip, 'Create')
try {
    foreach ($f in Get-ChildItem -Recurse -File $work) {
        $name = $f.FullName.Substring($work.Length + 1).Replace('\', '/')
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($za, $f.FullName, $name)
    }
} finally { $za.Dispose() }
Remove-Item -Recurse -Force $work
$mb = (Get-Item $zip).Length / 1MB
Write-Host ("Done: {0} ({1:N1} MB)" -f $zip, $mb)
Write-Host "Copy this zip back to the desktop PC (USB stick, cloud folder or LocalSend)."
if ($env:TWOC_COLLECT_QUIET -ne '1') { Start-Process explorer.exe -ArgumentList "/select,`"$zip`"" }
