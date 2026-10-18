Crash Bandicoot: The Wrath of Cortex (PC port) - Legion Go test package
=======================================================================

What this is: the PC port built on the desktop, with everything it needs, to try on the
Legion Go. The aim is to see how 144 Hz frame rates and the "render thread" (frames drawn
on a second CPU core) hold up on a handheld. The game files in game_files\ are your own
copy of the disc. There are no saves in this folder: start a new game and name it WOMBAT,
which opens every warp room (all crystals), so every level in the tests can be reached.

New in this package:
  - The frozen splash is fixed (the last round: 3 of 12 starts with full screen on froze
    on the Universal logo until alt+tab, and stuttered after). The desktop froze the same
    way, 7 starts in 16, and now 0. Starting with full screen on, the game opens in a
    window and goes full screen once its first frames are on the screen, and full screen
    stays in Windows' normal way of showing windows (DWM's composition), which never froze.
    If the screen still stops taking frames, the game does what alt+tab did by itself
    within about a second and a half. Full screen stays off in this package's settings
    until the first test below confirms it here.
  - If the splash still freezes: alt+tab once (then back to the game).
  - The game draws at the screen's own size (native, 1920 x 1200 or 2560 x 1600, whatever
    Windows is set to), which held 60 plugged in last round.
  - Four new settings (part F): Render scale, MSAA + SMAA, Pause when in the background,
    and Skip the title animation (on in this package).
  - Two launchers for the first test: fullscreen start test, and fullscreen old way.

What's in the folder
--------------------
  Play Crash.bat                  starts the game
  Play Crash (fullscreen start test).bat     starts the game with full screen on (the
                                  first test, part C)
  Play Crash (fullscreen old way).bat        full screen at once, as the last package did,
                                  with only the new recovery to unfreeze it (the first
                                  test's comparison, optional)
  Play Crash (72 fps test).bat    starts the game at 72 fps (test 2)
  Play Crash (ack wait test).bat  an optional test: starts the game with the "ack wait"
                                  switch on (test 2's d; part C says what it is)
  Play Crash (bake off).bat       starts the game with "baked draws" off, to compare (an
                                  optional test)
  Play Crash (Vulkan probe).bat   an optional one-minute test: a second small window that
                                  tries Vulkan
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
3.  Leave the screen's resolution as it was last time (1920 x 1200 then). The game draws
    at that size ("native"); part F's Render scale draws less on battery.
4.  Plug the charger in for the first tests, so the numbers don't depend on the battery.
    (Test 2 is on battery.)
5.  Power: press the Legion L button to open Legion Space and set the thermal mode to
    Performance. Then Settings > System > Power & battery > Power mode: Best performance.
6.  In AMD Software (Adrenalin), Gaming > Graphics, make sure these are off (they change
    the frame rate behind the game's back): Radeon Chill, Frame Rate Target Control,
    AMD Fluid Motion Frames, Radeon Boost, Radeon Super Resolution, Enhanced Sync.
    If you don't have AMD Software installed, skip this step.
7.  The game's settings menu opens from the controller: hold Menu (the button with three
    lines; Start, or Options on a PlayStation pad) for about a second. Hold it again to
    close it. A short press of Menu still opens the game's own pause menu, when you let
    go of the button. Holding View (the button with two squares) and Menu together for
    about a second also opens and closes it. In the settings menu the D-pad or the left
    stick moves, A chooses, B closes a list or the menu, LB and RB change tabs. On a
    keyboard F1 opens and closes it (Esc closes it too), and the touch screen works as a
    mouse there. For the first 8 seconds after the game starts, a line at the bottom of
    the picture says so ("Press F1 to open the settings menu (or hold Start on the
    controller)"); "Show the startup message" on the Display tab turns it off.
8.  Close other games and launchers (Steam, Xbox app, Epic...), and let Windows Update
    finish first. Nothing else should be running while you test.

The Legion Go already has everything else the game needs: Windows 11 includes DirectX 11,
the shader compiler, XAudio2, XInput and Media Foundation. The Visual C++ runtime is in
the folder. (Only a Windows "N" edition lacks Media Foundation; the Legion Go doesn't ship
one.)


B. Start the game (on the Legion Go)
------------------------------------
1.  Double-click "Play Crash.bat". A black console window opens, then the game window.
    Leave the console open; it closes by itself when the game ends.
    If Windows says "Windows protected your PC", click "More info", then "Run anyway".
2.  The game starts in a window nearly as big as the screen. Alt+Enter switches to full
    screen and back.
3.  The controllers work as an Xbox pad. The main menu opens a few seconds after the
    logos; start a new game named WOMBAT. Press Start
    during the intro to skip it.
4.  The frame-rate counter in the top left corner (on in this package) shows two numbers:
      Game 60/s ........... the game's own speed: 60 is full speed; under that the game
                            itself runs slow (it says "slow" under 57)
      Shown 144/s ......... frames shown a second (2.4 for each game frame at 144 Hz)
5.  Menu held about a second (or View + Menu held, or F1) opens the settings menu; the
    game keeps running behind it.
    The "Display" tab has everything for these tests:
      Frame rate .......... Game (60) / Match display / 90 fps / 144 fps / ...
      the lines under it .. "Now: 144.00 fps, 2.4 frames each game frame." (the plan) and
                            "Measured: the game 60 updates a second (full speed); 144
                            frames shown a second." (what really happens)
      Show frame rate ..... the counter in the corner, on or off
      Adaptive frame rate . leave On
      Render thread ....... Auto / On / Off; the line under it says whether it is on
      Render scale ........ 100% (the screen's size) or less or more (part F)
      Anti-aliasing ....... Off / FXAA / SMAA / MSAA / MSAA + SMAA (part F)
    Changes apply at once and are saved to crash_twoc.ini when you close the menu.
6.  To stop: close the game window (or Alt+F4). The black console window should close with
    it within a second or two. If it stays open (still saying "Crash is running"), the
    game's process didn't finish exiting: note that in Test notes.txt, with whether
    Task Manager > Details still lists crash_twoc.exe. "Collect logs.bat" ends it.


C. The tests
------------
All on the Legion Go. Write what you see in Test notes.txt as you go.

1. The fullscreen start (the one that matters most; plugged in). Ten times: double-click
   "Play Crash (fullscreen start test).bat", wait for the main menu (about 15 seconds),
   then close the game. Each time note whether the Universal logo froze, the picture
   stuttered, or the window flashed up first (it should show for about a second, then go
   full screen). If one froze: wait 10 seconds before touching anything (the game should
   unfreeze itself), and only then alt+tab if it is still frozen; note which.
   Optional, after the ten: five times "Play Crash (fullscreen old way).bat", the same way.
   It starts as the last package did (froze 3 in 12); here a freeze should clear by itself
   within about two seconds. Note any that didn't.
   If all ten fullscreen starts were fine, turn Fullscreen on in the settings menu for the
   rest of the tests (it is saved), or keep the window: say which in Test notes.txt.

2. The battery round (cast_bug's start, standing still 30 seconds, one run of the game
   each). Unplug the charger and write down the time and the percentage. Then:
     a. Match display: start "Play Crash.bat". Go to cast_bug, stand 30 s, write it down.
        Close the game.
     b. 72 fps: start "Play Crash (72 fps test).bat" (the settings menu says "72 fps").
        Go to cast_bug, stand 30 s, write it down. Close the game.
     c. Render scale 67%: start "Play Crash.bat". Go to cast_bug. Settings menu, Display
        tab, Render scale: 67%, close the menu. Stand 30 s, write it down. Then set it back
        to 100% and close the game.
     d. Ack wait (optional): start "Play Crash (ack wait test).bat". Go to cast_bug, stand
        30 s, write it down. Close the game.
   At the end, the battery percentage. Then plug the charger back in.

   Getting to cast_bug (every run): start a new game named WOMBAT and press Start to skip
   the intro. In the hub, walk Crash onto the round lift in the middle; it takes him up to
   the bonus warp room. Go through its gate to the first pad; the level is cast_bug. When
   it has loaded, put the controller down: Crash stands at the start.

   What to write down after the 30 seconds, each time:
     - the counter's two numbers in the top left corner: "Game N/s" and "Shown N/s"
     - the Measured line: open the settings menu (hold Menu), and on the Display tab copy
       the line that starts "Measured:" as it is. Then close the menu.
     - anything you saw: stutter, Crash in slow motion

3. Standard against High (plugged in, cast_bug's start, one run of "Play Crash.bat"). Stand
   30 s with each of these, changed in the settings menu, writing down the counter, the
   Measured line and how it looks (edges, the fences and leaves):
     a. Anti-aliasing Off (as it starts)
     b. Anti-aliasing SMAA
     c. Anti-aliasing MSAA 2x + SMAA
     d. Render scale 125% with Anti-aliasing Off
   Then set Anti-aliasing back to Off and Render scale to 100%.

4. DXVK once (optional; the DXVK-test folder, its own README): cast_bug's start, Match
   display, Render thread Auto, 30 s, the counter and the Measured line.

5. A 10-minute play session, plugged in, in any levels you like: note stutter, how warm
   the back gets and fan noise, and whether the game ever slowed down.

6. The small things not tried last time:
     - View + Menu held about a second: does it open and close the settings menu?
     - Could you read the startup line at the bottom ("Press F1 ... or hold Start")?
     - Pause when in the background (part F): turn it on, then press the Windows key or
       minimise the game: does the game stop and go quiet, and go on when you come back?
     - Skip the title animation (part F): turn it off, close and start the game again:
       how long until the main menu (roughly)? Then turn it back on.

What baked draws are (the optional "bake off" launcher): at 144 Hz each game frame is shown
2 or 3 times, each time drawn again, a little further along. The first drawing works out
every draw's settings; the others reuse what it set up. Last round it took about a quarter
off the render thread here. "Play Crash (bake off).bat" turns it off for one run.

The Vulkan probe (optional, about a minute, plugged in): start "Play Crash (Vulkan
probe).bat". A second small window, "Crash TWOC - Vulkan probe", opens beside the game and
shows a colour that keeps changing. Let the game reach the hub and stay a minute, then
close the game. Write down only whether the second window opened; this time it should also
measure how evenly its frames are shown (the log, which "Collect logs.bat" takes).

What the ack wait test is: one of the game's helper threads (the one that answers the
game's graphics commands) keeps a CPU core busy all the time, waiting for the next one.
"Play Crash (ack wait test).bat" makes it rest between commands instead, which gives back
about half a core. Plugged in it made no difference last round; on battery the freed half
core might help: that is what 2d finds out. It is only a test; play with "Play Crash.bat".

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
In the settings menu (Menu held, or F1), the "Saves" tab lists the copies (date, time,
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


F. The new settings
-------------------
All in the settings menu (Menu held, or F1), saved to crash_twoc.ini.
  Render scale (Display tab) ......... draws the picture at this share of the render size
        and shows it at the render size: 25% to 200%. Under 100% the graphics chip has
        less to draw (softer, cooler, longer on battery: 67% or 75% are good there); over
        100% it draws more than is shown and scales it down (supersampling: smoother
        edges, at a cost). Applies at once.
  Anti-aliasing: MSAA 2x/4x/8x + SMAA (Display tab) ..... MSAA, then SMAA on the finished
        picture, which also smooths the edges MSAA leaves inside see-through textures
        (fences, leaves). Applies at once.
  Pause when in the background (top of the menu, off by default) .... the game waits,
        silent, while its window is minimised or another window is in front, and goes on
        when it is in front again. Not while it is starting or going full screen.
  Skip the title animation (Display tab, on in this package) ......... at each start the
        game plays its title animation (Crash and the title music) before the main menu;
        on, the animation runs many times faster, so the menu opens about 20 seconds sooner. The logos and
        the menu itself are the same. From the next start.


If something goes wrong
-----------------------
- The Universal logo freezes at the start (the picture stops, the sound goes on): wait 10
  seconds (the game should get going by itself), then alt+tab once and back to the game.
  Note it in Test notes.txt either way.
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
