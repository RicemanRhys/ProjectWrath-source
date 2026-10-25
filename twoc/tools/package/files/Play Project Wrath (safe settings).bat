@echo off
rem Safe settings (notes 18.96): starts the game with every setting at its default, in a window,
rem ignoring ProjectWrath.ini and never writing it (RECOMP_IGNORE_INI=1; the settings menu says so).
rem For when the game won't start after a settings change. Your ProjectWrath.ini is left as it is.
setlocal
set RECOMP_IGNORE_INI=1
set RECOMP_FULLSCREEN=
set RECOMP_FRAMERATE=
rem The graphics card: Automatic (notes 18.100), whatever ProjectWrath.ini or a variable named.
set RECOMP_GPU=
call "%~dp0Play Project Wrath.bat"
endlocal
