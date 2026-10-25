@echo off
rem Makes a package: -Target LegionGo (the default, Desktop\Crash\LegionGoPackage\ProjectWrath-LegionGo),
rem -Target Nitro5 (...\Nitro5Package\ProjectWrath-Nitro5) or -Target Share; see make_package.ps1.
rem Arguments pass through, e.g.  make_package.bat -Force -Target Nitro5
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make_package.ps1" %*
if errorlevel 1 pause
