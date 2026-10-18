Crash Bandicoot: The Wrath of Cortex (PC port) - Legion Go test package
=======================================================================

What this is: the PC port built on the desktop, with everything it needs, to try on the
Legion Go. The aim is to see how 144 Hz frame rates and the "render thread" (frames drawn
on a second CPU core) hold up on a handheld. The game files in game_files\ are your own
copy of the disc. There are no saves in this folder: start a new game and name it WOMBAT,
which opens every warp room (all crystals), so every level in the tests can be reached.

*** Choose the Legion Space power mode BEFORE starting the game and don't change it while
*** it runs (my Legion Go crashed after a mid-game switch). To change it: close the game,
*** change the mode, then start the game again.

*** Keep it cool: play with the Legion Go in your hands or on a hard, flat surface (a desk
*** or table), never on a bed, sofa, cushion or blanket, and keep its vents (on the back and
*** along the top edge) clear. Don't cover it while it charges. If the back gets too hot to
*** hold comfortably, close the game and let it cool down.

New in this package:
  - The game logs what the chip and Windows' power settings do, so the next zip shows what
    happened if the Legion Go crashes again: Windows' power mode, the power plan, plugged
    in or on battery (and how many watts the battery delivers), the processor's real
    speed in MHz and as a share of its rated speed, and a temperature if Windows offers
    one. Every 10 seconds, and at once when any of them changes (with how fast the game
    was running). It goes into the log and into run_twoc.power.log, which is written
    straight to the disk line by line, so it survives even if the Legion Go crashes.
    Collect logs also takes Windows' own records of crashes and power losses from the last
    3 days (events.txt). Nothing to do: it is always on and costs nothing measurable.
  - More render sizes, grouped by the screen's shape (4:3, 16:10, 16:9, 21:9) with the
    Legion Go's (16:10) marked, any size of your own (Custom), and a line saying what the
    game really draws.
  - Draw distance is gone from the settings menu (on request).
  - Still from last time: the frozen-splash fix (if the splash ever freezes: alt+tab once),
    Render scale, MSAA + SMAA, Pause when in the background, Skip the title animation.

What's in the folder
--------------------
  Play Crash.bat                  starts the game
  Play Crash (fullscreen start test).bat     starts the game with full screen on (the
                                  first test, part C)
  Play Crash (fullscreen old way).bat        full screen at once, as the last package did,
                                  with only the new recovery to unfreeze it (the first
                                  test's comparison, optional)
  Play Crash (72 fps test).bat    starts the game at 72 fps (an optional test)
  Play Crash (ack wait test).bat  an optional test: starts the game with the "ack wait"
                                  switch on (part C says what it is)
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
last run's log), run_twoc.power.log (the last run's power log), game_files\UDATA (saves),
game_files\UDATA_backups (copies of the save,
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
    Do this before starting the game, never while it runs (see the top of this file); each
    test below says which mode to set before it.
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
      Render size ......... Monitor (native, 1920x1200) / 1280x800 / ... (part F)
      Frame rate .......... Game (60) / Match display / 90 fps / 144 fps / ...
      the lines under it .. "Now: 144.00 fps, 2.4 frames each game frame." (the plan) and
                            "Measured: the game 60 updates a second (full speed); 144
                            frames shown a second." (what really happens)
      Show frame rate ..... the counter in the corner, on or off
      Adaptive frame rate . leave On
      Render thread ....... Auto / On / Off; the line under it says whether it is on
      Render scale ........ 100% (the screen's size) or less or more (part F)
      Anisotropic filtering  Off / 2x / 4x / 8x / 16x (further down, under Textures)
      Anti-aliasing ....... Off / FXAA / SMAA / MSAA / MSAA + SMAA (part F)
    Changes apply at once and are saved to crash_twoc.ini when you close the menu.
    This package starts with: Render size "Monitor (native, ...)" (the screen's own size),
    Frame rate "Match display", Anisotropic filtering 4x, Anti-aliasing Off, Render scale
    100%, Render thread Auto, Adaptive frame rate On. The tests below say which of these
    to change, and when to put them back.
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

2. The power-mode round: where it crashed (plugged in; avalanch and the snow boss). Set
   the mode before each start, never while the game runs:
     a. With the game closed, Legion Space: Performance. Start "Play Crash.bat" and keep the
        package's settings (Render size "Monitor (native, ...)", Frame rate "Match display",
        Anisotropic filtering 4x). Play avalanch and the snow boss for about 10 minutes.
        Close the game.
     b. With the game closed, Legion Space: Balanced. Start "Play Crash.bat", open the
        settings menu, and on the Display tab set three things: Render size 1280x800 (in
        the 16:10 group), Frame rate 72 fps, and Anisotropic filtering 2x. Leave everything
        else as it is. Play the same for about 10 minutes. Close the game.
     c. Optional: Balanced again, but with the package's settings back: on the Display tab
        set Render size "Monitor (native, ...)", Frame rate "Match display" and Anisotropic
        filtering 4x (the last round's "very slow demo" was Balanced with these settings).
        5 minutes is enough. Close the game.
   For each, write down: the counter's "Game N/s" and "Shown N/s" a minute into each level,
   how warm the back got, the fan, and anything odd. If the Legion Go crashes or freezes:
   note the time (roughly), hold the power button to switch it off if it is frozen, start
   it again, and run "Collect logs.bat" before anything else. The power log was written to
   the disk as the game ran, so it shows what happened up to the crash.

3. The lighter settings on battery (cast_bug's start, then avalanch). Unplug the charger
   and write down the time and the percentage. With the game closed, set Legion Space to
   Balanced. Start "Play Crash.bat", and on the settings menu's Display tab set Render size
   1280x800, Frame rate 72 fps and Anisotropic filtering 2x (as in test 2b; they are saved,
   so if you did 2b last they are still set). Go to cast_bug and stand 30 s: write down the
   counter and the Measured line. Then play avalanch for 5 minutes. Close the game, write
   down the battery percentage and plug the charger back in.

   Getting to cast_bug: start a new game named WOMBAT and press Start to skip the intro. In
   the hub, walk Crash onto the round lift in the middle; it takes him up to the bonus warp
   room. Go through its gate to the first pad; the level is cast_bug. When it has loaded,
   put the controller down: Crash stands at the start.

   What to write down after the 30 seconds:
     - the counter's two numbers in the top left corner: "Game N/s" and "Shown N/s"
     - the Measured line: open the settings menu (hold Menu), and on the Display tab copy
       the line that starts "Measured:" as it is. Then close the menu.
     - anything you saw: stutter, Crash in slow motion

4. Anti-aliasing (plugged in, Legion Space Performance, set before starting): start "Play
   Crash.bat". On the Display tab set Render size "Monitor (native, ...)", Frame rate
   "Match display", Anti-aliasing "MSAA 2x + SMAA" and Anisotropic filtering 16x. Then
   cast_bug's start for 30 s: the counter, the Measured line and how it looks (edges,
   fences, leaves) against test 2a. Afterwards set Anti-aliasing back to Off and
   Anisotropic filtering back to 4x.

5. The small things:
     - Render size: open its list. Is 16:10 marked as the aspect ratio chosen? Is the line
       under it ("Renders ... at 16:10") readable?
     - View + Menu held about a second: does it open and close the settings menu?
     - Pause when in the background (part F): turn it on, then press the Windows key or
       minimise the game: does the game stop and go quiet, and go on when you come back?

Optional, if there is time: the DXVK-test folder once (its own README; cast_bug's start
with the package's settings: native, Match display, 4x; 30 s, the counter and the Measured
line).

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
core might help. It is only an optional test; play with "Play Crash.bat".

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
    The zip has the last run's log and power log, the earlier runs' (logs\),
    crash_twoc.ini, Test notes.txt, a summary of the PC (CPU, GPU and driver, Windows
    version, power mode, plugged in or not, the screen's modes and refresh rate), Windows'
    crash and power records of the last 3 days (events.txt) and vrrprobe's results.
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
  On battery or in Legion Space's Balanced mode, lighter settings keep the chip cooler,
  quieter and longer on battery: Render size 1280x800, Frame rate 72 fps and Anisotropic
  filtering 2x (Display tab). Plugged in, the package's own: Render size "Monitor (native,
  ...)", Frame rate "Match display", Anisotropic filtering 4x.
  Render size (Display tab) .......... the sizes are grouped by shape: 4:3, 16:10, 16:9 and
        21:9, the group of the chosen aspect ratio marked (16:10 here: 1280 x 800,
        1440 x 900, 1680 x 1050, 1920 x 1200, 2560 x 1600, 3840 x 2400); Custom takes any
        width and height from 64 to 8192 (- and + change them, or type them, then Apply).
        The line under it says what the game really draws ("Renders 1280x800 at 16:10.")
        and, for a size of another shape, that the picture fits inside it with bars.
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
- The Legion Go crashes, restarts or freezes while the game runs: note the time, start it
  again (hold the power button about 10 seconds to switch off a frozen one), and run
  "Collect logs.bat" first, before starting the game again. The power log
  (run_twoc.power.log) and Windows' own records (events.txt in the zip) show what the
  chip did up to then. Note in Test notes.txt what you were doing and which Legion Space
  mode was on.
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
