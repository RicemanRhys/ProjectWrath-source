@echo off
rem Project Wrath (the packages, twoc/tools/package): starts the game from this folder.
rem The game finds game_files\ in the working directory and ProjectWrath.ini next to the exe,
rem so this folder can be anywhere. The switches are the ones run_twoc.ps1 -HostOnly sets on the
rem desktop (notes 18.40): the host renderer alone. The title animation plays in full unless the
rem settings menu's "Skip the title animation" (Extras, off by default) is on: ProjectWrath.ini's fast_title = 1
rem (notes 18.93; it was RECOMP_DEV_FAST_TITLE=1 here, which the menu couldn't turn off).
rem Step 107 (notes 18.110): without game_files the game opens its setup screen (the player's own disc image), so
rem this no longer stops there; the first start also writes ProjectWrath.ini when there is none. Double-clicking
rem ProjectWrath.exe does all this by itself too; this launcher keeps the console window and the restart loop.
rem Step 109 (notes 18.112): the project's name, Project Wrath: ProjectWrath.exe, ProjectWrath.ini (an ini here from
rem before the name is read until the game writes its own) and ProjectWrath.*.log.
setlocal
cd /d "%~dp0"
set RECOMP_VBLANK=1
set RECOMP_AC97_READY=1
set RECOMP_D3D_BRIDGE=1
set RECOMP_PB_EXEC=
set RECOMP_WATCHDOG_SECS=
rem The power log (notes 18.94): Windows' power mode, the plan, the CPU's speed and a temperature,
rem also written to this file line by line straight to the disk, so it survives the PC crashing.
set RECOMP_POWER_LOG=ProjectWrath.power.log
rem Restart now (notes 18.100): the settings menu's restart ends the game with code 75, and this
rem window starts it again (every "Play Project Wrath (...)" launcher comes through here); at most 10 times.
set "TWOC_LAUNCHER=Play Project Wrath.bat"
set restarts=0

:run
rem The log of the run before is kept in logs\, named by the time it ended.
if not exist logs mkdir logs
if exist ProjectWrath.err.log (
    for /f "usebackq delims=" %%t in (`powershell -NoProfile -Command "(Get-Item 'ProjectWrath.err.log').LastWriteTime.ToString('yyyy-MM-dd_HH-mm-ss')"`) do (
        move /y ProjectWrath.err.log "logs\run_%%t.err.log" >nul
        if exist ProjectWrath.out.log move /y ProjectWrath.out.log "logs\run_%%t.out.log" >nul
        if exist xbox_kernel.log move /y xbox_kernel.log "logs\run_%%t.kernel.log" >nul
        if exist ProjectWrath.power.log move /y ProjectWrath.power.log "logs\run_%%t.power.log" >nul
    )
)
rem A power log left on its own (the DXVK test's launcher moves only the others): by its own time.
if exist ProjectWrath.power.log (
    for /f "usebackq delims=" %%t in (`powershell -NoProfile -Command "(Get-Item 'ProjectWrath.power.log').LastWriteTime.ToString('yyyy-MM-dd_HH-mm-ss')"`) do (
        move /y ProjectWrath.power.log "logs\run_%%t.power.log" >nul
    )
)

echo Project Wrath is running. Close the game window to stop it (this window closes with it).
if defined RECOMP_FRAMERATE echo Frame rate for this run: %RECOMP_FRAMERATE%
if defined RECOMP_NV2A_ACK_WAIT echo Ack wait test for this run: %RECOMP_NV2A_ACK_WAIT% us
if "%RECOMP_D3D_BRIDGE_BAKE%"=="0" echo Baked draws off for this run (the comparison)
if defined RECOMP_VULKAN echo Vulkan probe for this run: a second window, [VK] lines in the log
if defined RECOMP_IGNORE_INI echo Safe settings for this run: every setting at its default, ProjectWrath.ini not read or written
rem By its full path: cmd doesn't look in the current folder when NoDefaultCurrentDirectoryInExePath is set.
"%~dp0ProjectWrath.exe" >ProjectWrath.out.log 2>ProjectWrath.err.log
set code=%errorlevel%
rem The log's last line: the exit code and when the process was gone (none: it never finished exiting).
>>ProjectWrath.err.log echo [BAT] ProjectWrath.exe ended with code %code% at %date% %time%
if not "%code%"=="75" goto ended
set /a restarts+=1
if %restarts% GTR 10 (
    echo.
    echo The game asked to start again 10 times from this window, so not again.
    echo Start it again yourself with "Play Project Wrath.bat".
    pause
    goto done
)
echo The game is starting again (Restart now in its settings menu)...
goto run

:ended
if not "%code%"=="0" (
    echo.
    echo The game ended with code %code%. Its log is ProjectWrath.err.log in this folder;
    echo "Collect logs.bat" zips it up to bring back.
    pause
)
:done
endlocal
