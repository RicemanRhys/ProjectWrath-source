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
  - The settings menu's tabs are now Display, Graphics, Audio, Controls, Saves, Extras,
    About (LB / RB still go round all of them). Graphics holds Anisotropic filtering,
    Anti-aliasing and Ambient occlusion (with its Strength and Half resolution), moved from
    the Display tab. Extras holds Character fur, See-through gems and crystals in the hub,
    Show the startup message, Pause the game while this menu is open (it was at the top of
    the menu), Pause when in the background and Skip the title animation, with a "Reset
    extras to defaults" button of its own. The same settings, defaults and saved values as
    before. "Reset display settings to defaults" (Display tab) now resets the Display and
    Graphics tabs only.
  - Menu scale (Display tab, the last setting): makes the settings menu's text and boxes
    smaller or bigger, 50% to 200%, at once (D-pad left / right on it). 100% is the size
    it had. Bigger than the screen allows is held back, and the line under the slider says
    so ("Limited by the window: showing 120%").
  - The startup message ("Press F1 to open the settings menu ...") is now at the top of
    the picture, centred.
  - With Skip the title animation on, the title animation's sound track (Cortex's lines
    and the animation's sounds) no longer plays on over the main menu; the title music
    plays as before. From the next start. Later cutscenes keep their voices.
  - Photo mode and Freeze (Extras tab, at the top; F3 and F2 on a keyboard): photo mode
    swaps the game's camera for a free one you fly around the scene, with the game's HUD,
    the frame-rate counter and the startup message hidden. The game keeps running behind
    it (Crash and the rest move), or Freeze holds it, silent, and you fly around the frozen
    scene. Keyboard: W A S D move, E / Space up, Q / C down, the arrows look, Left Shift
    held faster, Left Ctrl held slower, Z / X the field of view, R back to the game's
    camera; the right mouse button held turns the camera, the wheel zooms. The controller
    keeps playing the game while it runs, and flies the camera while frozen (left stick
    moves, right stick looks, RT / LT up / down, RB / LB faster / slower, D-pad up / down
    the field of view, Y the game's camera). Photo mode is never saved (off at every
    start) and turns itself off at a level load, a respawn or a cutscene's cut. The game
    only sends what is near its own view: a little more while photo mode is on, but not
    what is behind its camera, nor crates, fruit and enemies far from Crash; reflections
    still show the game's view.
  - Freecam controls (Controls tab, at the bottom): every photo mode key (two each) and
    controller button can be changed the same way as the game's, with Camera speed, Look
    sensitivity, Field of view (the game's, or 30 to 110 degrees), Invert look and a reset.
    Freeze and Photo mode can't take a key the game uses.

From the last package:
  - With Frame rate "Match display" (or any rate above 60), buildings, walls and ground no
    longer shake or shift sideways while Crash moves, and textures on them no longer pop
    in and out (late in Gold Rush and Medieval Madness most of all; the further a level
    goes from its start, the worse it was). Part of the scenery was drawn a few pixels off
    in every in-between frame and put back on the game's own frames. Crash, Coco and the
    enemies move as smoothly as before. Frame rate "Game (60)" is unchanged.

From the package before:
  - "Character fur" (now on the Extras tab) replaces the older "Crash's
    fur" and has three choices: On (the Xbox's look, the default), Off for Crash only (Crash
    without his layer of fur, as on the PlayStation 2; Coco, Crunch, Pura and the others
    keep theirs) and Off for everyone (no fur
    on anyone, as "Crash's fur" Off did). Applies at once. A crash_twoc.ini from the last
    package keeps its choice (fur off there is "Off for everyone").
  - "See-through gems and crystals in the hub" (now on the Extras tab; Off in
    this package, the game's own look): the crystal and the gems shown at each warp room
    pad are drawn as glass, the way a level draws a crystal or gem you already have (a
    coloured gem keeps its colour). The relics are glass already; the pads' outline
    icons are part of the room and stay as they are. Applies at once.
  - "Mute Droid Void's background drone" (Audio tab; Off in this package): Droid Void
    plays a droning ambience loop under its music for the whole level, even after Crash
    dies (the Xbox does the same). This silences that one sound and nothing else. It
    takes effect the next time the level starts.
  - With Frame rate "Match display" (or any rate above 60), the marks on Gold Rush's walls
    no longer flicker in and out: the in-between frames draw them a hair nearer, so they
    stay in front of their wall as in the game's own frames. Medieval Madness's flicker
    looks the same kind and should be gone too, but couldn't be checked on the desktop
    (Test notes asks). Frame rate "Game (60)" is unchanged.

Known quirks of the original game (the Xbox version does these too; not bugs of the port):
  - Droid Void's drone plays for the whole level, also after Crash dies and while the
    level restarts. "Mute Droid Void's background drone" (Audio tab) silences it.
  - In Crashteroids, Coco looks green and furry: her fur is seen through the cockpit's
    green glass. Character fur (Extras tab) "Off for everyone" removes it; "Off for Crash
    only" keeps it, since it is Coco's.

From the packages before that (also new to this folder if you skipped them):
  - Fahrenheit Frenzy's X-ray room shows its X-ray (a green negative of the room, Crash
    as a skeleton) instead of a black screen.
  - The "HUD margin" slider is gone: with HUD position "Screen edges" the counters sit at
    the screen's edges (as at 0% before). An older crash_twoc.ini's hud_margin line is
    ignored and dropped the next time the settings are saved.
  - Quit game: a button next to Resume at the bottom of the settings menu, on every tab.
    It asks "Quit the game? Unsaved progress will be lost." with Yes / No (No is first;
    B means No) and then closes the game cleanly: your settings saved, a save copy that is
    being made finished first, the sound faded out. With only the controller: D-pad down
    to it, A, then Left and A for Yes.
  - Graphics card (Display tab, at the top): Automatic, or a card by name. The Legion Go
    has one graphics chip, so leave it on Automatic; the About tab says which card draws
    and which one the screen is on.
  - Settings that only take effect at the next start (the Graphics card, Skip the title
    animation, Speakers on the Audio tab, and a save restore on the Saves tab) now ask
    "Restart the game to ...? Unsaved progress will be lost." with Restart now / Later
    (Later is first; B means Later). Restart now closes the game and starts it again from
    this window with your settings kept; Later leaves an orange line at the bottom of the
    menu, "Changes waiting for a restart: ...", with its own Restart now button.
  - The settings menu keeps the same size and place on every tab: a tab longer than the
    screen scrolls inside it (the D-pad moves through it, the mouse wheel and the touch
    screen scroll it), and the buttons at the bottom stay where they are.
  - The About tab names the processor (cores, threads) and says the game isn't slowed
    by Windows' power throttling (the game now asks Windows not to).
  - The same build as last time otherwise, with one fix from the desktop: Sea Shell's
    water no longer stops the in-between frames there (it was stuck at 60 / 60).

Still from last time:
  - Less work a frame for the processor, with the same picture (the render thread reuses
    the frame before's set-up; the game thread records about half as much), and the logs'
    measures of the graphics driver's share, the repeats and the "CPU clock down" line.
  - Collect logs' events.txt dates marked as local time, with the UTC time beside them.
  - "Reset display settings to defaults", a button at the bottom of the Display tab. It
    asks first (Yes / No; works with the controller: D-pad to the button, A, then Left for
    Yes and A, or B for No) and puts every setting on the Display and Graphics tabs back to
    the game's own default: Render size Game (480 lines), Frame rate Game (60), Render scale
    100%, Render thread Auto, the HUD in the centre, the counter off, Menu scale 100%;
    Anti-aliasing and Ambient occlusion Off, Anisotropic filtering Off. Full screen, the
    aspect ratio, the Extras, sound, controls and saves stay as they are (the Extras tab
    has its own reset).
  - 72 fps in the Frame rate list: half of 144 Hz, so each frame is held for 2
    refreshes and motion stays even. The line under the list says what it does on 144,
    180 and 240 Hz screens.
  - "Show frame rate" is Off / Simple / Detailed. Simple is the two numbers as before.
    Detailed adds the game thread's and the render thread's milliseconds a frame, the
    adaptive frame rate's step, the processor's speed (MHz and % of its rated speed), the
    battery's watts (or "Plugged in"), Windows' power mode and the temperature if Windows
    has one. This package starts on Simple.
  - "Play Crash (safe settings).bat": every setting at its default, in a window, and
    crash_twoc.ini neither read nor written (the settings menu says so at its top). If the
    game won't start after a settings change, use this.
  - An About tab in the settings menu (now the last; LB / RB): the build, the graphics chip and
    its driver, the screen's mode and refresh rate, the processor, the Windows version and
    the game files' check. THIRD_PARTY_LICENSES.txt lists the other people's code inside
    the game and beside it, with their licences.
  - And from before: the power log (run_twoc.power.log, written straight to the disk,
    and Windows' crash records in Collect logs' zip), the render sizes grouped by shape,
    the frozen-splash fix (if the splash ever freezes: alt+tab once), Render scale, MSAA +
    SMAA, Pause when in the background, Skip the title animation.

What's in the folder
--------------------
  Play Crash.bat                  starts the game
  Play Crash (fullscreen start test).bat     starts the game with full screen on (the
                                  first test, part C)
  Play Crash (fullscreen old way).bat        full screen at once, as the package before
                                  last did, with only the recovery to unfreeze it (the
                                  first test's second half)
  Play Crash (safe settings).bat  starts the game with every setting at its default, in a
                                  window, without reading or writing crash_twoc.ini (the
                                  settings menu says so while it is on). If the game won't
                                  start after a settings change, use this.
  Play Crash (72 fps test).bat    starts the game at 72 fps for one run (an optional test;
                                  72 fps is in the Frame rate list now too)
  Play Crash (ack wait test).bat  starts the game with the "ack wait" switch on (test 2b;
                                  part C says what it is)
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
  THIRD_PARTY_LICENSES.txt        the other people's code in the game and beside it, and
                                  their licences

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
    (Test 4 is on battery.)
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
    mouse there. For the first 8 seconds after the game starts, a line at the top of
    the picture says so ("Press F1 to open the settings menu (or hold Start on the
    controller)"); "Show the startup message" on the Extras tab turns it off.
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
      Show frame rate ..... the counter in the corner: Off, Simple or Detailed
      Adaptive frame rate . leave On
      Render thread ....... Auto / On / Off; the line under it says whether it is on
      Render scale ........ 100% (the screen's size) or less or more (part F)
      Menu scale .......... the settings menu's own size, 50% to 200% (the last setting)
    The "Graphics" tab (the second):
      Anisotropic filtering  Off / 2x / 4x / 8x / 16x
      Anti-aliasing ....... Off / FXAA / SMAA / MSAA / MSAA + SMAA (part F)
      Ambient occlusion ... Off / SSAO / GTAO
    The "Extras" tab (the sixth):
      Photo mode, Freeze .. the free camera and the freeze (F3, F2); see "New in this package"
      Character fur ....... On (the Xbox's fur) / Off for Crash only (Crash as on the PS2;
                            the others keep theirs) / Off for everyone
      See-through gems and crystals in the hub ... Off (the game's look) / On (glass)
      Show the startup message, Pause the game while this menu is open, Pause when in the
      background, Skip the title animation
    On the Audio tab: Mute Droid Void's background drone (from the level's next start).
    Changes apply at once and are saved to crash_twoc.ini when you close the menu.
    This package starts with: Render size "Monitor (native, ...)" (the screen's own size),
    Frame rate "Match display", Anisotropic filtering 4x (Graphics tab), Anti-aliasing Off, Render scale
    100%, Render thread Auto, Adaptive frame rate On. The tests below say which of these
    to change, and when to put them back.
6.  To stop: Quit game at the bottom of the settings menu (Yes), or close the game window
    (or Alt+F4). The black console window should close with it within a second or two. If
    it stays open (still saying "Crash is running"), the game's process didn't finish
    exiting: note that in Test notes.txt, with whether Task Manager > Details still lists
    crash_twoc.exe. "Collect logs.bat" ends it.


C. The tests
------------
All on the Legion Go. Write what you see in Test notes.txt as you go.

1. The fullscreen start (the one that matters most; plugged in, Legion Space Performance
   set before the first start). Twenty starts, each one the same way:
     - double-click the launcher, then leave the game's window in front (don't click the
       black console window or anything else while it starts),
     - wait for the main menu (about 15 seconds), close the game (the window's X, or
       Alt+F4), and wait about 5 seconds before the next start.
   a. Ten times "Play Crash (fullscreen start test).bat" (full screen after the first
      frames are shown, as players will run it).
   b. Then ten times "Play Crash (fullscreen old way).bat" (full screen at once, as the
      package before last did: it froze 3 times in 12 then; here a freeze should clear by
      itself within about two seconds).
   c. Then run "Collect logs.bat" before anything else (each start's log is kept in logs\,
      named by the time it ended), and keep the zip for part D.
   Each time note in Test notes.txt whether the Universal logo froze, the picture
   stuttered, or the window flashed up first (it should show for about a second, then go
   full screen). If one froze: wait 10 seconds before touching anything (the game should
   unfreeze itself), and only then alt+tab if it is still frozen; note which.
   Note: if you copied crash_twoc.ini from your earlier test folder into this one, plain
   "Play Crash.bat" may start full screen too (that copy has fullscreen = 1 saved); this
   package's own ini starts in a window. Either is fine for the tests below; say which.

2. Ten minutes at cast_bug's start, twice (plugged in, Legion Space Performance and
   Windows' Best performance set before starting, the package's settings).
   a. Start cool: leave the Legion Go about 10 minutes with the game closed first. Start
      "Play Crash.bat", go to cast_bug (below), put the controller down and let Crash
      stand for 10 minutes. Write down the counter's two numbers after 1, 5 and 10
      minutes, then close the game.
   b. Let it cool about 10 minutes again, then the same 10 minutes with
      "Play Crash (ack wait test).bat".
   Then "Collect logs.bat". The power log shows where the processor's speed settles (a
   "CPU clock down" line now marks a drop of more than a tenth that lasts 10 seconds).

   The tests from here on are optional this round (they were the last package's list):
   do them only if there is time, and never back to back without letting it cool.

3. The power-mode round: where it crashed (plugged in; avalanch and the snow boss). Set
   the mode before each start, never while the game runs:
     a. With the game closed, Legion Space: Performance. Start "Play Crash.bat" and keep the
        package's settings (Render size "Monitor (native, ...)", Frame rate "Match display",
        Anisotropic filtering 4x). Play avalanch and the snow boss for about 10 minutes.
        Close the game.
     b. With the game closed, Legion Space: Balanced. Start "Play Crash.bat", open the
        settings menu, and set three things: on the Display tab Render size 1280x800 (in
        the 16:10 group) and Frame rate 72 fps, on the Graphics tab Anisotropic filtering
        2x. Leave everything else as it is. Play the same for about 10 minutes. Close the game.
     c. Optional: Balanced again, but with the package's settings back: Render size "Monitor
        (native, ...)" and Frame rate "Match display" (Display tab), Anisotropic filtering
        4x (Graphics tab) (the last round's "very slow demo" was Balanced with these settings).
        5 minutes is enough. Close the game.
   For each, write down: the counter's "Game N/s" and "Shown N/s" a minute into each level,
   how warm the back got, the fan, and anything odd. If the Legion Go crashes or freezes:
   note the time (roughly), hold the power button to switch it off if it is frozen, start
   it again, and run "Collect logs.bat" before anything else. The power log was written to
   the disk as the game ran, so it shows what happened up to the crash.

4. The lighter settings on battery (cast_bug's start, then avalanch). Unplug the charger
   and write down the time and the percentage. With the game closed, set Legion Space to
   Balanced. Start "Play Crash.bat", and in the settings menu set Render size 1280x800 and
   Frame rate 72 fps (Display tab), Anisotropic filtering 2x (Graphics tab) (as in test 3b; they are saved,
   so if you did 3b last they are still set). Go to cast_bug and stand 30 s: write down the
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

5. Anti-aliasing (plugged in, Legion Space Performance, set before starting): start "Play
   Crash.bat". On the Display tab set Render size "Monitor (native, ...)" and Frame rate
   "Match display", on the Graphics tab Anti-aliasing "MSAA 2x + SMAA" and Anisotropic
   filtering 16x. Then
   cast_bug's start for 30 s: the counter, the Measured line and how it looks (edges,
   fences, leaves) against test 3a. Afterwards set Anti-aliasing back to Off and
   Anisotropic filtering back to 4x.

6. The small things:
     - Render size: open its list. Is 16:10 marked as the aspect ratio chosen? Is the line
       under it ("Renders ... at 16:10") readable?
     - Show frame rate: Detailed. Are the extra lines readable (the threads' ms, Adaptive,
       CPU MHz, Battery W or Plugged in, Power mode, a temperature)? Note one set of them on
       battery. Then back to Simple.
     - Frame rate 72 fps from the list: does the line under it say "Even: 72.00 fps, each
       frame held 2 refreshes"?
     - Reset display settings to defaults (bottom of the Display tab), with the controller:
       does No (or B) leave everything as it was, and Yes put the Display and Graphics tabs
       back to the defaults (a small window, Frame rate Game (60)) while the Extras tab keeps
       its settings? Afterwards set the package's settings again (the Aspect ratio stays
       16:10): Render size "Monitor (native, ...)", Frame rate "Match display", Render
       thread Auto, Show frame rate Simple, HUD position Screen edges (Display tab),
       Anisotropic filtering 4x (Graphics tab). (Or close the game and copy crash_twoc.ini
       from the desktop again.)
     - The About tab: does it name the Legion Go's graphics chip (AMD Radeon) and driver,
       1920x1200 (or your resolution) at 144 Hz, and Windows 11? Copy its lines into Test
       notes.txt.
     - "Play Crash (safe settings).bat" once: does it start in a small window with the
       orange "Safe settings" line at the top of the settings menu? Close it; then
       "Play Crash.bat" should start with your settings as before.
     - View + Menu held about a second: does it open and close the settings menu?
     - Quit game with the controller only (D-pad down to it, A; No first, then again and
       Yes): does the game close, and the black window with it?
     - Switch tabs with LB / RB: does the menu stay the same size and in the same place?
       On the Controls tab, does the D-pad scroll down the long list and back up?
     - Skip the title animation off (Extras tab): does it ask "Restart the game to play
       the title animation again?" Choose Later: is there an orange "Changes waiting for a
       restart" line at the bottom? Press its Restart now: does the game come back (the
       title animation plays at its own speed now)? Then switch it on again and Restart now.
     - The About tab: does it name the processor (AMD Ryzen Z1 Extreme, 8 cores, 16
       threads) and say "Power throttling: not throttled"? Graphics card: AMD Radeon,
       "Chosen by: Automatic (the only graphics card)"?
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
about half a core. Plugged in it made no difference to the frame rate before; with the
power limit cutting the processor's speed a few minutes in, the freed half core might keep
it faster. Test 2b compares it; otherwise play with "Play Crash.bat".

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
                          has its save loaded while it runs). It asks "Restart the game to
                          restore ...?": Restart now starts the game again (or Later, and
                          close it yourself), then load at the Load/Save console. The save
                          in use is copied first and kept in the list. "Cancel the restore"
                          undoes it until then.
  Open backups folder ... shows the copies in File Explorer
If a save ever goes wrong (a slot shows the wrong progress, or the game won't load it):
Restore a copy from before it went wrong, as above.
To take your progress to another PC, copy game_files\UDATA (and UDATA_backups if you want
the copies too) into the other folder's game_files\.


F. The new settings
-------------------
All in the settings menu (Menu held, or F1), saved to crash_twoc.ini.
  On battery or in Legion Space's Balanced mode, lighter settings keep the chip cooler,
  quieter and longer on battery: Render size 1280x800, Frame rate 72 fps (Display tab) and
  Anisotropic filtering 2x (Graphics tab). Plugged in, the package's own: Render size
  "Monitor (native, ...)", Frame rate "Match display", Anisotropic filtering 4x.
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
  Anti-aliasing: MSAA 2x/4x/8x + SMAA (Graphics tab) .... MSAA, then SMAA on the finished
        picture, which also smooths the edges MSAA leaves inside see-through textures
        (fences, leaves). Applies at once.
  Pause when in the background (Extras tab, off by default) ......... the game waits,
        silent, while its window is minimised or another window is in front, and goes on
        when it is in front again. Not while it is starting or going full screen.
  Skip the title animation (Extras tab, on in this package) .......... at each start the
        game plays its title animation (Crash and the title music) before the main menu;
        on, the animation runs many times faster, so the menu opens about 20 seconds sooner,
        and its sound track (Cortex's lines) is left out; the logos, the title music and
        the menu itself are the same. From the next start (it asks whether to restart now).
  Menu scale (Display tab) ................ the settings menu's size, 50% to 200%.
  Photo mode, Freeze (Extras tab) ......... a free camera, the game held; never saved.
  Freecam controls (Controls tab) ......... photo mode's keys, buttons and sliders.
  Graphics card (Display tab, Automatic in this package) ..... which graphics chip draws the
        game; on a PC with two (a laptop's Intel and NVIDIA) it can name one. From the next
        start: it asks "Restart the game to use ...?" (Restart now / Later).
  Quit game (bottom of the menu, every tab) ......... asks, then closes the game cleanly.


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
- If the game won't start after a settings change, use "Play Crash (safe settings).bat": every
  setting at its default, in a window, and crash_twoc.ini neither read nor changed. Then put
  the setting back in crash_twoc.ini (or delete the file) and start "Play Crash.bat" again.
- The game window opens and closes at once: run "Collect logs.bat" and bring the zip back.
- A black screen or a crash at the start on the Legion Go only: the same; the log says
  which step failed.
- To go back to the package's settings: copy crash_twoc.ini from the desktop again (or
  delete it for the game's own defaults: 4:3, 640 x 480 lines, 60 fps).
