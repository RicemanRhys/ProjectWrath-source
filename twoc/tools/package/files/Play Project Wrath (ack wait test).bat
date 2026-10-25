@echo off
rem The ack wait test (notes 18.87): the thread that answers the game's graphics commands
rem waits between them instead of spinning, which gives back about half a CPU core. On the
rem desktop it made the game's own thread 0.3-0.6 ms slower a frame; on the handheld the
rem freed core may let the rest run faster. For this run only; the log says
rem "NV2A ack thread: spins 2000 us after work, then waits 100 us a pass".
setlocal
set RECOMP_NV2A_ACK_WAIT=100
call "%~dp0Play Project Wrath.bat"
endlocal
