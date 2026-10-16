@echo off
rem Zips the game's logs, the settings, a system summary and vrrprobe's display test
rem into one file in this folder to copy back to the desktop.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\collect_logs.ps1"
pause
