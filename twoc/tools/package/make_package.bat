@echo off
rem Makes the Legion Go package (Desktop\Crash\LegionGoPackage); see make_package.ps1.
rem Arguments pass through, e.g.  make_package.bat -Force
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make_package.ps1" %*
if errorlevel 1 pause
