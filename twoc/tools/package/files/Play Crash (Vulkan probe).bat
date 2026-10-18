@echo off
rem The Vulkan probe (notes 18.89): a second, small window beside the game that only clears
rem its picture, through Vulkan instead of DirectX 11, and writes [VK] lines into the log:
rem the graphics chip, the Vulkan features a future Vulkan version of the game would use,
rem and how evenly its frames are shown. The game itself runs as with "Play Crash.bat".
rem Closing the probe's window ends the probe only. For this run only.
setlocal
set RECOMP_VULKAN=probe
call "%~dp0Play Crash.bat"
endlocal
