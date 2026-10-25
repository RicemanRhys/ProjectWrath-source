# Collect logs.bat (the test packages, twoc/tools/package): zips the game's logs (with the power
# logs, notes 18.94), the settings, the tester's notes, a system summary (CPU, GPU, Windows, power,
# the display's modes and refresh rate), the System log's crash and power events (events.txt) and
# vrrprobe's display test into ProjectWrath_logs_<Target>_<time>.zip next to "Play Project Wrath.bat" (the target
# from PACKAGE.txt's "Target:" line, notes 18.100: LegionGo, Nitro5 or Share; step 109: the project's name first).
# Windows PowerShell 5.1; ASCII only (5.1 reads a script without a BOM as the ANSI code page).
$ErrorActionPreference = 'Continue'
$pkg   = Split-Path -Parent $PSScriptRoot
$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$work  = Join-Path $env:TEMP "ProjectWrath_logs_$stamp"
$target = 'LegionGo'
$pk = Join-Path $pkg 'PACKAGE.txt'
if (Test-Path -LiteralPath $pk) {
    $m = Select-String -LiteralPath $pk -Pattern '^Target: (\w+)' | Select-Object -First 1
    if ($m) { $target = $m.Matches[0].Groups[1].Value }
}
$zip   = Join-Path $pkg "ProjectWrath_logs_${target}_$stamp.zip"
New-Item -ItemType Directory -Force -Path $work | Out-Null

$sys = New-Object System.Collections.Generic.List[string]
function Add([string]$s) { $sys.Add($s) }
function Section([string]$s) { $sys.Add(''); $sys.Add("== $s ==") }
Add "Collected $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz') on $env:COMPUTERNAME, package folder $pkg"

# 0. The game's process. Closing its window ends it at once (TerminateProcess), and the log's last
#    line then says "host window closed: ending the process". On the Legion Go (notes 18.84) a
#    game process was still listed after that, until Windows restarted, and this script
#    refused to collect. So: a process left over from a run whose log has ended is described in
#    system.txt and ended; one whose log hasn't ended is the game running (or stuck), ended only
#    if the tester says so.
$game = @(Get-Process ProjectWrath -ErrorAction SilentlyContinue)
if ($game.Count) {
    $err = Join-Path $pkg 'ProjectWrath.err.log'
    $last = if (Test-Path -LiteralPath $err) { "$(Get-Content -LiteralPath $err -Tail 1)" } else { '' }
    $ended = $last -like '*ending the process*' -or $last -like '`[BAT`]*'
    Section 'The game process at collection'
    Add "ProjectWrath.err.log's last line: $last"
    foreach ($p in $game) {
        $l = "ProjectWrath pid $($p.Id)"
        try { $l += ", started $($p.StartTime.ToString('yyyy-MM-dd HH:mm:ss'))" } catch {}
        try { $l += (", CPU {0:N1} s, {1:N0} MB, {2} handles" -f $p.CPU, ($p.WorkingSet64 / 1MB), $p.HandleCount) } catch {}
        try { $l += ", window '$($p.MainWindowTitle)', responding $($p.Responding)" } catch {}
        Add $l
        try {
            $states = $p.Threads | ForEach-Object {
                $w = ''
                try { if ("$($_.ThreadState)" -eq 'Wait') { $w = " ($($_.WaitReason))" } } catch {}
                "$($_.ThreadState)$w"
            }
            Add ("  {0} threads: {1}" -f @($p.Threads).Count, (($states | Group-Object | ForEach-Object { "$($_.Count) $($_.Name)" }) -join ', '))
        } catch { Add "  threads: $_" }
    }
    if ($ended) {
        Write-Host "The game's log is complete, but a ProjectWrath process is still there (it didn't finish"
        Write-Host "exiting). Ending it; system.txt notes what was left."
    } else {
        Write-Host "Project Wrath is still running (or stuck). Close the game window first, then run Collect logs again."
        Write-Host "If it won't close: type Y and press Enter to end it and collect anyway."
        $a = if ($env:TWOC_COLLECT_QUIET -eq '1') { '' } else { Read-Host 'End it? (Y/N)' }
        if ($a -notmatch '^[Yy]') { Remove-Item -Recurse -Force $work; exit 1 }
        Add 'the tester chose to end it'
    }
    $game | Stop-Process -Force -ErrorAction SilentlyContinue
    $t0 = Get-Date
    while (((Get-Date) - $t0).TotalSeconds -lt 10 -and (Get-Process -Id $game.Id -ErrorAction SilentlyContinue)) { Start-Sleep -Milliseconds 250 }
    if (Get-Process -Id $game.Id -ErrorAction SilentlyContinue) {
        Add 'Stop-Process: still listed 10 s later (stuck in Windows; a restart clears it)'
        Write-Host "It is still there 10 s later; a restart clears it. Collecting anyway."
    } else {
        Add ("Stop-Process: gone after {0:N1} s" -f ((Get-Date) - $t0).TotalSeconds)
    }
}

Write-Host "Collecting into $zip ..."

# 1. The game's files: the last run's logs, the earlier runs' (logs\), the settings, the notes.
foreach ($f in 'ProjectWrath.err.log', 'ProjectWrath.out.log', 'xbox_kernel.log', 'ProjectWrath.power.log', 'ProjectWrath.ini', 'Test notes.txt', 'PACKAGE.txt') {
    $p = Join-Path $pkg $f
    if (Test-Path -LiteralPath $p) { Copy-Item -LiteralPath $p -Destination $work }
}
if (Test-Path (Join-Path $pkg 'logs')) {
    Copy-Item -Recurse -Path (Join-Path $pkg 'logs') -Destination (Join-Path $work 'logs')
}

# 2. A system summary (after section 0's notes on the game process, if any).
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
# Step 97 (notes 18.100): the cores by kind, as the game reads them (GetSystemCpuSetInformation's EfficiencyClass:
# more than one class is a hybrid CPU, performance and efficiency cores), and the game's own [CPU] lines (the
# process's power throttling) from its last run.
try {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class TwocCpuSets {
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetSystemCpuSetInformation(IntPtr info, uint len, out uint ret, IntPtr proc, uint flags);
    public static string[] Describe() {
        uint len;
        GetSystemCpuSetInformation(IntPtr.Zero, 0, out len, IntPtr.Zero, 0);
        if (len == 0) return new string[] { "GetSystemCpuSetInformation: nothing returned" };
        IntPtr b = Marshal.AllocHGlobal((int)len);
        try {
            if (!GetSystemCpuSetInformation(b, len, out len, IntPtr.Zero, 0)) return new string[] { "GetSystemCpuSetInformation failed" };
            var threads = new SortedDictionary<int, int>();
            var cores = new SortedDictionary<int, HashSet<int>>();
            int off = 0;
            while (off + 8 <= len) {
                int size = Marshal.ReadInt32(b, off), type = Marshal.ReadInt32(b, off + 4);
                if (size == 0) break;
                if (type == 0) {        // CpuSetInformation: Id, Group (12), LogicalProcessorIndex, CoreIndex (15), ..., EfficiencyClass (18)
                    int grp = Marshal.ReadInt16(b, off + 12), core = Marshal.ReadByte(b, off + 15), cls = Marshal.ReadByte(b, off + 18);
                    if (!threads.ContainsKey(cls)) { threads[cls] = 0; cores[cls] = new HashSet<int>(); }
                    threads[cls]++;
                    cores[cls].Add(grp << 8 | core);
                }
                off += size;
            }
            var r = new List<string>();
            r.Add(threads.Count > 1 ? "hybrid: " + threads.Count + " kinds of core" : "one kind of core");
            foreach (var k in threads.Keys)
                r.Add(string.Format("  efficiency class {0}: {1} cores, {2} threads", k, cores[k].Count, threads[k]));
            return r.ToArray();
        } finally { Marshal.FreeHGlobal(b); }
    }
}
'@
    Section 'CPU cores by kind (the higher efficiency class is the faster core)'
    foreach ($l in [TwocCpuSets]::Describe()) { Add $l }
} catch { Add "CPU set query failed: $_" }
try {
    $err = Join-Path $pkg 'ProjectWrath.err.log'
    if (Test-Path -LiteralPath $err) {
        Section "The game's processor lines (its last run, ProjectWrath.err.log)"
        foreach ($m in Select-String -LiteralPath $err -Pattern '^\[(CPU|GPU)\]' | Select-Object -First 12) { Add $m.Line }
        # step 99: where the game and render threads ran (the kind of core) and each kind's MHz, its last 10 s
        $m = Select-String -LiteralPath $err -Pattern '^\[D3DB\] host cores:' | Select-Object -Last 1
        if ($m) { Add "The last 10-s interval's: $($m.Line)" }
        # step 99: memory (the process, the GPU's, the game's own caches, the guest heap): the first and last samples
        $mem = @(Select-String -LiteralPath $err -Pattern '^\[MEM\]')
        if ($mem.Count) {
            Section "The game's memory (its last run: the first and the last [MEM] lines, $($mem.Count) in all)"
            Add $mem[0].Line
            if ($mem.Count -gt 1) { Add $mem[$mem.Count - 1].Line }
        }
    }
} catch { Add "the game's [CPU] lines: $_" }
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
        # -like ignores case, so '*Ac*' matched the "Ac" of "Active" in both names (both said plugged in, step 98)
        if ($v) { $name = $modes[$v]; if (-not $name) { $name = $v }; Add "Power mode ($(if ($n -eq 'ActiveOverlayAcPowerScheme') { 'plugged in' } else { 'on battery' })): $name" }
    }
} catch { Add "power query failed: $_" }
if ($target -ne 'Share') { try {     # step 107: not in the Share package's zip (what else the player runs is theirs)
    Section 'Busiest processes now (CPU seconds since they started)'
    Get-Process | Sort-Object CPU -Descending | Select-Object -First 15 | ForEach-Object {
        Add ("  {0,-32} {1,10:N1} s  {2,8:N0} MB" -f $_.ProcessName, $_.CPU, ($_.WorkingSet64 / 1MB))
    }
} catch { Add "process list failed: $_" } }
$sys | Set-Content -Encoding UTF8 (Join-Path $work 'system.txt')

# 2b. Step 91 (notes 18.94): the System log's crash, power and display-driver events from the last
#     3 days, newest first, and the active power plan, in events.txt.
$ev = New-Object System.Collections.Generic.List[string]
$ev.Add("Collected $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz'). System log events of the last 3 days (wevtutil), newest first; each Date is this PC's local time, with UTC beside it:")
$ev.Add('  Kernel-Power 41: Windows started again without a clean shutdown (a crash, a hang, power lost);')
$ev.Add('  EventLog 6008: the shutdown before was unexpected; BugCheck 1001: a blue screen and its code;')
$ev.Add('  source Display (4101: the display driver stopped responding and recovered).')
$ev.Add('')
try {
    $q = "*[System[((Provider[@Name='Microsoft-Windows-Kernel-Power'] and EventID=41) or " +
         "(Provider[@Name='EventLog'] and EventID=6008) or " +
         "(EventID=1001 and (Provider[@Name='Microsoft-Windows-WER-SystemErrorReporting'] or Provider[@Name='BugCheck'])) or " +
         "Provider[@Name='Display'] or EventID=4101) and TimeCreated[timediff(@SystemTime) <= 259200000]]]"
    $r = @(& wevtutil.exe qe System "/q:$q" /f:text /rd:true 2>&1 | ForEach-Object { "$_" })
    # Step 94 (notes 18.97): wevtutil's text format prints the local time with a "Z" after it (checked
    # against Get-WinEvent and the event's own SystemTime on the Legion Go and the desktop), so each
    # Date line says local, with its UTC offset and the UTC time.
    foreach ($l in $r) {
        if ($l -match '^(\s*Date: )(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)(\.\d+)?Z\s*$') {
            $t = [datetime]::ParseExact($Matches[2], 'yyyy-MM-ddTHH:mm:ss', [Globalization.CultureInfo]::InvariantCulture,
                                        [Globalization.DateTimeStyles]::AssumeLocal)
            $o = [TimeZoneInfo]::Local.GetUtcOffset($t)
            $l = '{0}{1} local (UTC{2}{3:hh\:mm}; {4} UTC)' -f $Matches[1], $t.ToString('yyyy-MM-dd HH:mm:ss'),
                 $(if ($o -lt [TimeSpan]::Zero) { '-' } else { '+' }), $o,
                 $t.ToUniversalTime().ToString('yyyy-MM-dd HH:mm:ss')
        }
        $ev.Add($l)
    }
    if (-not $r.Count) { $ev.Add('(no such events)') }
} catch { $ev.Add("wevtutil failed: $_") }
# Kernel-Power 41's own fields tell a blue screen (BugcheckCode not 0) from a hang ended with the
# power button (LongPowerButtonPressDetected) or power lost.
try {
    $k41 = @(Get-WinEvent -FilterHashtable @{ LogName = 'System'; ProviderName = 'Microsoft-Windows-Kernel-Power'; Id = 41;
                                              StartTime = (Get-Date).AddDays(-3) } -ErrorAction Stop)
    $ev.Add('')
    $ev.Add('== Kernel-Power 41, each with its data (local time) ==')
    foreach ($e in $k41) {
        $x = [xml]$e.ToXml()
        $d = @($x.Event.EventData.Data | ForEach-Object { "$($_.Name)=$($_.'#text')" }) -join ', '
        $ev.Add("$($e.TimeCreated.ToString('yyyy-MM-dd HH:mm:ss')): $d")
    }
} catch { $ev.Add(''); $ev.Add("Kernel-Power 41 in the last 3 days: none ($($_.Exception.Message))") }
$ev.Add('')
$ev.Add('== powercfg /getactivescheme ==')
try { foreach ($l in @(& powercfg.exe /getactivescheme 2>&1)) { $ev.Add("$l") } } catch { $ev.Add("powercfg failed: $_") }
$ev | Set-Content -Encoding UTF8 (Join-Path $work 'events.txt')

# 3. vrrprobe: the display as it describes itself (modes, rotation, scale, EDID range), then
#    two short tests of how frames presented at 90 fps reach the screen (a grey screen with a
#    moving bar, about 6 s each): on a fixed 144 Hz refresh they land 6.9 or 13.9 ms apart, with
#    variable refresh every 11.1 ms.
$probe = Join-Path $PSScriptRoot 'vrrprobe.exe'
$out = Join-Path $work 'vrrprobe.txt'
if ($target -eq 'Share') {
    # step 107: the Share package has no display test
} elseif (Test-Path $probe) {
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

# 3b. Step 107 (notes 18.110): the scrub. In every text file going into the zip, the home folder's path becomes
#     <user-home>, the Windows user name in a path (\Users\NAME) becomes \Users\<user>, the name on its own (as a
#     whole word, its own case) <user>, and the PC's name <pc>. scrub.txt says what was replaced and how often.
$scrub = New-Object System.Collections.Generic.List[string]
$scrub.Add("Scrubbed $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss'): the home folder -> <user-home>, the user name -> <user>, the PC's name -> <pc>.")
$homeDir = $env:USERPROFILE; $user = $env:USERNAME; $pcName = $env:COMPUTERNAME
$utf8 = New-Object Text.UTF8Encoding($false)
foreach ($f in Get-ChildItem -Recurse -File $work) {
    if ($f.Extension -notin '.log', '.txt', '.ini', '.csv') { continue }
    $t = [IO.File]::ReadAllText($f.FullName, $utf8)
    $n0 = $t.Length; $hits = 0
    $pairs = @()
    if ($homeDir) {
        $pairs += ,@([regex]::Escape($homeDir), '<user-home>')
        $pairs += ,@([regex]::Escape($homeDir.Replace('\', '/')), '<user-home>')
    }
    # (each pattern in brackets: a comma binds tighter than +, and would glue the replacement onto it)
    if ($user) {
        $pairs += ,@(('(?i)([\\/]Users[\\/])' + [regex]::Escape($user) + '(?=[\\/"'' ]|$)'), '$1<user>')
        $pairs += ,@(('(?<![A-Za-z0-9_])' + [regex]::Escape($user) + '(?![A-Za-z0-9_])'), '<user>')
    }
    if ($pcName) { $pairs += ,@(('(?i)(?<![A-Za-z0-9_-])' + [regex]::Escape($pcName) + '(?![A-Za-z0-9_-])'), '<pc>') }
    foreach ($p in $pairs) {
        $rx = $p[0]
        if (-not $rx.StartsWith('(?i)') -and $rx -notlike '(?<!*') { $rx = '(?i)' + $rx }
        $c = [regex]::Matches($t, $rx).Count
        if ($c) { $t = [regex]::Replace($t, $rx, $p[1]); $hits += $c }
    }
    if ($hits) {
        [IO.File]::WriteAllText($f.FullName, $t, $utf8)
        $scrub.Add("  $($f.FullName.Substring($work.Length + 1)): $hits replaced")
    }
}
$scrub | Set-Content -Encoding UTF8 (Join-Path $work 'scrub.txt')

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
if ($target -eq 'Share') {
    Write-Host "Your Windows user name, home folder and PC name were replaced by <user>, <user-home> and <pc>"
    Write-Host "(scrub.txt in the zip lists where). Look inside before you send it; nothing is sent by itself."
} else {
    Write-Host "Copy this zip back to the desktop PC (USB stick, cloud folder or LocalSend)."
}
if ($env:TWOC_COLLECT_QUIET -ne '1') { Start-Process explorer.exe -ArgumentList "/select,`"$zip`"" }
