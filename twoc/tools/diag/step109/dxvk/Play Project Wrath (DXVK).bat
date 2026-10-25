@echo off
rem Project Wrath with DXVK (step 86; renamed in step 109): the same exe as the Legion Go package, with DXVK 3.1.1's
rem d3d11.dll and dxgi.dll beside it, so the game's Direct3D 11 runs on Vulkan. It plays from the
rem package's game_files (no second copy) and keeps its own settings in this folder (ProjectWrath.ini; the
rem crash_twoc.ini from before is read until the game saves its own).
rem The package folder: the one named in "package path.txt" here, else ProjectWrath-LegionGo beside this
rem folder (or inside LegionGoPackage beside it), else C:\Games\ProjectWrath-LegionGo, else the older
rem LegionGoPackage places.
setlocal
set "HERE=%~dp0"
set "PKG="
if exist "%HERE%package path.txt" (
    for /f "usebackq delims=" %%p in ("%HERE%package path.txt") do if not defined PKG set "PKG=%%~p"
)
if not defined PKG if exist "%HERE%..\ProjectWrath-LegionGo\game_files\default.xbe" set "PKG=%HERE%..\ProjectWrath-LegionGo"
if not defined PKG if exist "%HERE%..\LegionGoPackage\ProjectWrath-LegionGo\game_files\default.xbe" set "PKG=%HERE%..\LegionGoPackage\ProjectWrath-LegionGo"
if not defined PKG if exist "C:\Games\ProjectWrath-LegionGo\game_files\default.xbe" set "PKG=C:\Games\ProjectWrath-LegionGo"
if not defined PKG if exist "%HERE%..\LegionGoPackage\game_files\default.xbe" set "PKG=%HERE%..\LegionGoPackage"
if not defined PKG if exist "C:\Games\LegionGoPackage\game_files\default.xbe" set "PKG=C:\Games\LegionGoPackage"
if not defined PKG goto nopkg
for %%p in ("%PKG%") do set "PKG=%%~fp"
if not exist "%PKG%\game_files\default.xbe" goto nopkg

set RECOMP_VBLANK=1
set RECOMP_AC97_READY=1
set RECOMP_D3D_BRIDGE=1
set RECOMP_DEV_FAST_TITLE=1
set RECOMP_PB_EXEC=
set RECOMP_WATCHDOG_SECS=
rem Restart now in the settings menu (notes 18.100): the game ends with code 75 and this starts it
rem again, at most 10 times.
set "TWOC_LAUNCHER=Play Project Wrath (DXVK).bat"
set restarts=0
rem DXVK's own logs go here (named after the exe: ProjectWrath_d3d11.log, ProjectWrath_dxgi.log); its shader
rem cache stays in %LOCALAPPDATA%\dxvk as usual (ProjectWrath.dxvk-cache, a new one for the new name).
set "DXVK_LOG_PATH=%HERE:~0,-1%"

rem The package's last run's logs into its logs\ first, as "Play Project Wrath.bat" does (the game
rem writes xbox_kernel.log into the package folder, its working directory).
:run
pushd "%PKG%"
if not exist logs mkdir logs
if exist ProjectWrath.err.log (
    for /f "usebackq delims=" %%t in (`powershell -NoProfile -Command "(Get-Item 'ProjectWrath.err.log').LastWriteTime.ToString('yyyy-MM-dd_HH-mm-ss')"`) do (
        move /y ProjectWrath.err.log "logs\run_%%t.err.log" >nul
        if exist ProjectWrath.out.log move /y ProjectWrath.out.log "logs\run_%%t.out.log" >nul
        if exist xbox_kernel.log move /y xbox_kernel.log "logs\run_%%t.kernel.log" >nul
    )
)

echo Project Wrath with DXVK (Vulkan) is running from "%PKG%".
echo Close the game window to stop it (this window closes with it).
if defined RECOMP_FRAMERATE echo Frame rate for this run: %RECOMP_FRAMERATE%
if defined DXVK_HUD echo DXVK's HUD for this run: %DXVK_HUD%
rem By its full path: cmd doesn't look in the current folder when NoDefaultCurrentDirectoryInExePath is set.
"%HERE%ProjectWrath.exe" >"%HERE%run_dxvk.out.log" 2>"%HERE%run_dxvk.err.log"
set code=%errorlevel%
>>"%HERE%run_dxvk.err.log" echo [BAT] ProjectWrath.exe (DXVK) ended with code %code% at %date% %time%

rem This run's logs into the package's logs\ too, so "Collect logs.bat" there takes them.
for /f "usebackq delims=" %%t in (`powershell -NoProfile -Command "(Get-Date).ToString('yyyy-MM-dd_HH-mm-ss')"`) do (
    copy /y "%HERE%run_dxvk.err.log" "logs\dxvk_%%t.err.log" >nul
    if exist "%HERE%run_dxvk.out.log" copy /y "%HERE%run_dxvk.out.log" "logs\dxvk_%%t.out.log" >nul
    if exist "%HERE%ProjectWrath_d3d11.log" copy /y "%HERE%ProjectWrath_d3d11.log" "logs\dxvk_%%t.d3d11.log" >nul
    if exist "%HERE%ProjectWrath_dxgi.log" copy /y "%HERE%ProjectWrath_dxgi.log" "logs\dxvk_%%t.dxgi.log" >nul
    if exist xbox_kernel.log move /y xbox_kernel.log "logs\dxvk_%%t.kernel.log" >nul
    if exist "%HERE%ProjectWrath.ini" (
        copy /y "%HERE%ProjectWrath.ini" "logs\dxvk_%%t.ini" >nul
    ) else if exist "%HERE%crash_twoc.ini" copy /y "%HERE%crash_twoc.ini" "logs\dxvk_%%t.ini" >nul
)
popd
if not "%code%"=="75" goto ended
set /a restarts+=1
if %restarts% GTR 10 (
    echo The game asked to start again 10 times from this window, so not again.
    pause
    endlocal
    exit /b
)
echo The game is starting again (Restart now in its settings menu)...
goto run

:ended
if not "%code%"=="0" (
    echo.
    echo The game ended with code %code%. Its log is run_dxvk.err.log in "%HERE%"
    echo and a copy is in the package's logs\ folder ^("Collect logs.bat" there zips it^).
    pause
)
endlocal
exit /b

:nopkg
echo The Legion Go package wasn't found. Put this folder beside ProjectWrath-LegionGo, or write the
echo package's full path (the folder holding game_files) on one line in "package path.txt" here.
pause
endlocal
exit /b 1
