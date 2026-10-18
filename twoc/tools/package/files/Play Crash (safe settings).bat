@echo off
rem Safe settings (notes 18.96): starts the game with every setting at its default, in a window,
rem ignoring crash_twoc.ini and never writing it (RECOMP_IGNORE_INI=1; the settings menu says so).
rem For when the game won't start after a settings change. Your crash_twoc.ini is left as it is.
setlocal
set RECOMP_IGNORE_INI=1
set RECOMP_FULLSCREEN=
set RECOMP_FRAMERATE=
call "%~dp0Play Crash.bat"
endlocal
