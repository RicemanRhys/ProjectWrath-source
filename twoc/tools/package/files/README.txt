Crash Bandicoot: The Wrath of Cortex (PC port) - Legion Go test package
=======================================================================

What this is: the PC port built on the desktop, with everything it needs, to try on the
Legion Go. The aim is to see how 144 Hz frame rates and the "render thread" (frames drawn
on a second CPU core) hold up on a handheld. The game files in game_files\ are your own
copy of the disc. There are no saves in this folder: start a new game and name it WOMBAT,
which opens every warp room (all crystals), so every level in the tests can be reached.

What's in the folder
--------------------
  Play Crash.bat                  starts the game
  Play Crash (72 fps test).bat    starts the game at 72 fps (the first test's b, and test 3)
  Play Crash (ack wait test).bat  an optional test: starts the game with the "ack wait"
                                  switch on (the first test's d; part C says what it is)
  Collect logs.bat                zips the logs and a PC summary to bring back
  crash_twoc.ini                  the game's settings, already set up for the Legion Go
  Test notes.txt                  write what you see here; it goes into the zip
  crash_twoc.exe, vcruntime140.dll, vcruntime140_1.dll    the game and its C++ runtime
  game_files\                     the disc files (default.xbe and Crashdat\)
  tools\                          vrrprobe.exe (a display test) and the log collector
  PACKAGE.txt                     which build this is

The game also makes these when it runs: logs\ (earlier runs' logs), run_twoc.err.log (the
last run's log), game_files\UDATA (saves), game_files\UDATA_backups (copies of the save,
part E) and game_files\TDATA, and, outside this folder,
%LOCALAPPDATA%\xboxrecomp (the Xbox's empty hard-disk images: about 5 GB on paper, but
"sparse" files that take only a few MB of real space).


A. Set up the Legion Go (once)
------------------------------
1.  Copy the whole LegionGoPackage folder from the desktop to the Legion Go, for example
    to C:\Games\LegionGoPackage. Any folder works, spaces in the name too. It needs about
    1 GB. A USB stick or a USB-C drive is quickest. An older LegionGoPackage already on
    the Legion Go: delete it (after "Collect logs.bat" if it has runs you haven't brought
    back); this one replaces it. Steps 2-8 are on the Legion Go.
2.  Set the screen to 144 Hz: Settings > System > Display > Advanced display >
    "Choose a refresh rate" > 144 Hz (Legion Space's quick settings have the same switch).
    If the list offers "Dynamic", pick 144 instead.
3.  Leave the screen at its full resolution (2560 x 1600). The game draws at 1280 x 800
    itself.
4.  Plug the charger in for the first round of tests, so the numbers don't depend on the
    battery. (The first test's second round is on battery.)
5.  Power: press the Legion L button to open Legion Space and set the thermal mode to
    Performance. Then Settings > System > Power & battery > Power mode: Best performance.
6.  In AMD Software (Adrenalin), Gaming > Graphics, make sure these are off (they change
    the frame rate behind the game's back): Radeon Chill, Frame Rate Target Control,
    AMD Fluid Motion Frames, Radeon Boost, Radeon Super Resolution, Enhanced Sync.
    If you don't have AMD Software installed, skip this step.
7.  The game's settings menu opens from the controller: hold View (the button with two
    squares) and Menu (the button with three lines) together for about a second. Hold
    them again to close it. Press View first, or both at once: Menu then doesn't open the
    game's own pause menu too. In the settings menu the D-pad or the left stick moves, A
    chooses, B closes a list or the menu, LB and RB change tabs. Esc on a keyboard also
    opens and closes it, and the touch screen works as a mouse there.
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
    start; start a new game named WOMBAT. Press Start during the intro to skip it.
4.  The frame-rate counter in the top left corner (on in this package) shows two numbers:
      Game 60/s ........... the game's own speed: 60 is full speed; under that the game
                            itself runs slow (it says "slow" under 57)
      Shown 144/s ......... frames shown a second (2.4 for each game frame at 144 Hz)
5.  View + Menu held (or Esc) opens the settings menu; the game keeps running behind it.
    The "Display" tab has everything for these tests:
      Frame rate .......... Game (60) / Match display / 90 fps / 144 fps / ...
      the lines under it .. "Now: 144.00 fps, 2.4 frames each game frame." (the plan) and
                            "Measured: the game 60 updates a second (full speed); 144
                            frames shown a second." (what really happens)
      Show frame rate ..... the counter in the corner, on or off
      Adaptive frame rate . leave On
      Render thread ....... Auto / On / Off; the line under it says whether it is on
    Changes apply at once and are saved to crash_twoc.ini when you close the menu.
6.  To stop: close the game window (or Alt+F4). The black console window should close with
    it within a second or two. If it stays open (still saying "Crash is running"), the
    game's process didn't finish exiting: note that in Test notes.txt, with whether
    Task Manager > Details still lists crash_twoc.exe. "Collect logs.bat" ends it.


C. The tests
------------
First test (the one that matters most): cast_bug's start, standing still 30 seconds, in
four runs of the game. All of it is on the Legion Go. Round 1 plugged in, round 2 on
battery.

Getting to cast_bug (every run): start a new game named WOMBAT and press Start to skip
the intro. In the hub, walk Crash onto the round lift in the middle; it takes him up to
the bonus warp room. Go through its gate to the first pad; the level is cast_bug. When it
has loaded, put the controller down: Crash stands at the start.

What to write down in Test notes.txt after the 30 seconds, each time:
  - the counter's two numbers in the top left corner: "Game N/s" and "Shown N/s"
  - the Measured line: open the settings menu (View + Menu), and on the Display tab copy
    the line that starts "Measured:" as it is. Then close the menu.
  - anything you saw: stutter, Crash in slow motion

Round 1, plugged in (charger in, Legion Space on Performance):
  a. Match display: start "Play Crash.bat". Go to cast_bug, stand 30 s, write it down.
     Close the game.
  b. 72 fps: start "Play Crash (72 fps test).bat" (the settings menu says "72 fps"). Go to
     cast_bug, stand 30 s, write it down. Close the game.
  c. Render thread Off: start "Play Crash.bat". Go to cast_bug. Open the settings menu,
     Display tab, Render thread: Off, and close the menu. Stand 30 s, write it down.
     Then open the menu again, set Render thread back to Auto, close the menu (that
     saves it), and close the game.
  d. Ack wait (optional, see below): start "Play Crash (ack wait test).bat". Go to
     cast_bug, stand 30 s, write it down. Close the game.

Round 2, on battery: unplug the charger and write down the time you did. Then a, b, c and
d again, the same way. (The log also notes every 10 seconds whether it is plugged in and
the battery's percentage.)

What the ack wait test is: one of the game's helper threads (the one that answers the
game's graphics commands) keeps a CPU core busy all the time, waiting for the next one.
"Play Crash (ack wait test).bat" makes it rest between commands instead, which gives back
about half a core. On the desktop that made the game itself a little slower (0.3-0.6 ms
a frame). On the Legion Go the CPU and the graphics share one power budget, so the
freed half core might make the rest faster: that is what d finds out. It is only a test;
play with "Play Crash.bat".

Then, if there's time, the other tests below (also on the Legion Go).

The other tests: for each, get to the place, stand still for 30 seconds, then play
normally for a minute or two (run, turn the camera, jump). Write down in "Test notes.txt":
  - the counter's numbers (Game and Shown), or the settings menu's "Measured:" line. The
    "Now:" line above it is the plan, not a measurement.
  - Legion Space's FPS counter, if you use it, counts frames shown too (like "Shown"). At
    144 Hz each game frame is shown 2.4 times, so it divided by 2.4 is the game's own
    speed: 144 is full speed (60), 100 is 42 and 80 is 33 (Crash in slow motion).
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
  - cast_bug (the bonus warp room upstairs, the first pad) and avalanch (warp room 4, the
    first pad): the two heaviest places the desktop measured. A new game named WOMBAT
    has every warp room open.

1.  Match display (the setting it starts with). The "Now:" line should say 144.00 fps,
    2.4 frames each game frame. In the hub, then upstairs, then Arctic Antics.
2.  144 fps: settings > Display > Frame rate: 144 fps. Same places. It should look the
    same as test 1 (the display is 144 Hz).
3.  72 fps: close the game and start it with "Play Crash (72 fps test).bat". The menu
    shows "72 fps" (one frame every 2 refreshes). Same places.
4.  90 fps: settings > Display > Frame rate: 90 fps. 144 Hz can't show 90 evenly, so the
    line under it should say it falls back to 72. Note what it says.
5.  Render thread Auto against Off: back on Match display, in the heaviest place you can
    reach (upstairs), compare Render thread: Auto (it should say "on, beside the game's
    thread (this PC: 8 cores)") with Off. Note the counter and smoothness of each.
    With Off, the line under Frame rate may show fewer fps ("of 144.00") where the
    single thread can't keep up.
6.  Full screen: Alt+Enter, then repeat test 1 in one place. Note any difference.
7.  If you have time: test 1 and test 5 again on battery (charger out), and with Legion
    Space's thermal mode on Balanced. Note the mode for each.

Then bring the logs back (part D).


D. Bring the logs back
----------------------
Steps 1-3 on the Legion Go, step 4 on the desktop.
1.  Close the game. If Collect logs says Crash is still running though its window is gone,
    it notes what is left of the process and ends it (or asks first, when the log says the
    game hadn't ended).
2.  Double-click "Collect logs.bat". It takes about 20 seconds; the screen turns grey
    with a moving white bar twice (that's vrrprobe testing how the display shows frames).
3.  It makes LegionGo_logs_<date>_<time>.zip in this folder and opens Explorer on it.
    The zip has the last run's log, the earlier runs' logs (logs\), crash_twoc.ini,
    Test notes.txt, a summary of the PC (CPU, GPU and driver, Windows version, power
    mode, plugged in or not, the screen's modes and refresh rate) and vrrprobe's results.
4.  Copy the zip to the desktop PC (USB stick, a cloud folder or LocalSend) and give it
    to Claude.


E. Saves and their backups
--------------------------
The game keeps all its save slots in one file, game_files\UDATA\56550003\4D5C926D6357\
crash.sav. The PC port copies it (with SaveMeta.xbx beside it) to
game_files\UDATA_backups\<date>_<time>\ at every start and a few seconds after each save
from the hub's Load/Save console, whenever it changed. It keeps the newest 10 copies and
the newest of each day for 14 days, and deletes older ones by itself. The copying never
holds the game up.
In the settings menu (View + Menu, or Esc), the "Saves" tab lists the copies (date, time,
size, and when each was taken) with three buttons:
  Back up now ........... a copy of the save as it is now
  Restore this one ...... the chosen copy replaces the save at the next start (the game
                          has its save loaded while it runs). Close the game, start it
                          again, then load at the Load/Save console. The save in use is
                          copied first and kept in the list. "Cancel the restore" undoes it
                          until then.
  Open backups folder ... shows the copies in File Explorer
If a save ever goes wrong (a slot shows the wrong progress, or the game won't load it):
Restore a copy from before it went wrong, as above.
To take your progress to another PC, copy game_files\UDATA (and UDATA_backups if you want
the copies too) into the other folder's game_files\.


If something goes wrong
-----------------------
- At the start, a "Crash TWOC - game files" message: the game checks game_files\ against
  the disc before it starts (default.xbe's size and checksum, and Crashdat's 2,266 files;
  2,267 with default.xbe) and says what is wrong: game_files\ or default.xbe missing,
  default.xbe from another release, or Crashdat with files missing (or extra). Copy
  game_files\ from the desktop again (all of it: about 930 MB, 2,267 files), or extract
  the disc image into it, and start again. Started some other way than "Play Crash.bat",
  the game looks for game_files\ in the wrong folder: use the .bat.
- "Crash can't write to its save folder": copy the package somewhere you can write to
  (not C:\Program Files), or delete game_files\UDATA (the copies in UDATA_backups stay).
- The game window opens and closes at once: run "Collect logs.bat" and bring the zip back.
- A black screen or a crash at the start on the Legion Go only: the same; the log says
  which step failed.
- To go back to the package's settings: copy crash_twoc.ini from the desktop again (or
  delete it for the game's own defaults: 4:3, 640 x 480 lines, 60 fps).
