@echo off
rem The DXVK test with DXVK's own HUD in the picture's top left corner: its frame rate and frame
rem times, the GPU's load, and the draw calls and queue submissions a frame. For this run only.
setlocal
set DXVK_HUD=fps,frametimes,gpuload,drawcalls,submissions,version,api
call "%~dp0Play Project Wrath (DXVK).bat"
endlocal
