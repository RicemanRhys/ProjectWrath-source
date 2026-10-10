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
# Test-only options, off unless their switch is given (recomp_manual.c, "Test-only options"):
#   -FastTitle   RECOMP_DEV_FAST_TITLE=1: the title animation runs at 64 instead of 0.5 per update, so the
#                main menu opens soon after the title music starts instead of ~12.5 minutes in
#   -AutoStart   RECOMP_DEV_AUTOSTART=1: presses A on port 0 at +5 s and +20 s after the main menu opens
#                (menu 4, then "start game"); stands in a virtual pad on port 0 if no real one is there
#
# Option B mirror (notes 18.31 on), off unless its switch is given:
#   -Bridge      RECOMP_D3D_BRIDGE=1: also opens "Crash TWOC - D3D bridge (host)", a second window where
#                the game's D3D calls are mirrored through xbox_d3d8. The native D3D and the executor still
#                run as before
#   -HostOnly    -Bridge with RECOMP_PB_EXEC unset (S7, notes 18.40): the mirror is the only renderer. The
#                fence mirror completes the push buffers, and the bridge's host present paces the game at
#                59.94 Hz (RECOMP_D3D_BRIDGE_HZ overrides the rate, 0 turns the limiter off). No -DumpDir
#                shots from the executor, only the host's hostNNN.bmp
#   -Widescreen  RECOMP_WIDESCREEN=1 (needs -Bridge or -HostOnly; notes 18.56): 16:9. The game widens every
#                projection that renders to the screen (the camera aspect, sub_000956B0) and the host renders
#                a 16:9 picture (full-width 2D stretched, the HUD and menus pillarboxed). Off, nothing changes
#                Other ratios (notes 18.67): the overlay's Aspect ratio (16:10, 21:9), or by hand
#                $env:RECOMP_WIDESCREEN = '16:10' (any W:H wider than 4:3); -Widescreen always means 16:9
#   -RenderSize WxH | native   RECOMP_RENDER_SIZE (needs -Bridge or -HostOnly; notes 18.56): the host renders at
#                that size (the largest of the picture's aspect that fits: 1920x1080 is 1440x1080 at 4:3, all of it
#                at 16:9), in a window sized in real pixels; "native" is the monitor's mode. Without it the host
#                renders at the game's 480 lines, as before
#   -Fullscreen  RECOMP_FULLSCREEN=1 (needs -Bridge or -HostOnly; notes 18.57): the host window starts borderless
#                over its monitor, the picture letterboxed at its own aspect. Alt+Enter on the window switches
#                between that and the window at any time
#
# Settings (notes 18.57): Esc on the bridge window opens the settings overlay and holds the game until Esc
# again (fullscreen, aspect ratio, render size, frame pacing, anisotropic filtering, volume; applied at once).
# They are saved to twoc\build\Release\crash_twoc.ini, which the next start reads. A switch given here wins
# over that file for this run: -Widescreen, -Fullscreen, -RenderSize WxH | native | game, and to switch one
# off -Widescreen:$false or -Fullscreen:$false. Without the file, and with no switch, everything is as before.
#
# Usage, from anywhere (script execution is off by default on this machine, so bypass it for this process):
#   powershell -NoProfile -ExecutionPolicy Bypass -File twoc\run_twoc.ps1
#   ... -File twoc\run_twoc.ps1 -Seconds 30
#   ... -File twoc\run_twoc.ps1 -DumpDir dumps          # also write framebuffer BMPs into .\dumps
#   ... -File twoc\run_twoc.ps1 -Window                 # also open the 640x480 framebuffer window
#   ... -File twoc\run_twoc.ps1 -FastTitle -AutoStart   # straight to the next level's load, unattended
#   ... -File twoc\run_twoc.ps1 -Bridge -Seconds 900    # play, with the mirrored picture in its own window
#   ... -File twoc\run_twoc.ps1 -HostOnly -Seconds 900  # play on the mirror alone, at 60 fps
#   ... -File twoc\run_twoc.ps1 -HostOnly -Widescreen -RenderSize 1920x1080 -Seconds 900   # 16:9 at 1080p
#   ... -File twoc\run_twoc.ps1 -HostOnly -Widescreen -RenderSize native -Fullscreen -Seconds 900
#
# Logs go to run_twoc.out.log and run_twoc.err.log in the repo root (gitignored). The BMPs are named
# shot000.bmp, shot001.bmp ...; the executor writes the first eight frames that contain geometry and one
# more about every 10 seconds. Build first: cmake --build twoc\build --config Release --parallel
param(
    [int]$Seconds = 90,
    [string]$DumpDir,
    [switch]$Window,
    [switch]$FastTitle,
    [switch]$AutoStart,
    [switch]$Bridge,
    [switch]$HostOnly,
    [switch]$Widescreen,
    [string]$RenderSize,
    [switch]$Fullscreen
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $repo 'twoc\build\Release\crash_twoc.exe'
if (-not (Test-Path $exe)) { throw "Not built: $exe (cmake --build twoc\build --config Release --parallel)" }
if (-not (Test-Path (Join-Path $repo 'game_files\default.xbe'))) { throw "game_files\default.xbe is missing" }

if ($HostOnly) { $Bridge = $true }
$env:RECOMP_VBLANK        = '1'
if ($HostOnly) { Remove-Item Env:RECOMP_PB_EXEC -ErrorAction SilentlyContinue } else { $env:RECOMP_PB_EXEC = '1' }
$env:RECOMP_AC97_READY    = '1'
$env:RECOMP_WATCHDOG_SECS = "$Seconds"
if ($Window) { $env:RECOMP_FB_WINDOW = '1' }
# Set or cleared explicitly, so a value left in the calling shell can't switch one on unasked.
if ($FastTitle) { $env:RECOMP_DEV_FAST_TITLE = '1' } else { Remove-Item Env:RECOMP_DEV_FAST_TITLE -ErrorAction SilentlyContinue }
if ($AutoStart) { $env:RECOMP_DEV_AUTOSTART  = '1' } else { Remove-Item Env:RECOMP_DEV_AUTOSTART  -ErrorAction SilentlyContinue }
if ($Bridge)    { $env:RECOMP_D3D_BRIDGE     = '1' } else { Remove-Item Env:RECOMP_D3D_BRIDGE     -ErrorAction SilentlyContinue }
# A switch given as -Name:$false is passed on as '0', so it wins over crash_twoc.ini; one not given is
# cleared and leaves the setting to that file (notes 18.57).
if ($Widescreen) { $env:RECOMP_WIDESCREEN    = '1' }
elseif ($PSBoundParameters.ContainsKey('Widescreen')) { $env:RECOMP_WIDESCREEN = '0' }
else { Remove-Item Env:RECOMP_WIDESCREEN     -ErrorAction SilentlyContinue }
if ($RenderSize -eq 'game') { $RenderSize = '0' }
if ($RenderSize) { $env:RECOMP_RENDER_SIZE   = $RenderSize } else { Remove-Item Env:RECOMP_RENDER_SIZE -ErrorAction SilentlyContinue }
if ($Fullscreen) { $env:RECOMP_FULLSCREEN    = '1' }
elseif ($PSBoundParameters.ContainsKey('Fullscreen')) { $env:RECOMP_FULLSCREEN = '0' }
else { Remove-Item Env:RECOMP_FULLSCREEN     -ErrorAction SilentlyContinue }
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
if ($DumpDir) { Write-Host ("Framebuffer dumps: {0} executor, {1} host file(s) in {2}" -f (Get-ChildItem $DumpDir -Filter 'shot*').Count, (Get-ChildItem $DumpDir -Filter 'host*').Count, (Resolve-Path $DumpDir)) }
