Project Wrath - Acer Nitro 5 test package
=========================================

What this is: the PC port built on the desktop, with everything it needs, to try on the
Acer Nitro 5 laptop (RTX 3050 Laptop GPU with 4 GB, Intel Core i5-12500H, 1920 x 1080 at
144 Hz). It is the first time the game runs on this laptop, so the tests are short: does it
use the NVIDIA GPU, how fast does it run, and does full screen start cleanly. The game files
in game_files\ are your own copy of the disc. There are no saves in this folder: start a new
game and name it WOMBAT, which opens every warp room, so every level in the tests can be
reached.

New in this build: your saves, their backups and your settings now live in
%LOCALAPPDATA%\ProjectWrath (saves\ and ProjectWrath.ini), not in this folder, so a new package never
loses them; a save in this folder's game_files\UDATA is copied there at the first start (the old
copy stays), and the game says so at the top of the screen. This package's ProjectWrath.ini is read
only while %LOCALAPPDATA%\ProjectWrath has none (delete that one to go back to the package's). The
Saves tab shows the folder ("Open saves folder"); portable.txt next to ProjectWrath.exe keeps
everything here as before.
From the build before: the project has its name, Project Wrath. The game is ProjectWrath.exe (Task
Manager shows "Project Wrath"), its window and settings menu say Project Wrath, the launchers are
"Play Project Wrath.bat" and its variants, the settings ProjectWrath.ini, the logs
ProjectWrath.err.log, and "Collect logs.bat" makes ProjectWrath_logs_Nitro5_<date>_<time>.zip. The
package is the ProjectWrath-Nitro5 folder. Nothing in the game itself changes. Windows keeps its
Graphics setting (part A step 6) per program file, so set it again for ProjectWrath.exe even if you
set it for the earlier package's exe. A crash_twoc.ini copied in from the earlier test folder is still
read (until the game saves its own ProjectWrath.ini). The About tab has "Copy build info" (the build,
Windows, the processor, the graphics card and driver, the display, the Frame rate setting and the changed
settings on the clipboard, with no user name, PC name or folders: paste it into Test notes.txt once,
please) and "Open logs folder".
From the last build: a first-run setup (the start of sharing the game with other people): when
game_files\ is missing or fails its check, a setup screen opens before the game and copies
the game files from a disc image (.iso) or an extracted folder, checking every file (this
package has its game files, so you shouldn't see it). The About tab has a Game files part:
"Verify game files" (all 2,267 files against the disc, about 10 seconds, the game running)
and "Choose another disc image..." (restarts into the setup screen). ProjectWrath.exe can be
double-clicked on its own now; "Play Project Wrath.bat" stays the way to start it for the tests.
From the builds before: Smokey and the Bandicoot's flat grey ("silver") triangle that flashed over
the road for a frame while driving is gone (a piece of ground beside the camera drawn at the
picture's centre for one frame; both Frame rate settings; nothing else should look different).
From the build before: Smokey and the Bandicoot's dark slate triangle on the far canyon wall
(near the finish gate) is gone: the light haze along the foot of the canyon walls now always
draws over its wall (at both Frame rate settings; nothing else should look different).
Earlier: the settings menu's tabs are Display, Graphics, Audio, Controls, Saves,
Extras, About. Graphics holds Anisotropic filtering, Anti-aliasing and Ambient occlusion
(moved from Display); Extras holds Character fur, See-through gems and crystals in the hub,
Show the startup message, Pause the game while this menu is open, Pause when in the
background and Skip the title animation, with its own reset; "Reset display settings"
covers the Display and Graphics tabs. Menu scale (Display tab, last): the menu's size, 50%
to 200%. The startup message is at the top of the picture. Skip the title animation also
leaves out the title animation's sound track (Cortex's lines) instead of letting it play on
over the main menu. Photo mode and Freeze (Extras tab; F3 and F2): a free camera with the
HUD hidden, flown with the keyboard (W A S D, E / Q, the arrows, the right mouse button held
to turn) while the game runs, or around the frozen scene (the controller flies it then);
never saved, off at every start; its keys under Controls, "Freecam controls".

Known quirks of the original game (the Xbox does these too): Droid Void's drone plays for the
whole level, also after Crash dies (the Audio tab's option silences it); in Crashteroids
Coco looks green and furry, her fur seen through the cockpit's green glass (Character fur,
Extras tab: "Off for everyone" removes it, "Off for Crash only" keeps it).

Each step says which computer it is on: [desktop] or [laptop].

What's in the folder
--------------------
  Play Project Wrath.bat                         starts the game
  Play Project Wrath (fullscreen start test).bat starts it with full screen on (test 5)
  Play Project Wrath (fullscreen old way).bat    full screen at once, the older way (not needed now)
  Play Project Wrath (72 fps test).bat           one run at 72 fps (72 fps is in the menu's list too)
  Play Project Wrath (safe settings).bat         every setting at its default, ProjectWrath.ini not
                                         read or written: if the game won't start after a
                                         settings change
  Play Project Wrath (ack wait test).bat         one run with the "ack wait" switch (not needed now)
  Play Project Wrath (bake off).bat              one run with "baked draws" off (not needed now)
  Collect logs.bat                       zips the logs and a PC summary to bring back
  ProjectWrath.ini                         the game's settings, set up for this laptop
  Test notes.txt                         write what you see here; it goes into the zip
  ProjectWrath.exe                         the game (its C++ runtime built in)
  xemu_apu.dll                           the game's audio library (needed beside the exe)
  game_files\                            the disc files
  tools\, PACKAGE.txt                    the log collector and a display test; which build
                                         this is
  THIRD_PARTY_LICENSES.txt               other people's code in the game, and its licences
  LICENSE.txt                            this project's own code (MIT) and what that covers
When it runs the game also makes logs\ and ProjectWrath.*.log in this folder, and
%LOCALAPPDATA%\ProjectWrath with saves\ (the saves and their copies), ProjectWrath.ini (your
settings) and cache\ (the Xbox's hard-disk stand-in).


A. Set up the laptop (once)
---------------------------
1.  [desktop] Copy the ProjectWrath-Nitro5 folder (in Desktop\Crash\Nitro5Package) to a USB stick
    (about 1 GB).
2.  [laptop] Copy it from the stick to the laptop, for example to C:\Games\ProjectWrath-Nitro5.
    Any folder works, spaces in the name too (not C:\Program Files).
3.  [laptop] Plug the charger in. The first tests are plugged in.
4.  [laptop] Windows' power mode (Settings > System > Power & battery > Power mode):
    Balanced is enough. Measured on this laptop: plugged in, Balanced and NitroSense
    Default, the game held full speed (Game 60, Shown 144) for 10 minutes with about a
    quarter of each game frame to spare, the processor levelling off at 86-90 C. Best
    performance isn't needed.
5.  [laptop] NitroSense (Acer's app, the "N" key or the Start menu): Default is enough.
    Performance (or Turbo) runs the fan faster: cooler, louder (measured only when the
    laptop was idle). Close NitroSense's window before playing; keep its mode. In one
    session it took most of a processor thread in the background.
    If the laptop is already at 85 C or more before the game starts (NitroSense shows it),
    restart Windows first. Measured once: 85-90 C idle before a restart, 67-72 C after;
    that hot session ran the game at 100 C and about 12% slower.
6.  [laptop] Make sure the game uses the NVIDIA GPU, not the Intel one:
      a. Settings > System > Display > Graphics.
      b. "Add desktop app" (or "Browse"), go to the ProjectWrath-Nitro5 folder and pick
         ProjectWrath.exe. (Windows keeps this setting per program file: an entry for the
         earlier package's crash_twoc.exe doesn't count for ProjectWrath.exe; you may remove
         that old entry.)
      c. Click ProjectWrath.exe in the list, then Options (or the drop-down), choose
         "High performance" (it names the NVIDIA GeForce RTX 3050), and Save.
      d. If the NVIDIA Control Panel is installed (right-click the desktop, or the Start
         menu): Manage 3D settings > Program Settings > Add > ProjectWrath.exe, and
         "Preferred graphics processor": High-performance NVIDIA processor > Apply.
         If it isn't installed, step c is enough.
    The game also asks for the NVIDIA GPU by itself, and its settings menu can name the
    card outright (Graphics card, part B step 5), but Windows' setting comes first.
7.  [laptop] Close other games and launchers (Steam, Xbox app, Epic...) and let Windows
    Update finish first.

About the processor: the i5-12500H has 4 fast cores (P-cores, 2 threads each) and 8
efficiency cores (E-cores). Windows 11 knows this mix and puts a game's busy threads on the
fast cores; nothing to set. The game also tells Windows not to slow it down as a background
program would (its log and the About tab say so: "Power throttling: not throttled").

The laptop already has everything else the game needs: Windows 11 includes DirectX 11, the
shader compiler, XAudio2, XInput and Media Foundation; the Visual C++ runtime is built into
the game.


B. Start the game [laptop]
--------------------------
1.  Double-click "Play Project Wrath.bat". A black console window opens, then the game window.
    Leave the console open; it closes by itself when the game ends.
    If Windows says "Windows protected your PC", click "More info", then "Run anyway".
2.  The game opens in a window as big as the screen. Alt+Enter switches to full screen and
    back.
3.  Press F1: the settings menu opens. Click the "About" tab (the last). Check:
      Graphics card ...... must say NVIDIA GeForce RTX 3050 Laptop GPU (not Intel UHD
                           or Intel Iris Xe). If it says Intel: close the game, do part A
                           step 6 again, start again.
      Display's card ..... most likely Intel Iris Xe "(Windows copies each frame)": the
                           screen is wired to the Intel GPU, and Windows copies each frame
                           from the NVIDIA card to it. That is normal on this laptop.
      Processor .......... the i5-12500H, "hybrid: 4 performance cores ..., 8 efficiency
                           cores ...".
      Power throttling ... "not throttled".
    Write the lines into Test notes.txt (or a photo of the screen). Press F1 to close.
4.  A controller works as an Xbox pad (hold Start about a second for the settings menu); the
    keyboard plays too (W A S D or the arrows move, Space jumps, J spins, Enter pauses).
    Start a new game named WOMBAT; press Start (or Enter) during the intro to skip it.
5.  The settings menu (F1):
      - Quit game (next to Resume, at the bottom, on every tab) closes the game after
        asking "Quit the game? Unsaved progress will be lost." (No is first; Esc or B
        means No).
      - Display tab, Graphics card: Automatic (Windows' choice, the NVIDIA GPU after part A)
        or a card by name. A change applies at the next start, so it asks "Restart the game
        to use ...?": Restart now starts it again with the new card (your settings are
        kept); Later leaves an orange note at the bottom, "Changes waiting for a restart",
        with its own Restart now button. Skip the title animation (Extras tab) and Speakers
        (Audio tab) ask the same way.
      - Extras tab, Character fur: On (the Xbox's fur), Off for Crash only (his body, head
        and tail as on the PlayStation 2; the others keep theirs) or Off for everyone.
        See-through gems and crystals in the hub: the pads' crystal and gems as glass.
        Both apply at once. Audio tab, Mute Droid Void's background drone: from the
        level's next start.
      - Show frame rate is Detailed in this package: the corner shows the game's speed
        ("Game 60/s" is full speed), the frames shown ("Shown 144/s" at 144 Hz), the two
        threads' milliseconds, the processor's speed and "Plugged in" or the battery's
        watts.
6.  To stop: Quit game in the menu, or close the window (its X, or Alt+F4).


C. The tests [laptop]
---------------------
Write what you see in Test notes.txt as you go. Plugged in, Balanced and NitroSense Default,
unless a test says otherwise. Let the laptop cool a few minutes between tests.

1. cast_bug's start, standing 30 seconds. Start "Play Project Wrath.bat", new game WOMBAT, skip the
   intro. In the hub, walk Crash onto the round lift in the middle; it takes him up to the
   bonus warp room. Go through its gate to the first pad; the level is cast_bug. When it has
   loaded, let go of everything: Crash stands at the start. After 30 seconds write down the
   corner's lines (all of them: Game, Shown, the threads' ms, Adaptive, the CPU line).
2. The hub: back in the hub (pause, WARP ROOM, YES), walk around 30 seconds, write down the
   corner's Game and Shown numbers, and anything that looks wrong.
3. Match display against 72 fps: at cast_bug's start again, F1, Display tab, Frame rate
   "72 fps" (the line under it should say it is even: each frame held 2 refreshes), close
   the menu, 30 s, write down the corner's lines. Then Frame rate back to "Match display".
4. Plugged in against battery: unplug the charger, wait a minute, cast_bug's start for 30 s,
   the corner's lines (it shows the battery's watts now). Plug the charger back in.
5. Ten full-screen starts: ten times "Play Project Wrath (fullscreen start test).bat", each time
   leaving the game's window in front, waiting for the main menu (about 15 seconds), then
   Quit game (or Alt+F4), and about 5 seconds before the next. For each, note: fine / the
   Universal logo froze (did it start again by itself?) / stuttered / anything else.
6. Then "Collect logs.bat" (part D).

Optional, if there is time: Restart now once (F1, Display, Graphics card: pick the NVIDIA
card by name, Restart now; the game comes back; About says the same card; then Automatic
again, Restart now). "Play Project Wrath (safe settings).bat" once: it starts in a small window with
an orange "Safe settings" line at the top of the menu, and asks no restart questions.


D. Bring the logs back
----------------------
1.  [laptop] Close the game.
2.  [laptop] Double-click "Collect logs.bat". It takes about 20 seconds; the screen turns
    grey with a moving white bar twice (a display test).
3.  [laptop] It makes ProjectWrath_logs_Nitro5_<date>_<time>.zip in this folder and opens Explorer on it.
4.  [laptop -> desktop] Copy the zip to the desktop (USB stick, a cloud folder or
    LocalSend) and give it to Claude.


E. The 4 GB of graphics memory
------------------------------
The RTX 3050 Laptop GPU has 4 GB of its own memory; Windows and the desktop keep a few
hundred MB of it.
What the game itself takes was measured on the desktop
(Windows' "dedicated GPU memory" for the game, standing at cast_bug's start, a busy level),
not on the laptop:
  1920 x 1080, this package's settings ............................  about 90 MB
  1920 x 1080 with MSAA 4x / MSAA 8x ................................  about 165 / 230 MB
  1920 x 1080 with Ambient occlusion GTAO (full / half resolution) ..  about 140 / 120 MB
  3840 x 2160 (4K), no anti-aliasing ................................  about 280 MB
  4K with MSAA 8x and GTAO ..........................................  about 920 MB
  1080p, MSAA 8x + SMAA, GTAO, Render scale 200% (4K drawn) .........  about 970 MB
So even the heaviest of these fits in 4 GB with room to spare. On the laptop the limit is
more likely the GPU's speed than its memory: 4K, MSAA 8x and full-size ambient occlusion
cost it a lot of drawing. The safe start at 1080p is this package's settings; to try more,
one at a time, watching the counter's "Game 60/s" (all on the Graphics tab): Anti-aliasing
SMAA or MSAA 2x, Ambient occlusion GTAO with Half resolution, Anisotropic filtering 8x or
16x. If the counter drops under 60, put the
last change back. (Other levels weren't measured; the drivers differ too, so treat these
as a guide.)


F. If something goes wrong
--------------------------
- The About tab says Intel: part A step 6, then start again. Or F1, Display, Graphics card:
  pick the NVIDIA card, Restart now.
- The game is in slow motion or far too fast: write it down with the corner's numbers and
  run "Collect logs.bat".
- The Universal logo freezes at the start: wait 10 seconds (it should get going by itself),
  then alt+tab once and back. Note it either way.
- The game won't start after a settings change: "Play Project Wrath (safe settings).bat" (every
  setting at its default, in a window, ProjectWrath.ini not read or changed).
- The setup screen at the start ("The game files need attention: ..."): game_files\ is
  missing files or has wrong ones; copy the whole folder from the desktop again.
- "Your saves stay in the game's folder this run" at the top of the screen: %LOCALAPPDATA%\
  ProjectWrath couldn't be used (the message says why); note it in Test notes.txt.
- The game window opens and closes at once, or the laptop crashes: run "Collect logs.bat"
  and bring the zip back.
