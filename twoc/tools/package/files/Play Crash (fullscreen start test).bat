@echo off
rem The fullscreen start test (notes 18.93): starts the game with full screen on, as
rem crash_twoc.ini's fullscreen = 1 would, for this run only. It opens in a window, goes full
rem screen once its first frames are on the screen, and stays in DWM's composition there (the
rem fix for the frozen splash); if the screen still stops taking frames, the game does what
rem alt+tab did by itself. The log says each step ("host window", "host display").
setlocal
set RECOMP_FULLSCREEN=1
call "%~dp0Play Crash.bat"
endlocal
