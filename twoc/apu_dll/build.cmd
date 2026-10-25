@echo off
rem build.cmd -- rebuilds xemu_apu.dll, the game's audio library, from this folder (step 108).
rem
rem Needs: 64-bit Windows 10 or 11 and Visual Studio 2022 (Community is free) or its Build Tools, with the
rem "Desktop development with C++" workload (MSVC x64, the Windows SDK and CMake are part of it).
rem Run it by double-clicking it, or from a command prompt. It writes the new DLL here, as xemu_apu.dll,
rem and keeps the build in the folder "build". To use it: close the game, copy this xemu_apu.dll over the
rem one next to ProjectWrath.exe (keep the old one somewhere if you want to go back), start the game.
rem Keep this folder's path short (for example C:\xemu_apu-source): Visual Studio's build tools fail on
rem paths over 260 characters, and the build adds about 120 of its own.
setlocal
set "HERE=%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto novs
set "VS="
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS goto novs
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto fail
where cmake >nul 2>nul
if errorlevel 1 set "PATH=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
cmake -S "%HERE%." -B "%HERE%build" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 goto fail
cmake --build "%HERE%build" --config Release
if errorlevel 1 goto fail
copy /y "%HERE%build\Release\xemu_apu.dll" "%HERE%xemu_apu.dll" >nul
if errorlevel 1 goto fail
echo.
echo Built: "%HERE%xemu_apu.dll"
exit /b 0
:novs
echo Visual Studio 2022 with the C++ tools wasn't found (vswhere.exe). Install Visual Studio 2022 Community
echo or the Build Tools for Visual Studio 2022 with "Desktop development with C++", then run this again.
exit /b 1
:fail
echo.
echo The build failed: see the messages above. If they mention a path that couldn't be found or a
echo tracking log (FileTracker), this folder's path is probably too long for Visual Studio's tools
echo (260 characters with the build's own folders): move the folder to a short path such as
echo C:\xemu_apu-source and run build.cmd again.
exit /b 1
