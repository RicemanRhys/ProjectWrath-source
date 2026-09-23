# Run crash_twoc.exe from the repo root with the environment variables the bring-up needs.
#
#   RECOMP_VBLANK=1          the runtime raises the GPU vblank interrupt at 60 Hz (without it the game
#                            waits forever on the splash screen)
#   RECOMP_PB_EXEC=1         run the recompiled D3D library's push buffers through the toolkit's NV2A
#                            executor (without it nothing is drawn)
#   RECOMP_AC97_READY=1      report the AC'97 codec ready and route APU registers to the emulated APU.
#                            Without it DirectSoundCreate fails (DSERR_NODRIVER), no DSOUND stream is ever
#                            created, and the title screen waits forever for its music (notes 18.24/18.25)
#   RECOMP_WATCHDOG_SECS=N   the runtime ends the process after N seconds (exit code 3). This is the normal
#                            way a run ends for now: there is no quit path yet
#
# Usage, from anywhere (script execution is off by default on this machine, so bypass it for this process):
#   powershell -NoProfile -ExecutionPolicy Bypass -File twoc\run_twoc.ps1
#   ... -File twoc\run_twoc.ps1 -Seconds 30
#   ... -File twoc\run_twoc.ps1 -DumpDir dumps          # also write framebuffer BMPs into .\dumps
#   ... -File twoc\run_twoc.ps1 -Window                 # also open the 640x480 framebuffer window
#
# Logs go to run_twoc.out.log and run_twoc.err.log in the repo root (gitignored). The BMPs are named
# shot000.bmp, shot001.bmp ...; the executor writes the first eight frames that contain geometry and one
# more about every 10 seconds. Build first: cmake --build twoc\build --config Release --parallel
param(
    [int]$Seconds = 90,
    [string]$DumpDir,
    [switch]$Window
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $repo 'twoc\build\Release\crash_twoc.exe'
if (-not (Test-Path $exe)) { throw "Not built: $exe (cmake --build twoc\build --config Release --parallel)" }
if (-not (Test-Path (Join-Path $repo 'game_files\default.xbe'))) { throw "game_files\default.xbe is missing" }

$env:RECOMP_VBLANK        = '1'
$env:RECOMP_PB_EXEC       = '1'
$env:RECOMP_AC97_READY    = '1'
$env:RECOMP_WATCHDOG_SECS = "$Seconds"
if ($Window) { $env:RECOMP_FB_WINDOW = '1' }
if ($DumpDir) {
    New-Item -ItemType Directory -Force -Path $DumpDir | Out-Null
    $env:RECOMP_FB_DUMP = Join-Path (Resolve-Path $DumpDir) 'shot'
}

$out = Join-Path $repo 'run_twoc.out.log'
$err = Join-Path $repo 'run_twoc.err.log'
Write-Host "Running for $Seconds s from $repo (logs: run_twoc.out.log, run_twoc.err.log)"
$p = Start-Process -FilePath $exe -WorkingDirectory $repo -NoNewWindow -PassThru `
        -RedirectStandardOutput $out -RedirectStandardError $err
$null = $p.Handle    # keeps the exit code readable after WaitForExit (Windows PowerShell 5.1 quirk)
$t0 = Get-Date
# Hard limit: the watchdog should end the process at $Seconds; kill it if it has not by then.
if (-not $p.WaitForExit(($Seconds + 30) * 1000)) {
    $p.Kill()
    Write-Host "Hard limit ($($Seconds + 30) s) reached; process killed."
} else {
    $p.WaitForExit()
    $took = [int]((Get-Date) - $t0).TotalSeconds
    if ($p.ExitCode -eq 3) { Write-Host "Watchdog ended the run after $took s (exit code 3, the normal end)." }
    else { Write-Host "Exited with code $($p.ExitCode)." }
}
if ($DumpDir) { Write-Host ("Framebuffer dumps: {0} file(s) in {1}" -f (Get-ChildItem $DumpDir -Filter 'shot*').Count, (Resolve-Path $DumpDir)) }
