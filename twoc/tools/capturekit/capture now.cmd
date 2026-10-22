@echo off
rem capture now.cmd -- double-click this the moment the problem is on the screen (the game keeps running).
rem The running kit game then writes: 2 game frames' every in-between picture, 3 game frames' draws
rem in its log with their pictures (the game runs at about 1 frame a second for those 3: that's normal),
rem and a third of a second of the interpolation's record. A note with the time goes to captures.txt.
setlocal
set "KIT=%~dp0"
if not exist "%KIT%kit\current.txt" (
    echo No kit game has been started yet: start one of the numbered .cmd files first.
    pause
    exit /b 1
)
set /p OUT=<"%KIT%kit\current.txt"
>"%OUT%\interp.bin.go" echo 20
>"%OUT%\subdump.trig" echo 2
>"%OUT%\drawlog.trig" echo 3
>>"%OUT%\captures.txt" echo capture at %date% %time%
echo Captured into %OUT%
echo (Give it 10 seconds before the next capture: the pictures take a moment to write.)
timeout /t 4 >nul
