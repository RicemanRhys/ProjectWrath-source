@echo off
rem Crash TWOC (Legion Go package, twoc/tools/package): starts the game from this folder.
rem The game finds game_files\ in the working directory and crash_twoc.ini next to the exe,
rem so this folder can be anywhere. The switches are the ones run_twoc.ps1 -HostOnly -FastTitle
rem sets on the desktop (notes 18.40): the host renderer alone, the title animation sped up.
setlocal
cd /d "%~dp0"
if not exist "game_files\default.xbe" (
    echo game_files\default.xbe is missing from "%~dp0".
    echo Copy the whole LegionGoPackage folder, game_files included.
    pause
    exit /b 1
)
set RECOMP_VBLANK=1
set RECOMP_AC97_READY=1
set RECOMP_D3D_BRIDGE=1
set RECOMP_DEV_FAST_TITLE=1
set RECOMP_PB_EXEC=
set RECOMP_WATCHDOG_SECS=

rem The log of the run before is kept in logs\, named by the time it ended.
if not exist logs mkdir logs
if exist run_twoc.err.log (
    for /f "usebackq delims=" %%t in (`powershell -NoProfile -Command "(Get-Item 'run_twoc.err.log').LastWriteTime.ToString('yyyy-MM-dd_HH-mm-ss')"`) do (
        move /y run_twoc.err.log "logs\run_%%t.err.log" >nul
        if exist run_twoc.out.log move /y run_twoc.out.log "logs\run_%%t.out.log" >nul
        if exist xbox_kernel.log move /y xbox_kernel.log "logs\run_%%t.kernel.log" >nul
    )
)

echo Crash is running. Close the game window to stop it (this window closes with it).
if defined RECOMP_FRAMERATE echo Frame rate for this run: %RECOMP_FRAMERATE%
rem By its full path: cmd doesn't look in the current folder when NoDefaultCurrentDirectoryInExePath is set.
"%~dp0crash_twoc.exe" >run_twoc.out.log 2>run_twoc.err.log
set code=%errorlevel%
rem The log's last line: the exit code and when the process was gone (none: it never finished exiting).
>>run_twoc.err.log echo [BAT] crash_twoc.exe ended with code %code% at %date% %time%
if not "%code%"=="0" (
    echo.
    echo The game ended with code %code%. Its log is run_twoc.err.log in this folder;
    echo "Collect logs.bat" zips it up to bring back.
    pause
)
endlocal
