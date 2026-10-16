Crash Bandicoot: The Wrath of Cortex (PC port) - Legion Go test package
=======================================================================

What this is: the PC port built on the desktop, with everything it needs, to try on the
Legion Go. The aim is to see how 144 Hz frame rates and the "render thread" (frames drawn
on a second CPU core) hold up on a handheld. The game files in game_files\ are your own
copy of the disc. There are no saves in this folder: the game starts a new game.

What's in the folder
--------------------
  Play Crash.bat                  starts the game
  Play Crash (72 fps test).bat    starts the game at 72 fps (test 3 below)
  Collect logs.bat                zips the logs and a PC summary to bring back
  crash_twoc.ini                  the game's settings, already set up for the Legion Go
  Test notes.txt                  write what you see here; it goes into the zip
  crash_twoc.exe, vcruntime140.dll, vcruntime140_1.dll    the game and its C++ runtime
  game_files\                     the disc files (default.xbe and Crashdat\)
  tools\                          vrrprobe.exe (a display test) and the log collector
  PACKAGE.txt                     which build this is

The game also makes these when it runs: logs\ (earlier runs' logs), run_twoc.err.log (the
last run's log), game_files\UDATA (saves) and game_files\TDATA, and, outside this folder,
%LOCALAPPDATA%\xboxrecomp (the Xbox's empty hard-disk images: about 5 GB on paper, but
"sparse" files that take only a few MB of real space).


A. Set up the Legion Go (once)
------------------------------
1.  Copy the whole LegionGoPackage folder to the Legion Go, for example to
    C:\Games\LegionGoPackage. Any folder works, spaces in the name too. It needs about
    1 GB. A USB stick or a USB-C drive is quickest.
2.  Set the screen to 144 Hz: Settings > System > Display > Advanced display >
    "Choose a refresh rate" > 144 Hz (Legion Space's quick settings have the same switch).
    If the list offers "Dynamic", pick 144 instead.
3.  Leave the screen at its full resolution (2560 x 1600). The game draws at 1280 x 800
    itself.
4.  Plug the charger in for the first round of tests, so the numbers don't depend on the
    battery. (A second round on battery is useful later: test 7.)
5.  Power: press the Legion L button to open Legion Space and set the thermal mode to
    Performance. Then Settings > System > Power & battery > Power mode: Best performance.
6.  In AMD Software (Adrenalin), Gaming > Graphics, make sure these are off (they change
    the frame rate behind the game's back): Radeon Chill, Frame Rate Target Control,
    AMD Fluid Motion Frames, Radeon Boost, Radeon Super Resolution, Enhanced Sync.
    If you don't have AMD Software installed, skip this step.
7.  You need an Esc key for the game's settings menu. Easiest: a USB or Bluetooth
    keyboard. Without one: Start, type "On-Screen Keyboard", open it, tap the game
    window once, then tap Esc on the on-screen keyboard. (The touch screen and the
    right controller's touchpad work as a mouse in the settings menu.)
8.  Close other games and launchers (Steam, Xbox app, Epic...), and let Windows Update
    finish first. Nothing else should be running while you test.

The Legion Go already has everything else the game needs: Windows 11 includes DirectX 11,
the shader compiler, XAudio2, XInput and Media Foundation. The Visual C++ runtime is in
the folder. (Only a Windows "N" edition lacks Media Foundation; the Legion Go doesn't ship
one.)


B. Start the game
-----------------
1.  Double-click "Play Crash.bat". A black console window opens, then the game window.
    Leave the console open; it closes by itself when the game ends.
    If Windows says "Windows protected your PC", click "More info", then "Run anyway".
2.  The game starts in a 1280 x 800 window. Alt+Enter switches to full screen and back.
3.  The controllers work as an Xbox pad. The main menu opens about 10 seconds after the
    start; start a new game (any name). Press Start during the intro to skip it.
4.  Esc opens the settings menu (the game keeps running behind it). The "Display" tab has
    everything for these tests:
      Frame rate .......... Match display / 90 fps / 144 fps / ...
      the line under it ... "Now: 144.00 fps, 2.4 frames each game frame." - this is
                            the fps line to note
      Adaptive frame rate . leave On
      Render thread ....... Auto / On / Off; the line under it says whether it is on
    Changes apply at once and are saved to crash_twoc.ini when you close the menu (Esc).
5.  To stop: close the game window (or Alt+F4).


C. The tests
------------
For each test: get to the place, stand still for 30 seconds, then play normally for a
minute or two (run, turn the camera, jump). Then open Esc > Display and note the "Now:"
line. Write down in "Test notes.txt":
  - the fps line (for example "Now: 144.00 fps" or "Now: 72.00 fps of 144.00 (fewer
    while game frames run late)")
  - whether motion looks smooth, or stutters / judders (how often: every second, now and
    then, only when loading)
  - whether the game itself slows down (Crash moves in slow motion, music stays normal)
  - fan noise and how warm the back gets (Legion Space's performance overlay shows the CPU
    temperature and power if you use it: note them)
  - plugged in or battery, and the Legion Space thermal mode
Close the game between tests: each start keeps the run before's log in logs\.

Places:
  - the hub: where a new game starts, after the intro.
  - the hub's upstairs room: walk Crash onto the round lift in the middle of the hub; it
    takes him up. It is the heaviest place in the hub.
  - Arctic Antics: the first level (the first warp room's first pad).
  - cast_bug (warp room 6, the first pad) and avalanch (warp room 4, the first pad): the
    two heaviest places the desktop measured. A new game can't reach them without
    playing through; do them only if you have a save that opens those rooms.

1.  Match display (the setting it starts with). The fps line should say 144.00 fps,
    2.4 frames each game frame. In the hub, then upstairs, then Arctic Antics.
2.  144 fps: Esc > Display > Frame rate: 144 fps. Same places. It should look the same
    as test 1 (the display is 144 Hz).
3.  72 fps: close the game and start it with "Play Crash (72 fps test).bat". The menu
    shows "72 fps" (one frame every 2 refreshes). Same places.
4.  90 fps: Esc > Display > Frame rate: 90 fps. 144 Hz can't show 90 evenly, so the line
    under it should say it falls back to 72. Note what it says.
5.  Render thread Auto against Off: back on Match display, in the heaviest place you can
    reach (upstairs), compare Render thread: Auto (it should say "on, beside the game's
    thread (this PC: 8 cores)") with Off. Note the fps line and smoothness of each.
    With Off, the line under Frame rate may show fewer fps ("of 144.00") where the
    single thread can't keep up.
6.  Full screen: Alt+Enter, then repeat test 1 in one place. Note any difference.
7.  If you have time: test 1 and test 5 again on battery (charger out), and with Legion
    Space's thermal mode on Balanced. Note the mode for each.

Then bring the logs back (part D).


D. Bring the logs back
----------------------
1.  Close the game.
2.  Double-click "Collect logs.bat". It takes about 20 seconds; the screen turns grey
    with a moving white bar twice (that's vrrprobe testing how the display shows frames).
3.  It makes LegionGo_logs_<date>_<time>.zip in this folder and opens Explorer on it.
    The zip has the last run's log, the earlier runs' logs (logs\), crash_twoc.ini,
    Test notes.txt, a summary of the PC (CPU, GPU and driver, Windows version, power
    mode, plugged in or not, the screen's modes and refresh rate) and vrrprobe's results.
4.  Copy the zip to the desktop PC (USB stick, a cloud folder or LocalSend) and give it
    to Claude.


If something goes wrong
-----------------------
- "Failed to load default.xbe": the folder was copied without game_files\, or the game
  was started some other way than "Play Crash.bat". Use the .bat.
- "Crash can't write to its save folder": copy the package somewhere you can write to
  (not C:\Program Files), or delete game_files\UDATA.
- The game window opens and closes at once: run "Collect logs.bat" and bring the zip back.
- A black screen or a crash at the start on the Legion Go only: the same; the log says
  which step failed.
- To go back to the package's settings: copy crash_twoc.ini from the desktop again (or
  delete it for the game's own defaults: 4:3, 640 x 480 lines, 60 fps).
