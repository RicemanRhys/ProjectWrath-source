@echo off
rem The comparison for the fullscreen start test (notes 18.93): full screen at once from the
rem start and allowed out of DWM's composition, as the step-88 package did (the frozen splash
rem 3 starts in 12), but with the new recovery, which does what alt+tab did when the screen
rem stops taking frames. For this run only.
setlocal
set RECOMP_FULLSCREEN=1
set RECOMP_FULLSCREEN_DEFER=0
set RECOMP_FULLSCREEN_COMPOSED=0
call "%~dp0Play Project Wrath.bat"
endlocal
