@echo off
rem Makes a test package: -Target LegionGo (the default, Desktop\Crash\LegionGoPackage) or
rem -Target Nitro5 (Desktop\Crash\Nitro5Package); see make_package.ps1.
rem Arguments pass through, e.g.  make_package.bat -Force -Target Nitro5
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make_package.ps1" %*
if errorlevel 1 pause
