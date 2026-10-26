@echo off
cd /d "%~dp0"
rem Visual Studio 2022's x64 compiler in any edition or folder, found by vswhere. No quotes in the for /f command
rem (cmd /c strips them when the path has parentheses); .\ because of NoDefaultCurrentDirectoryInExePath
set "VSDIR="
pushd "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
for /f "usebackq delims=" %%i in (`.\vswhere.exe -latest -version [17.0^,18.0^) -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
popd
if not defined VSDIR (echo Visual Studio 2022 with the C++ x64 tools not found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /EHsc /O2 ddtimes.cpp /Fe:ddtimes.exe
