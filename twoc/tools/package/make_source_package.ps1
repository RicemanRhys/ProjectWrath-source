# The whole program's source package (step 107, notes 18.110; docs\SHARING.md part 1): one zip with twoc's own source
# and build files, xboxrecomp as built with every local commit (never pushed upstream; as patches too), and the
# build instructions, for developers or a public repository later. Since step 108 (notes 18.111) it is NOT the LGPL
# mechanism: the LGPL audio code is xemu_apu.dll, whose own source zip (make_apu_source_package.ps1) goes with it.
# Made locally; this script publishes nothing.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File twoc\tools\package\make_source_package.ps1 [-Out ZIP] [-Force]
#
# Left out, and said so in SOURCE.txt: the recompiled game code (twoc\src\recomp\gen, made from the player's own
# default.xbe by lift.ps1; gitignored), the disc's files, twoc\docs\option_b (a disassembly listing of the game's
# statically linked D3D library), twoc\tools\diag and capturekit (the test harness, with this PC's paths), the
# project notes, the user's settings, saves and logs. Taken from the commits (git archive), so changes not
# committed are not in it (the script warns). package_audit.ps1 then checks the folder for game files and this
# PC's name and user folder.
# Windows PowerShell 5.1; ASCII only.
param([string]$Out, [switch]$Force)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$twoc = Split-Path -Parent (Split-Path -Parent $here)
$repo = Split-Path -Parent $twoc
$xr = Join-Path $repo 'xboxrecomp'
function Git1([string]$dir, [string[]]$a) { (& git -C $dir @a 2>$null | Select-Object -First 1) }
$th = Git1 $repo @('rev-parse', '--short', 'HEAD')
$xh = Git1 $xr @('rev-parse', '--short', 'HEAD')
if (-not $Out) { $Out = Join-Path ([Environment]::GetFolderPath('Desktop')) "Crash\SourcePackage\ProjectWrath-source_${th}_xboxrecomp-${xh}.zip" }
if ((Test-Path -LiteralPath $Out) -and -not $Force) { throw "$Out exists; -Force replaces it" }

# twoc: the source and build files, as committed.
$include = @('twoc/src', 'twoc/CMakeLists.txt', 'twoc/data', 'twoc/third_party', 'twoc/tools/lift_flag_join.py',
             'twoc/tools/lift_frndint.py', 'twoc/tools/wrap_callers.py', 'twoc/tools/build_info.cmake',
             'twoc/tools/package', 'twoc/lift.ps1', 'twoc/analyze.ps1', 'twoc/run_twoc.ps1', 'twoc/seed_functions.json',
             'twoc/THIRD_PARTY_LICENSES.txt', 'twoc/LICENSE', 'twoc/NOTICE', 'twoc/docs/SHARING.md', 'twoc/docs/RELEASE_CHECKLIST.md', 'twoc/.gitignore',
             'twoc/apu_dll')
$exclude = @('twoc/tools/package/smoke104.py')     # a test script with this PC's paths
$dirty = @(& git -C $repo status --porcelain --untracked-files=no -- @include 2>$null)
if ($dirty.Count) { Write-Warning ("twoc has changes not committed (not in the zip): " + (($dirty | Select-Object -First 5) -join '; ')) }
$xdirty = @(& git -C $xr status --porcelain --untracked-files=no 2>$null)
if ($xdirty.Count) { Write-Warning ("xboxrecomp has changes not committed (not in the zip): " + (($xdirty | Select-Object -First 5) -join '; ')) }

$tmp = Join-Path $env:TEMP ('twoc_src_' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$st = Join-Path $tmp 'ProjectWrath-source'
New-Item -ItemType Directory -Force -Path $st | Out-Null
try {
    $z1 = Join-Path $tmp 'twoc.zip'
    & git -C $repo archive --format=zip -o $z1 HEAD -- @include
    if ($LASTEXITCODE) { throw 'git archive (twoc) failed' }
    Expand-Archive -LiteralPath $z1 -DestinationPath $st
    foreach ($e in $exclude) { $p = Join-Path $st $e.Replace('/', '\'); if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p } }
    $z2 = Join-Path $tmp 'xboxrecomp.zip'
    & git -C $xr archive --format=zip -o $z2 HEAD
    if ($LASTEXITCODE) { throw 'git archive (xboxrecomp) failed' }
    Expand-Archive -LiteralPath $z2 -DestinationPath (Join-Path $st 'xboxrecomp')
    # xboxrecomp's own test program (upstream's, not the game's; not needed to build): the audit allows no .xbe
    $tx = Join-Path $st 'xboxrecomp\tools\conformance\test.xbe'
    if (Test-Path -LiteralPath $tx) { Remove-Item -LiteralPath $tx }
    $base = Git1 $xr @('merge-base', 'HEAD', 'origin/main')
    $local = @(& git -C $xr log --format='%h %ad %s' --date=short "$base..HEAD")
    $pd = Join-Path $st 'xboxrecomp-local-commits'
    New-Item -ItemType Directory -Force -Path $pd | Out-Null
    & git -C $xr format-patch -q -o $pd "$base..HEAD"
    if ($LASTEXITCODE) { throw 'git format-patch (xboxrecomp) failed' }
    Remove-Item -LiteralPath $z1, $z2

    $src = @(
        'Project Wrath: the source package',
        '================================',
        '',
        "Made $(Get-Date -Format 'yyyy-MM-dd') by twoc\tools\package\make_source_package.ps1 from:",
        "  twoc        $(Git1 $repo @('log', '-1', '--format=%h %ad %s', '--date=short'))",
        "  xboxrecomp  $(Git1 $xr @('log', '-1', '--format=%h %ad %s', '--date=short'))",
        "              upstream: https://github.com/sp00nznet/xboxrecomp, branched at $base;",
        "              $($local.Count) local commits on top, never pushed (xboxrecomp-local-commits\*.patch,",
        '              also already applied in xboxrecomp\)',
        '',
        'The LGPL part: xboxrecomp\src\apu (the Xbox audio processor, from xemu; LGPL-2.1-or-later, see',
        'xboxrecomp\LICENSE, xboxrecomp\NOTICE and twoc\THIRD_PARTY_LICENSES.txt). It is built as its own',
        'library, xemu_apu.dll (twoc\apu_dll), beside the exe; its own source zip (make_apu_source_package.ps1)',
        'is what goes with the DLL. This zip is the whole program''s source, for developers.',
        '',
        'What is in here:',
        '  twoc\src, CMakeLists.txt, data, third_party   the port''s source, its build file, the disc''s',
        '                                                manifest (sizes and hashes only), other people''s',
        '                                                code it builds in (ImGui, SMAA, XeGTAO, volk, Vulkan',
        '                                                headers; SDL is fetched by CMake)',
        '  twoc\lift.ps1, analyze.ps1, tools\*.py       the steps that make the recompiled code',
        '  twoc\tools\package                           the package scripts (and this one)',
        '  twoc\docs\SHARING.md                         the sharing checklist',
        '  twoc\LICENSE, twoc\NOTICE                     this project''s own code: MIT (LICENSE); what that covers',
        '                                                and what it doesn''t (NOTICE)',
        '  xboxrecomp\                                  the runtime and the recompiler, as built',
        '  BUILD.txt                                    how to build the exe from this',
        '',
        'The repository keeps its working names (the twoc folder, the CMake target crash_twoc, which builds',
        'crash_twoc.exe); the packages ship that exe as ProjectWrath.exe, and it takes its names from its own',
        'file name (notes 18.112, twoc\src\names.h).',
        '',
        'Left out:',
        '  - the recompiled game code (twoc\src\recomp\gen, about 32 MB of C): it is made from the game''s',
        '    own program (default.xbe) by lift.ps1, so it comes from your own disc (BUILD.txt step 3);',
        '  - the game''s files (your own disc), saves, settings and logs;',
        '  - twoc\docs\option_b (a disassembly listing of the D3D library inside the game''s program);',
        '  - the test harness (twoc\tools\diag, capturekit) and the project notes (they hold this PC''s',
        '    paths and the development history; nothing in them is needed to build);',
        '  - xboxrecomp\tools\conformance\test.xbe (upstream''s own test program for its conformance tests,',
        '    not the game''s; left out because the package audit allows no Xbox program at all).',
        '',
        'xboxrecomp''s local commits:'
    ) + ($local | ForEach-Object { "  $_" })
    [IO.File]::WriteAllText((Join-Path $st 'SOURCE.txt'), (($src -join "`r`n") + "`r`n"), (New-Object Text.UTF8Encoding($false)))
    $build = @(
        'Building Project Wrath from the source package',
        '==============================================',
        '',
        'As done on the development PC (Windows 11). Not yet re-run from this zip on a clean PC.',
        '',
        'You need: Windows 10 or 11, 64-bit; Visual Studio 2022 with "Desktop development with C++" (MSVC',
        'x64, the Windows SDK, CMake); Python 3.12 or later; Git; LLVM (the lifter''s tests); your own',
        'copy of the game: default.xbe from the USA Xbox disc of Crash Bandicoot: The Wrath of Cortex',
        '(SHA-256 e8d7cbf225d899eb88227c11d1f40434c34e27c1b23ed946fb2c3471168d2f4d).',
        '',
        'In an "x64 Native Tools Command Prompt for VS 2022", from the folder holding twoc\ and xboxrecomp\:',
        ' 1. python -m venv .venv  and  .venv\Scripts\pip install capstone pefile pytest',
        '    (what the development PC''s .venv uses for the recompiler''s tools)',
        ' 2. game_files\: extract the disc there (or run a built crash_twoc.exe once: its setup does it), so',
        '    that game_files\default.xbe and game_files\Crashdat are there.',
        ' 3. The analysis and the lift (from xboxrecomp\, with ..\.venv\Scripts\python.exe):',
        '      python -m tools.xbe_parser ..\game_files\default.xbe --json ..\game_files\crash_analysis.json',
        '      python -m tools.disasm ..\game_files\default.xbe -v',
        '      python -m tools.func_id ..\game_files\default.xbe -v',
        '      python -m tools.abi_analysis ..\game_files\default.xbe -v',
        '    then  powershell -File twoc\lift.ps1  (writes twoc\src\recomp\gen).',
        ' 4. cmake -S twoc -B twoc\build   and   cmake --build twoc\build --config Release --parallel',
        '    (CMake downloads SDL 3 and builds xboxrecomp\ as part of it; without git the About tab''s build',
        '    line says "unknown"). The exe: twoc\build\Release\crash_twoc.exe (the packages copy it as',
        '    ProjectWrath.exe, the name it is meant to run under: make_package.ps1).',
        ' 5. To change the LGPL audio code: edit xboxrecomp\src\apu, then step 4 again (it rebuilds',
        '    twoc\build\Release\xemu_apu.dll beside the exe; only that file changes).',
        '',
        'To make the program''s folder: twoc\tools\package\make_package.ps1 -Target Share.'
    )
    [IO.File]::WriteAllText((Join-Path $st 'BUILD.txt'), (($build -join "`r`n") + "`r`n"), (New-Object Text.UTF8Encoding($false)))

    # No game file, no trace of this PC.
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'package_audit.ps1') -Dir $st
    if ($LASTEXITCODE) { throw 'package_audit.ps1 failed on the source package: see above (nothing was zipped)' }

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Out) | Out-Null
    if (Test-Path -LiteralPath $Out) { Remove-Item -LiteralPath $Out }
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $za = [IO.Compression.ZipFile]::Open($Out, 'Create')
    try {
        foreach ($f in Get-ChildItem -Recurse -File $tmp) {
            $name = $f.FullName.Substring($tmp.Length + 1).Replace('\', '/')
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($za, $f.FullName, $name)
        }
    } finally { $za.Dispose() }
    $n = @(Get-ChildItem -Recurse -File $st).Count
    Write-Host ("Done: {0} ({1:N1} MB, {2} files; xboxrecomp {3} local commits)" -f $Out, ((Get-Item -LiteralPath $Out).Length / 1MB), $n, $local.Count)
} finally {
    if (Test-Path -LiteralPath $tmp) { Remove-Item -Recurse -Force -LiteralPath $tmp }
}
