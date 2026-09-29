@echo off
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c++17 /EHsc /O2 wgctimes.cpp /Fe:wgctimes.exe /link windowsapp.lib d3d11.lib dxgi.lib user32.lib
