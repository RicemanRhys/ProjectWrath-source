# The audio library's source package (step 108, notes 18.111): one zip with the complete source of xemu_apu.dll as
# built, so whoever gets the game can rebuild that DLL and put their copy in its place (the LGPL's terms for the
# library the program uses). Made locally; this script publishes nothing.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File twoc\tools\package\make_apu_source_package.ps1 [-Out ZIP] [-Force]
#
# Default ZIP: Desktop\Crash\SourcePackage\xemu_apu-source_xboxrecomp-<commit>_twoc-<commit>.zip. In it, one folder,
# xemu_apu-source\:
#   src\apu\        xboxrecomp's APU files the DLL is built from, as committed (git show <commit>:<path>): from xemu,
#                   LGPL-2.1-or-later: apu_core.c, apu_vp.c, apu_dsp.c, apu_regs.h, apu_state.h, apu_debug.h,
#                   fpconv.h, apu_shim.h, apu.h (its header says it is extracted from xemu too); xboxrecomp's own
#                   (MIT): apu_xaudio2.c, apu_xaudio2.h, apu_mmio_hook.c
#   src\nv2a\qemu_shim.h, src\platform\xbox_winnt.h   xboxrecomp's own headers they include (MIT)
#   CMakeLists.txt, xemu_apu_dll.c, xemu_apu_api.h, build.cmd   twoc\apu_dll (MIT): the build and the export
#   changes\        this project's changes to those files: xboxrecomp's local commits that touch them, as patches
#   LICENSES\       LGPL-2.1.txt, MIT-xboxrecomp.txt, MIT-twoc.txt
#   NOTICE.txt      xemu's attribution, each file's licence, what was changed and the commits; README.txt: build and swap
# Refuses if any of those files has changes not committed (it wouldn't be the source as built), and runs
# package_audit.ps1 on the folder before zipping. -Out a .zip path; -Force replaces it.
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
if (-not $Out) { $Out = Join-Path ([Environment]::GetFolderPath('Desktop')) "Crash\SourcePackage\xemu_apu-source_xboxrecomp-${xh}_twoc-${th}.zip" }
if ((Test-Path -LiteralPath $Out) -and -not $Force) { throw "$Out exists; -Force replaces it" }

$lgpl = @('apu_core.c', 'apu_vp.c', 'apu_dsp.c', 'apu_regs.h', 'apu_state.h', 'apu_debug.h', 'fpconv.h', 'apu_shim.h', 'apu.h')
$mitApu = @('apu_xaudio2.c', 'apu_xaudio2.h', 'apu_mmio_hook.c')
$xrFiles = @($lgpl + $mitApu | ForEach-Object { "src/apu/$_" }) + @('src/nv2a/qemu_shim.h', 'src/platform/xbox_winnt.h')
$twFiles = @('twoc/apu_dll/CMakeLists.txt', 'twoc/apu_dll/xemu_apu_dll.c', 'twoc/apu_dll/xemu_apu_api.h', 'twoc/apu_dll/build.cmd')

# As built: nothing changed and not committed.
$d1 = @(& git -C $xr status --porcelain -- @xrFiles 2>$null)
$d2 = @(& git -C $repo status --porcelain -- @twFiles 2>$null)
if ($d1.Count -or $d2.Count) { throw ("Changes not committed in the DLL's files (commit them first): " + (($d1 + $d2) -join '; ')) }

$tmp = Join-Path $env:TEMP ('twoc_apusrc_' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$st = Join-Path $tmp 'xemu_apu-source'
function Put-Blob([string]$dir, [string]$commitPath, [string]$to, [switch]$Crlf) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
    $p = Start-Process -FilePath git -ArgumentList @('-C', "`"$dir`"", 'show', "HEAD:$commitPath") -NoNewWindow -Wait -PassThru `
         -RedirectStandardOutput $to
    if ($p.ExitCode) { throw "git show HEAD:$commitPath failed" }
    if ($Crlf) {
        $t = [IO.File]::ReadAllText($to) -replace "`r`n", "`n" -replace "`n", "`r`n"
        [IO.File]::WriteAllText($to, $t, (New-Object Text.UTF8Encoding($false)))
    }
}
function Put-Text([string]$to, [string[]]$lines) {
    [IO.File]::WriteAllText($to, (($lines -join "`r`n") + "`r`n"), (New-Object Text.UTF8Encoding($false)))
}
try {
    New-Item -ItemType Directory -Force -Path $st | Out-Null
    foreach ($f in $xrFiles) { Put-Blob $xr $f (Join-Path $st $f.Replace('/', '\')) }
    foreach ($f in $twFiles) { Put-Blob $repo $f (Join-Path $st (Split-Path -Leaf $f)) -Crlf:($f -like '*.cmd') }
    $lic = Join-Path $st 'LICENSES'
    Put-Blob $xr 'LICENSES/LGPL-2.1.txt' (Join-Path $lic 'LGPL-2.1.txt')
    Put-Blob $xr 'LICENSE' (Join-Path $lic 'MIT-xboxrecomp.txt')
    Put-Blob $repo 'twoc/LICENSE' (Join-Path $lic 'MIT-twoc.txt')

    # This project's changes to these files: xboxrecomp's commits since its upstream (origin/main) that touch them.
    $base = Git1 $xr @('merge-base', 'HEAD', 'origin/main')
    $local = @(& git -C $xr log --reverse --format='%h %ad %s' --date=short "$base..HEAD" -- @xrFiles)
    $chg = Join-Path $st 'changes'
    New-Item -ItemType Directory -Force -Path $chg | Out-Null
    & git -C $xr format-patch -q -o $chg "$base..HEAD" -- @xrFiles
    if ($LASTEXITCODE) { throw 'git format-patch failed' }
    $notice = (& git -C $xr show HEAD:NOTICE) -join "`n"
    $i = $notice.IndexOf('LGPL-2.1-or-later'); $j = $notice.IndexOf('Derived algorithms')
    if ($i -lt 0 -or $j -lt $i) { throw "xboxrecomp's NOTICE: its LGPL section wasn't found" }
    $xemuNotice = $notice.Substring($i, $j - $i).TrimEnd() -split "`n"

    $n = @(
        'xemu_apu.dll: notice',
        '=====================',
        '',
        'xemu_apu.dll is the Xbox audio processor (MCPX APU) emulation used by Crash TWOC (crash_twoc.exe). Its',
        'emulation code comes from the xemu project (https://github.com/xemu-project/xemu), extracted by xboxrecomp',
        '(https://github.com/sp00nznet/xboxrecomp), and is licensed under the GNU Lesser General Public License,',
        'version 2.1 or (at your option) any later version: LICENSES\LGPL-2.1.txt.',
        '',
        "Built from: xboxrecomp $(Git1 $xr @('log', '-1', '--format=%h %ad', '--date=short')) (upstream branched at $base)",
        "            twoc       $(Git1 $repo @('log', '-1', '--format=%h %ad', '--date=short'))",
        '',
        'The files and their licences',
        '----------------------------',
        '  LGPL-2.1-or-later (from xemu; each file keeps its own copyright lines):'
    ) + ($lgpl | ForEach-Object { "    src\apu\$_" }) + @(
        '  MIT, xboxrecomp''s own (LICENSES\MIT-xboxrecomp.txt): src\apu\apu_xaudio2.c, apu_xaudio2.h (the XAudio2',
        '    output), src\apu\apu_mmio_hook.c (decodes the game''s register accesses), src\nv2a\qemu_shim.h,',
        '    src\platform\xbox_winnt.h (type and threading shims the APU files include)',
        '  MIT, Crash TWOC''s own (LICENSES\MIT-twoc.txt): xemu_apu_dll.c (the export), xemu_apu_api.h (the',
        '    interface with crash_twoc.exe), CMakeLists.txt, build.cmd',
        '',
        'xemu''s attribution, as xboxrecomp''s NOTICE gives it',
        '------------------------------------------------'
    ) + ($xemuNotice | ForEach-Object { '  ' + $_.TrimEnd() }) + @(
        '',
        'What was changed',
        '----------------',
        'xboxrecomp extracted these files from xemu and adapted them to run outside it (its upstream history).',
        'This project then changed them in the commits below, made locally on xboxrecomp and not published',
        'upstream; each is in changes\ as a patch (git format-patch, limited to these files), and the files in',
        'src\ already include them:'
    ) + ($local | ForEach-Object { "  $_" }) + @(
        '',
        'Twoc builds the DLL from these files unchanged (twoc\apu_dll\CMakeLists.txt, the same as this folder''s).',
        'Before step 108 the same files were linked into crash_twoc.exe as a static library.'
    )
    Put-Text (Join-Path $st 'NOTICE.txt') $n

    $r = @(
        'xemu_apu.dll: the source',
        '=========================',
        '',
        'This is the complete source of xemu_apu.dll, the audio library that comes with Crash TWOC (crash_twoc.exe),',
        'as it was built. The library is under the GNU LGPL 2.1 or later (NOTICE.txt, LICENSES\). You may change it,',
        'rebuild it and use your copy with the game in place of the one it came with.',
        '',
        'Rebuild it',
        '----------',
        'You need 64-bit Windows 10 or 11 and Visual Studio 2022 (Community is free) or the Build Tools for Visual',
        'Studio 2022, with the "Desktop development with C++" workload (it includes the C++ compiler, the Windows',
        'SDK and CMake). Unpack this zip into a folder with a short path (for example C:\xemu_apu-source: Visual',
        'Studio''s build tools fail on paths over 260 characters, and the build adds about 120 of its own).',
        'Then double-click build.cmd (or run it in a command prompt). It builds into the folder',
        '"build" and copies the result here as xemu_apu.dll. By hand, from this folder:',
        '    cmake -S . -B build -G "Visual Studio 17 2022" -A x64',
        '    cmake --build build --config Release',
        '  (the DLL: build\Release\xemu_apu.dll). Built with the same Visual Studio version as the one shipped',
        '  (MSVC 14.44), it comes out byte for byte the same as the shipped xemu_apu.dll (CMakeLists.txt builds it',
        '  with /Brepro); with another version it differs in its bytes but not in what it does.',
        '',
        'Use your copy',
        '-------------',
        '1. Close the game.',
        '2. In the game''s folder (where crash_twoc.exe is), keep the old xemu_apu.dll somewhere else if you may want',
        '   it back, then copy your xemu_apu.dll there.',
        '3. Start the game. Its log (run_twoc.err.log) names the DLL it loaded, with its size and SHA-256',
        '   ("[APU-DLL] ... loaded").',
        'The game loads xemu_apu.dll only from its own folder. Your build has to keep the interface in',
        'xemu_apu_api.h (version 1): one export, xemu_apu_get_api, handing over a table of functions. If the version',
        'doesn''t match, the game says so and doesn''t start.',
        '',
        'Change it',
        '---------',
        'The emulation is in src\apu: apu_vp.c (the voice processor: decoding, pitch, volume, the HRTF filter),',
        'apu_dsp.c (the mixdown of the bins to the output), apu_core.c (the registers, the frame thread, the output',
        'pacing). RECOMP_APU_OUT_WAV=<file.wav> makes the DLL write everything it outputs to a WAV file, for',
        'comparing a change with the original.'
    )
    Put-Text (Join-Path $st 'README.txt') $r

    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'package_audit.ps1') -Dir $st
    if ($LASTEXITCODE) { throw 'package_audit.ps1 failed on the audio library''s source: see above (nothing was zipped)' }

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
    $cnt = @(Get-ChildItem -Recurse -File $st).Count
    Write-Host ("Done: {0} ({1:N0} KB, {2} files; {3} local commits touch them)" -f $Out, ((Get-Item -LiteralPath $Out).Length / 1KB), $cnt, $local.Count)
} finally {
    if (Test-Path -LiteralPath $tmp) { Remove-Item -Recurse -Force -LiteralPath $tmp }
}
