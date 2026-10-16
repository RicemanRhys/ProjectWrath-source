@echo off
rem The 72 fps test (half of 144 Hz): the overlay's Frame rate list has no 72, so this run
rem sets it here. It wins over crash_twoc.ini for this run only; the overlay shows "72 fps".
setlocal
set RECOMP_FRAMERATE=72
call "%~dp0Play Crash.bat"
endlocal
