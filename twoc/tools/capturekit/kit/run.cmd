@echo off
rem run.cmd NAME [60|rolling] [wav] -- the issue launchers call this. Starts this kit's own copy of the game
rem (game\crash_twoc.exe, with its own crash_twoc.ini and its own copy of your save; your build folder,
rem your ini and your saves are never touched) with the capture switches on. Its logs and pictures go
rem to captures\NAME_<date>_<time>\. "60" plays this run at Frame rate: Game (60) for the comparison,
rem "wav" also records the game's sound to sound.wav in that folder. "rolling" (step 104) keeps the last
rem seconds in memory (every frame shown, small, the draws, the anomaly check) and writes them to
rem captures\rolling_<date>_<time>\ when F9 is pressed in the game window or "capture now.cmd" runs.
setlocal
set "KIT=%~dp0.."
for /f "usebackq delims=" %%t in (`powershell -NoProfile -Command "Get-Date -Format yyyy-MM-dd_HH-mm-ss"`) do set "TS=%%t"
set "OUT=%KIT%\captures\%~1_%TS%"
mkdir "%OUT%"
> "%KIT%\kit\current.txt" echo %OUT%
set "FWD=%OUT:\=/%"
set RECOMP_VBLANK=1
set RECOMP_AC97_READY=1
set RECOMP_D3D_BRIDGE=1
set RECOMP_PB_EXEC=
set RECOMP_WATCHDOG_SECS=
set "RECOMP_POWER_LOG=%OUT%\power.log"
rem What "capture now.cmd" triggers: the next swaps' draws in the log and their pictures, each
rem in-between frame of a few swaps, and the interpolation's record of a second of frames.
set "RECOMP_FB_DUMP=%OUT%\shot"
set RECOMP_D3D_BRIDGE_DUMP_ASKED=1
set "RECOMP_D3D_BRIDGE_DRAWLOG_TRIGGER=%OUT%\drawlog.trig"
set "RECOMP_D3D_BRIDGE_SUBDUMP=@%FWD%/subdump.trig"
set "RECOMP_D3D_BRIDGE_INTERP_REC=%OUT%\interp.bin"
set RECOMP_FRAMERATE=
if "%~2"=="60" set RECOMP_FRAMERATE=60
set RECOMP_D3D_BRIDGE_ROLLING=
set RECOMP_D3D_BRIDGE_ANOMALY=
set RECOMP_D3D_BRIDGE_ROLLING_TRIGGER=
set RECOMP_D3D_BRIDGE_ROLLING_FULL=
set RECOMP_D3D_BRIDGE_ROLLING_AUTO=
if "%~2"=="rolling" set "RECOMP_D3D_BRIDGE_ROLLING=%KIT%\captures"
rem step 105: the draws in full (vertices, indices, states, programs), for tools\diag\rollpix.py
if "%~2"=="rolling" set RECOMP_D3D_BRIDGE_ROLLING_FULL=1
if "%~2"=="rolling" set "RECOMP_D3D_BRIDGE_ROLLING_TRIGGER=%OUT%\rolling.trig"
if "%~2"=="rolling" set RECOMP_D3D_BRIDGE_ANOMALY=1
rem step 106: it also saves by itself (a flat patch for a frame, or the check's lines), 5 times a run at most
if "%~2"=="rolling" set RECOMP_D3D_BRIDGE_ROLLING_AUTO=5
if "%~3"=="wav" set "RECOMP_APU_OUT_WAV=%OUT%\sound.wav"
echo.
echo Crash is running (%~1). Play to the spot in README.txt, then double-click "capture now.cmd".
if defined RECOMP_FRAMERATE echo This run is at Frame rate: Game (60), for the comparison.
if defined RECOMP_APU_OUT_WAV echo The game's sound is being recorded to sound.wav.
if defined RECOMP_D3D_BRIDGE_ROLLING echo Rolling capture on: it saves by itself when it sees a glitch; you can also press F9 in the game window right AFTER one.
echo Close the game window when you're done; this window closes with it.
cd /d "%KIT%\game"
"%KIT%\game\crash_twoc.exe" >"%OUT%\run.out.log" 2>"%OUT%\run.err.log"
>>"%OUT%\run.err.log" echo [KIT] crash_twoc.exe ended with code %errorlevel% at %date% %time%
echo.
echo Done. Send the folder: %OUT%
pause
