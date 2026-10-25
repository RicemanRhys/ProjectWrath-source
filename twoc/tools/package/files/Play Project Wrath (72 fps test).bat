@echo off
rem The 72 fps test (half of 144 Hz): this run at 72 fps whatever ProjectWrath.ini says (the
rem settings menu's Frame rate list has 72 fps too since notes 18.96).
setlocal
set RECOMP_FRAMERATE=72
call "%~dp0Play Project Wrath.bat"
endlocal
