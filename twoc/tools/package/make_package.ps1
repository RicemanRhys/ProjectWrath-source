# The Legion Go package (notes 18.83): a folder that runs the game on another Windows PC.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File twoc\tools\package\make_package.ps1
#       [-Out DIR] [-Exe crash_twoc.exe] [-GameFiles DIR] [-Save UDATA_DIR] [-Force] [-Pgo]
#
# -Pgo (step 95, notes 18.98): the exe built with profile-guided optimisation in a build folder of its own
# (twoc\build_pgo; twoc\build, the ordinary build, untouched): linked instrumented (TWOC_PGO=GEN), trained by
# pgo_train.sh through the sweep harness (a scratch copy in %TEMP%: cast_bug's start gliding, avalanch, the upstairs
# room, ~6 minutes with the menu, intro and hub on the way; needs Git Bash, the repo's .venv and this PC's display),
# then linked again with the counts (TWOC_PGO=USE). /fp stays precise. PACKAGE.txt says so.
#
# Makes DIR (default: Desktop\Crash\LegionGoPackage) with:
#   crash_twoc.exe and the Visual C++ runtime DLLs it imports (vcruntime140.dll, vcruntime140_1.dll,
#     copied app-local from Visual Studio's redist folder); every other DLL it imports is part of
#     Windows 10/11 (checked here with dumpbin /dependents; the list goes into PACKAGE.txt)
#   game_files\ (default.xbe and Crashdat\ from -GameFiles; never UDATA (saves), UDATA_backups, TDATA
#     or our analysis files; -Save DIR copies DIR in as game_files\UDATA, only when asked)
#   tools\vrrprobe.exe (built from twoc\tools\diag\vrrprobe.cpp) and tools\collect_logs.ps1
#   files\: Play Crash.bat, Play Crash (72 fps test).bat, Play Crash (ack wait test).bat,
#     Play Crash (bake off).bat, Play Crash (Vulkan probe).bat (notes 18.90),
#     Play Crash (fullscreen start test).bat, Play Crash (fullscreen old way).bat (notes 18.93),
#     Play Crash (safe settings).bat (notes 18.96: RECOMP_IGNORE_INI=1, the defaults, the ini untouched),
#     Collect logs.bat, crash_twoc.ini (the Legion Go's settings), README.txt, Test notes.txt
#     (text files written with CRLF line ends)
#   THIRD_PARTY_LICENSES.txt (twoc/, from third_party_licenses.py)
#   PACKAGE.txt: the build (commits, exe hash, link-time code generation), the imported DLLs and
#     where each comes from
#
# The exe finds game_files\ in its working directory (Play Crash.bat sets it to its own folder)
# and crash_twoc.ini next to itself, so the folder runs from anywhere. An existing DIR is
# replaced only with -Force and only if it holds a PACKAGE.txt (one this script made).
# Build first: cmake --build twoc\build --config Release --parallel
# Then smoke-test a scratch copy (never the package itself): smoke_test.ps1 (notes 18.91).
param(
    [string]$Out = (Join-Path ([Environment]::GetFolderPath('Desktop')) 'Crash\LegionGoPackage'),
    [string]$Exe,
    [string]$GameFiles,
    [string]$Save,
    [switch]$Force,
    [switch]$Pgo
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$twoc = Split-Path -Parent (Split-Path -Parent $here)
$repo = Split-Path -Parent $twoc
if ($Pgo -and $Exe) { throw '-Pgo builds its own exe: leave -Exe out' }
if (-not $Exe) { $Exe = Join-Path $twoc 'build\Release\crash_twoc.exe' }
if (-not $GameFiles) { $GameFiles = Join-Path $repo 'game_files' }
if (-not $Pgo) { $Exe = (Resolve-Path $Exe).Path }
$GameFiles = (Resolve-Path $GameFiles).Path
if (-not (Test-Path (Join-Path $GameFiles 'default.xbe'))) { throw "No default.xbe in $GameFiles" }
if (-not (Test-Path (Join-Path $GameFiles 'Crashdat'))) { throw "No Crashdat\ in $GameFiles" }

# Visual Studio: dumpbin, vcvars64 (for vrrprobe) and the redistributable C++ runtime.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio with the C++ tools not found (vswhere)' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$dumpbin = Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe') | Sort-Object FullName | Select-Object -Last 1
$crt = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT') -Directory |
       Where-Object { $_.FullName -match '\\\d+\.\d+\.\d+\\' } | Sort-Object FullName | Select-Object -Last 1
if (-not $dumpbin -or -not $crt) { throw 'dumpbin or the VC redist CRT folder not found' }

# -Pgo (step 95): instrumented, trained, linked again, in twoc\build_pgo.
$pgoLine = 'off'
if ($Pgo) {
    $msvcBin = Split-Path -Parent $dumpbin.FullName
    $pgort = Join-Path $msvcBin 'pgort140.dll'
    $pgosweep = Join-Path $msvcBin 'pgosweep.exe'
    if (-not (Test-Path $pgort) -or -not (Test-Path $pgosweep)) { throw "pgort140.dll or pgosweep.exe not in $msvcBin" }
    $git = (Get-Command git -ErrorAction Stop).Source        # Git for Windows: ...\Git\cmd\git.exe
    $bash = Join-Path (Split-Path -Parent (Split-Path -Parent $git)) 'bin\bash.exe'
    if (-not (Test-Path $bash)) { throw "Git Bash not found beside $git" }
    if (-not (Test-Path (Join-Path $repo '.venv\Scripts\python.exe'))) { throw 'The repo''s .venv (the harness''s Python) not found' }
    $pb = Join-Path $twoc 'build_pgo'
    $pgd = Join-Path $pb 'crash_twoc.pgd'
    $cfg = @('-S', $twoc, '-B', $pb, "-DTWOC_PGO_PGD=$pgd")
    $sdl = Join-Path $twoc 'build\_deps\sdl3-src'      # SDL from the ordinary build's download (no network)
    if (Test-Path (Join-Path $sdl 'CMakeLists.txt')) { $cfg += "-DFETCHCONTENT_SOURCE_DIR_SDL3=$sdl" }
    $t0 = Get-Date
    Write-Host 'PGO: the instrumented build (twoc\build_pgo)'
    & cmake @cfg -DTWOC_PGO=GEN | Out-Null
    if ($LASTEXITCODE) { throw 'cmake configure (GEN) failed' }
    & cmake --build $pb --config Release --parallel | Out-Null
    if ($LASTEXITCODE) { throw 'the instrumented build failed' }
    Get-ChildItem $pb -Filter '*.pgc' | Remove-Item
    # The harness: a scratch copy of the sweep folder, its own root\game_files (default.xbe, an empty UDATA, Crashdat
    # as a junction), the instrumented exe beside pgort140.dll; a path without spaces for the harness's arguments.
    $h = Join-Path $env:TEMP ('twoc_pgo_' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    $junction = Join-Path $h 'root\game_files\Crashdat'
    try {
        Copy-Item -Recurse (Join-Path $twoc 'tools\diag\sweep') $h
        Copy-Item (Join-Path $here 'pgo_train.sh') $h
        foreach ($sh in Get-ChildItem $h -Filter '*.sh') {      # bash wants LF (a checkout may have made them CRLF)
            [IO.File]::WriteAllText($sh.FullName, ([IO.File]::ReadAllText($sh.FullName) -replace "`r`n", "`n"),
                                    (New-Object Text.UTF8Encoding($false)))
        }
        New-Item -ItemType Directory -Force -Path (Join-Path $h 'root\game_files\UDATA'), (Join-Path $h 'runs'),
            (Join-Path $h 'pgi') | Out-Null
        Copy-Item (Join-Path $GameFiles 'default.xbe') (Join-Path $h 'root\game_files')
        New-Item -ItemType Junction -Path $junction -Target (Join-Path $GameFiles 'Crashdat') | Out-Null
        Copy-Item (Join-Path $pb 'Release\crash_twoc.exe'), $pgort (Join-Path $h 'pgi')
        Write-Host "PGO: training (cast_bug, avalanch, upstairs) in $h"
        $t1 = Get-Date
        & $bash (Join-Path $h 'pgo_train.sh') ($h -replace '\\', '/') (Join-Path $h 'pgi\crash_twoc.exe') $pb $pgosweep
        $trainMin = ((Get-Date) - $t1).TotalMinutes
        if ($LASTEXITCODE) { throw 'the PGO training failed' }
    } finally {
        $tl = Join-Path $pb 'pgo_train_logs'                                # the training's logs, kept
        New-Item -ItemType Directory -Force -Path $tl | Out-Null
        Get-ChildItem (Join-Path $h 'runs') -Filter 'pgt_*' -ErrorAction SilentlyContinue |
            Where-Object { $_.Extension -in '.txt', '.log' } | Copy-Item -Destination $tl
        if (Test-Path $junction) { [IO.Directory]::Delete($junction) }     # the link only, never what it points to
        if ((Test-Path $h) -and -not (Test-Path $junction)) { Remove-Item -Recurse -Force $h }
    }
    $pgc = @(Get-ChildItem $pb -Filter '*.pgc')
    if ($pgc.Count -lt 3) { throw "PGO: $($pgc.Count) count files from the training, 3 expected" }
    Write-Host 'PGO: linking again with the counts'
    & cmake @cfg -DTWOC_PGO=USE | Out-Null
    if ($LASTEXITCODE) { throw 'cmake configure (USE) failed' }
    & cmake --build $pb --config Release --parallel | Out-Null
    if ($LASTEXITCODE) { throw 'the PGO build failed' }
    $Exe = (Resolve-Path (Join-Path $pb 'Release\crash_twoc.exe')).Path
    $pgoLine = ('on (twoc\build_pgo: trained {0:N1} min on cast_bug, avalanch and the upstairs room, {1} count files;' +
                ' {2:N1} min in all)') -f $trainMin, $pgc.Count, ((Get-Date) - $t0).TotalMinutes
    Write-Host "PGO: $pgoLine"
}

# A stale exe is the easiest mistake: say so if a source file is newer.
$exeTime = (Get-Item $Exe).LastWriteTime
$newer = Get-ChildItem -Recurse -File (Join-Path $twoc 'src'), (Join-Path $repo 'xboxrecomp\src') -Include *.c, *.cpp, *.h |
         Where-Object { $_.LastWriteTime -gt $exeTime } | Select-Object -First 3
if ($newer) { Write-Warning ("Sources newer than the exe (rebuild?): " + (($newer | ForEach-Object Name) -join ', ')) }

# The exe's imports, each placed: shipped beside it, or part of Windows.
$deps = & $dumpbin.FullName /nologo /dependents $Exe | Where-Object { $_ -match '^\s+\S+\.dll\s*$' } | ForEach-Object { $_.Trim() }
$shipped = @('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'concrt140.dll')
$windows = @{
    'kernel32.dll' = 'Windows'; 'user32.dll' = 'Windows'; 'gdi32.dll' = 'Windows'; 'imm32.dll' = 'Windows';
    'ole32.dll' = 'Windows'; 'oleaut32.dll' = 'Windows'; 'advapi32.dll' = 'Windows'; 'setupapi.dll' = 'Windows';
    'shell32.dll' = 'Windows'; 'dwmapi.dll' = 'Windows'; 'winmm.dll' = 'Windows'; 'dbghelp.dll' = 'Windows';
    'version.dll' = 'Windows'; 'shcore.dll' = 'Windows'; 'hid.dll' = 'Windows'; 'cfgmgr32.dll' = 'Windows';
    'bcrypt.dll' = 'Windows (CNG; the game-files check''s SHA-256, notes 18.86)';
    'd3d11.dll' = 'Windows (DirectX 11)'; 'dxgi.dll' = 'Windows (DirectX 11)';
    'd3dcompiler_47.dll' = 'Windows 8.1+ (the HLSL compiler)';
    'xinput1_4.dll' = 'Windows 8+ (XInput)'; 'xaudio2_9.dll' = 'Windows 10+ (XAudio2)';
    'mfplat.dll' = 'Windows (Media Foundation; missing on N editions without the Media Feature Pack)';
    'mfreadwrite.dll' = 'Windows (Media Foundation; missing on N editions without the Media Feature Pack)'
}
$depLines = @()
foreach ($d in $deps) {
    $k = $d.ToLower()
    if ($shipped -contains $k) { $where = 'shipped in this folder (Visual C++ runtime ' + (Split-Path -Leaf (Split-Path -Parent (Split-Path -Parent $crt.FullName))) + ')' }
    elseif ($k -like 'api-ms-win-crt-*') { $where = 'Windows 10+ (the Universal C runtime)' }
    elseif ($windows.ContainsKey($k)) { $where = $windows[$k] }
    else { throw "The exe imports $d, which this script doesn't know: decide whether to ship it" }
    if ($shipped -contains $k -and -not (Test-Path (Join-Path $crt.FullName $d))) { throw "$d not in $($crt.FullName)" }
    $depLines += ('  {0,-40} {1}' -f $d, $where)
}

# The folder.
if (Test-Path $Out) {
    if (-not $Force) { throw "$Out exists; -Force replaces it" }
    if (-not (Test-Path (Join-Path $Out 'PACKAGE.txt'))) { throw "$Out exists and has no PACKAGE.txt: not a package this script made, left alone" }
    Remove-Item -Recurse -Force $Out
}
New-Item -ItemType Directory -Force -Path (Join-Path $Out 'tools') | Out-Null
$Out = (Resolve-Path $Out).Path

Write-Host "Packaging into $Out"
Copy-Item $Exe (Join-Path $Out 'crash_twoc.exe')
foreach ($d in $deps) { if ($shipped -contains $d.ToLower()) { Copy-Item (Join-Path $crt.FullName $d) $Out } }

# vrrprobe, built for the package (a temporary .cmd: vcvars64 needs cmd, and cmd wants CRLF).
$tmp = Join-Path $env:TEMP ("twoc_pkg_" + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $tmp | Out-Null
$src = Join-Path $twoc 'tools\diag\vrrprobe.cpp'
$cmd = "@echo off`r`ncall `"$vcvars`" >nul 2>nul`r`ncd /d `"$tmp`"`r`n" +
       "cl /nologo /std:c++17 /EHsc /O2 /MT `"$src`" /Fe:`"$Out\tools\vrrprobe.exe`" >`"$tmp\cl.log`" 2>&1`r`n"
[IO.File]::WriteAllText((Join-Path $tmp 'b.cmd'), $cmd, [Text.Encoding]::ASCII)
& cmd.exe /d /c (Join-Path $tmp 'b.cmd')
if (-not (Test-Path "$Out\tools\vrrprobe.exe")) { Get-Content "$tmp\cl.log"; throw 'vrrprobe build failed' }
Remove-Item -Recurse -Force $tmp

# The disc files, without saves (UDATA), their backups (UDATA_backups, notes 18.86), the title cache (TDATA) or our
# analysis output.
& robocopy $GameFiles (Join-Path $Out 'game_files') /E /XD (Join-Path $GameFiles 'UDATA') (Join-Path $GameFiles 'UDATA_backups') `
    (Join-Path $GameFiles 'TDATA') /XF crash_analysis.json /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }
if ($Save) {
    $Save = (Resolve-Path $Save).Path
    & robocopy $Save (Join-Path $Out 'game_files\UDATA') /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy of the save failed ($LASTEXITCODE)" }
}

# The text files, with CRLF line ends whatever git checked out (cmd needs them in .bat files).
function Put-Text([string]$from, [string]$to) {
    $t = [IO.File]::ReadAllText($from) -replace "`r`n", "`n" -replace "`n", "`r`n"
    [IO.File]::WriteAllText($to, $t, (New-Object Text.UTF8Encoding($false)))
}
$files = Join-Path $here 'files'
foreach ($f in Get-ChildItem -File $files) { Put-Text $f.FullName (Join-Path $Out $f.Name) }
Put-Text (Join-Path $files 'tools\collect_logs.ps1') (Join-Path $Out 'tools\collect_logs.ps1')
# Every third-party component's licence (notes 18.96; twoc/tools/package/third_party_licenses.py writes it).
Put-Text (Join-Path $twoc 'THIRD_PARTY_LICENSES.txt') (Join-Path $Out 'THIRD_PARTY_LICENSES.txt')

# PACKAGE.txt: what this is built from.
function Git1([string]$dir, [string[]]$a) { try { (& git -C $dir @a 2>$null | Select-Object -First 1) } catch { '' } }
# The commits the exe's sources come from (twoc's src and build files; xboxrecomp), and whether
# those had changes not committed (which the exe may then hold).
$xrDir = Join-Path $repo 'xboxrecomp'
$head = Git1 $repo @('log', '-1', '--format=%h %ad %s', '--date=iso', '--', 'twoc/src', 'twoc/CMakeLists.txt', 'twoc/seed_functions.json')
$dirty = if (Git1 $repo @('status', '--porcelain', '--untracked-files=no', '--', 'twoc/src', 'twoc/CMakeLists.txt', 'twoc/seed_functions.json')) { ' (with uncommitted changes)' } else { '' }
$xr = Git1 $xrDir @('log', '-1', '--format=%h %ad', '--date=iso')
if (Git1 $xrDir @('status', '--porcelain', '--untracked-files=no')) { $xr += ' (with uncommitted changes)' }
$hash = (Get-FileHash -Algorithm SHA256 (Join-Path $Out 'crash_twoc.exe')).Hash
# Link-time code generation (notes 18.87): the exe's build folder's CMakeCache.txt (build\Release\..).
$cache = Join-Path (Split-Path -Parent (Split-Path -Parent $Exe)) 'CMakeCache.txt'
$ltcg = if (-not (Test-Path $cache)) { "unknown (no CMakeCache.txt beside the exe's folder)" }
        elseif (Select-String -Quiet -Pattern '^TWOC_LTCG:BOOL=ON' $cache) { 'on (TWOC_LTCG=ON)' }
        else { 'off (TWOC_LTCG=OFF)' }
$gf = Get-ChildItem -Recurse -File (Join-Path $Out 'game_files')
$pkg = @(
    "Crash TWOC, Legion Go package, made $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz') by twoc\tools\package\make_package.ps1",
    "",
    "Build: twoc $head$dirty",
    "       xboxrecomp $xr",
    "crash_twoc.exe: $((Get-Item $Exe).Length) bytes, built $($exeTime.ToString('yyyy-MM-dd HH:mm:ss')), SHA-256 $hash",
    "                link-time code generation: $ltcg",
    "                profile-guided optimisation: $pgoLine",
    ("game_files: {0} files, {1:N0} MB{2}" -f $gf.Count, (($gf | Measure-Object Length -Sum).Sum / 1MB), $(if ($Save) { ", with a save copied in from $Save" } else { ', no saves' })),
    "",
    "DLLs the exe imports (dumpbin /dependents) and where they come from:"
) + $depLines + @(
    "",
    "Loaded at run time when present: nvapi64.dll (NVIDIA only; the variable-refresh query), xaudio2_9.dll,",
    "powrprof.dll and pdh.dll (Windows; the power log, notes 18.94), the GPU's own driver DLLs. Needs: 64-bit",
    "Windows 10 or 11 with a DirectX 11 GPU."
)
[IO.File]::WriteAllText((Join-Path $Out 'PACKAGE.txt'), (($pkg -join "`r`n") + "`r`n"), (New-Object Text.UTF8Encoding($false)))

$all = Get-ChildItem -Recurse -File $Out
Write-Host ("Done: {0}, {1} files, {2:N0} MB" -f $Out, $all.Count, (($all | Measure-Object Length -Sum).Sum / 1MB))
$depLines | ForEach-Object { Write-Host $_ }
