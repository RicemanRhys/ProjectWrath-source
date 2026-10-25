@echo off
rem The baked-draw test (notes 18.90): the frames between game frames are drawn from what
rem each draw's first drawing set up (on by default), which takes the render thread about a
rem third less time on the desktop. This starts the game with that off, as before, to
rem compare: the first test's e. For this run only; the log says
rem "host baked draws: off: every pass translated (RECOMP_D3D_BRIDGE_BAKE)".
setlocal
set RECOMP_D3D_BRIDGE_BAKE=0
call "%~dp0Play Project Wrath.bat"
endlocal
