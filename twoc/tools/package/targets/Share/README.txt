Project Wrath - an unofficial fan port
======================================

What this is
------------
Project Wrath is an unofficial fan port. It is not affiliated with, endorsed by or sponsored by the
owners of the game. You need your own copy of the game.

It is a PC version of an original Xbox game from 2001: the game's own Xbox program was translated into
a Windows program ("static recompilation"), so it is the same game, running natively on Windows, with
extras such as widescreen, high frame rates and modern controllers. All trademarks belong to their
owners.

NO GAME FILES ARE INCLUDED. This folder holds only the program. To play you need your own copy of the
game: an image of the original Xbox disc (USA release) or the files extracted from one (see "What you
need"). The first time you start it, Project Wrath copies what it needs from your image into this folder
and checks every file. Nothing is downloaded, and nothing is sent anywhere.


What you need
-------------
- A Windows 10 or Windows 11 PC, 64-bit. Nothing to install first: the program carries
  everything it needs besides Windows itself (no Visual C++ Redistributable needed).
- A graphics card that can do Direct3D 11 (nearly every card from the last ten years,
  including the graphics built into most processors).
- 4 GB of memory (RAM).
- About 1.2 GB of free disk space (the game files take 0.93 GB).
- Your own copy of the game: Crash Bandicoot: The Wrath of Cortex, USA, original Xbox
  (the NTSC-U release), as:
    * a disc image (.iso): a "trimmed" XISO, or a full dump of the disc; or
    * a folder with the extracted game (default.xbe and the Crashdat folder in it).
  Other regions (Europe/PAL, Japan), other versions, the PlayStation 2 or GameCube
  versions, and other games of the series don't work: the port is built from the USA Xbox
  release's program, and the setup checks for exactly that disc.
- A controller is best (any Xbox, PlayStation, Switch or Steam controller works), but the
  keyboard plays too.


Starting for the first time
---------------------------
1. Put this folder somewhere you can write to, for example Documents\Project Wrath or
   C:\Games\Project Wrath. Spaces and any letters in the folder names are fine.
   (If you put it in C:\Program Files, the game keeps its files, settings and saves in
   %LOCALAPPDATA%\ProjectWrath instead, and says so.)
2. Double-click ProjectWrath.exe.
   If Windows says "Windows protected your PC", click "More info", then "Run anyway"
   (the program isn't signed; that is all the message means).
3. The setup screen opens: "The game files aren't set up yet." Choose one of:
     - "Choose disc image (.iso)..." and pick your disc image; or
     - "Choose folder with the extracted game..." and pick that folder; or
     - one of the "Disc images found on this PC" (it looks in this folder, your Desktop,
       Downloads and Documents, and one folder down); or
     - type or paste the path into the box and press "Use this path"; or
     - drag the .iso file (or the folder) onto the setup window.
   The mouse, the keyboard (Tab or the arrows, then Enter) and a controller (D-pad, A to
   choose, B to go back) all work.
4. The setup reads the image, says what it found (the disc's own title, its region "North
   America", and "the supported disc"), copies the files (a progress bar with the time
   left; usually well under a minute) and checks every one. Cancel stops it and deletes the
   half-made copy.
5. "Game files ready." Then the game starts by itself.

From then on, double-click ProjectWrath.exe and the game starts directly.
"Play Project Wrath.bat" also starts it; it keeps a console window open beside the game,
which is useful if something goes wrong (see below).

The first start also writes ProjectWrath.ini, the settings, chosen to suit most PCs: the
picture at your screen's own size and shape, in a window, with extra frames drawn between
the game's own at your display's refresh rate. Change them in the settings menu (F1).


If the setup fails
------------------
The setup screen says what happened and what to try, with Retry, "Choose another..." and
Quit. The common cases:
- "This is a different game" or "another region / version": that image isn't the USA Xbox
  release. Only that one works.
- "The disc image is cut short (truncated)": the image didn't copy or download completely.
  Make or copy it again.
- "... isn't the disc's: its contents differ": the image is damaged (or modified). Make it
  again from the disc.
- "Not enough free disk space": free about 1.2 GB on that drive, or move this folder.
- "This folder can't be written to": move this folder somewhere you can write to (for
  example Documents).
- "Another program has the file open": close it (a file manager or an antivirus scan) and
  Retry.
Nothing is left half-done: a failed or cancelled setup deletes only what it made; if the
PC loses power or the game is closed in the middle, the next start cleans up, says "The last
setup didn't finish", and offers Retry. If you already had game files, they stay as they are
until the new ones are complete and checked.

Your files: game_files is the copy of the disc's files the game uses. You may delete your
disc image afterwards, but keeping it lets you set the game up again.


Controls
--------
Controller (Xbox layout; other controllers by position: the bottom button is A):
  Left stick or D-pad  move          A  jump          X  spin          B  crouch / slide
  Start                pause         Hold Start about a second: the settings menu
Keyboard:
  W A S D or the arrows  move        Space (or K)  jump        J (or E)  spin
  L (or Shift)           crouch      Enter (or P)  pause       F1        the settings menu
  Alt+Enter              full screen on / off
Every button and key can be changed in the settings menu (Controls tab).


The settings menu (F1)
----------------------
F1 (or Start held about a second, or View + Menu on the controller) opens it. Tabs:
  Display    window or full screen, the picture's shape (4:3, 16:10, 16:9, 21:9), its size,
             the frame rate (below), the graphics card, Menu scale
  Graphics   texture filtering, anti-aliasing (FXAA, SMAA, MSAA), ambient occlusion
  Audio      volume, music and effects, stereo or 5.1
  Controls   the controller and keyboard bindings, the controllers found
  Saves      the save's backups (below)
  Extras     the characters' fur, pausing in the background, skipping the title animation,
             photo mode
  About      this build, your graphics card and display, the game files: "Verify game
             files" (checks every file against the disc) and "Choose another disc
             image..." (sets the game files up again; your saves are kept); "Copy build
             info" and "Open logs folder" for a problem report (below)
Quit game is at the bottom of every tab. Settings are saved in ProjectWrath.ini; delete it
to start over with the defaults. "Play Project Wrath (safe settings).bat" starts once with
every setting at its default without reading or changing ProjectWrath.ini (if a setting
stops the game from starting).


Frame rate
----------
The game itself always runs at 60 updates a second, as on the Xbox. Display tab, Frame rate:
  Match display   (the default here) extra frames are drawn between the game's own, at your
                  display's refresh rate (120, 144, 240 Hz...): smoother motion. The
                  "adaptive" option drops to fewer extra frames when the PC can't keep up.
  Game (60)       exactly the Xbox's 60 frames a second, nothing drawn in between.
  72 ... 240 fps  a fixed rate.
If the game runs slowly (Display tab, Show frame rate: "Game" under 60 in the picture's
corner), use Game (60), a smaller Render size, or turn anti-aliasing and ambient occlusion
off.


Saving and backups
------------------
Save in the game as on the Xbox (the save console in the warp room hub). The save is in
game_files\UDATA. The game copies it to game_files\UDATA_backups at each start and shortly
after each save (the newest 10 and one a day for 14 days are kept). The Saves tab lists the
backups and restores one at the next start. To keep your progress safe when you move or
reinstall the game, copy game_files\UDATA and game_files\UDATA_backups.


Known quirks
------------
- These are the original game's own (the Xbox does them too): Droid Void's background drone
  plays for the whole level (the Audio tab can silence it); in the asteroid level the pilot
  looks green and furry through the cockpit glass (Extras, Character fur).
- Full screen is "borderless": it covers the screen without changing the display's mode.
- Still being looked into: the player's character has been seen to go invisible for a moment
  in the Drain Damage boss level. If you see something like it, "Collect logs.bat" (below)
  and a note of where it happened help.


What has been tested
--------------------
Played through from start to end on:
  - a desktop PC: NVIDIA GeForce RTX 4080, AMD Ryzen 7 7800X3D, Windows 11, 4K at 240 Hz;
  - a laptop: NVIDIA GeForce RTX 3050 Laptop (4 GB), Intel Core i5-12500H, Windows 11,
    1920 x 1080 at 144 Hz;
  - a Lenovo Legion Go handheld: AMD Ryzen Z1 Extreme, Windows 11, 1920 x 1200 at 144 Hz.
Not tested: Windows 10; AMD Radeon and Intel Arc graphics cards; Intel's built-in graphics on
their own; PCs with less memory than these three; older processors. The requirements above
are estimates: the game itself uses about 0.5 to 0.9 GB of memory while playing (measured on
the desktop), and Direct3D 11 is what it is written for; the slowest PC it has run on is the
Legion Go.


If something goes wrong
-----------------------
- The game won't start after a settings change: "Play Project Wrath (safe settings).bat".
- It runs slowly: see Frame rate above.
- It crashes, freezes or looks wrong: see "Reporting a problem" below.
- The logs are in this folder: ProjectWrath.err.log (the last run) and logs\ (earlier runs).
  They hold the game's technical messages and the paths of its folders; no personal data
  besides your user name in those paths.
- More in the FAQ below.


Reporting a problem
-------------------
Where to report problems: @ISSUES_URL@

1. Start the game with "Play Project Wrath.bat", make the problem happen again if you can,
   and note the level and where in it.
2. Open the settings menu (F1), About tab, and press "Copy build info". It puts a short text
   on the clipboard: this build, your Windows version, processor, graphics card and driver,
   display and refresh rate, the Frame rate setting and the settings you changed. It holds
   no user name, no PC name and no folders. Paste it into your report.
3. Close the game, then double-click "Collect logs.bat". It makes
   ProjectWrath_logs_Share_<date>_<time>.zip in this folder: the game's logs and a short
   summary of the PC (Windows version, processor, graphics card, displays), with your
   Windows user name and home folder replaced by <user> and the PC's name by <pc>
   (scrub.txt in the zip lists where). Look inside before you send it to anyone.
4. Say what you did, what happened, and what you expected; whether it also happens with
   Frame rate: Game (60) (Display tab); and whether "Verify game files" (About tab) says
   all the game files are the disc's. A screenshot helps for anything you see.
The game makes no network connections and sends nothing by itself: a report holds only
what you choose to send. "Open logs folder" (About tab) shows the log files.


FAQ
---
The setup fails.
  The setup screen says why and what to try; "If the setup fails" above lists the usual
  messages. Most often the image is of another release (only the USA Xbox disc works), was
  cut short while copying, or the folder can't be written to (move it to Documents).

The first logo freezes (the picture stops at the start).
  Wait about 10 seconds: the game notices when the screen stops taking its frames and
  recovers by itself. If it stays frozen, press Alt+Tab twice, or Alt+Enter for a window.
  It has only been seen with full screen on: the game starts in a window by default, and
  "Play Project Wrath (safe settings).bat" starts in a window with every setting at its
  default.

A message says xemu_apu.dll is missing, or can't be loaded.
  xemu_apu.dll (the game's audio library) has to be in the same folder as
  ProjectWrath.exe. Copy it back from the package you received (or unpack the package
  again). If it keeps disappearing, your antivirus program may have moved it (see below).

Windows says "Windows protected your PC" (SmartScreen).
  Windows shows this for programs that aren't signed with a code-signing certificate and
  that few people have run yet. ProjectWrath.exe isn't signed. The message doesn't say the
  program is harmful; it says Windows doesn't know it. If you trust where you got it, click
  "More info", then "Run anyway". Nothing else needs changing in Windows.

An antivirus program warns about ProjectWrath.exe or xemu_apu.dll.
  New, unsigned programs are sometimes flagged by mistake (a "false positive"), and this
  one works in unusual ways (it runs a translated Xbox program and emulates its sound
  chip). PACKAGE.txt lists both files' SHA-256: compare them with what you have (in
  PowerShell: Get-FileHash ProjectWrath.exe). Most antivirus makers have a page for
  reporting a false positive. If in doubt, don't run it.

Where are my saves and the logs?
  Saves: game_files\UDATA in this folder, with copies in game_files\UDATA_backups (see
  "Saving and backups"). Logs: ProjectWrath.err.log and logs\ in this folder. Settings:
  ProjectWrath.ini. If the game said it keeps its files in %LOCALAPPDATA%\ProjectWrath
  (when this folder can't be written to), they are all there instead.

How do I report a problem?
  See "Reporting a problem" above.


Removing it
-----------
Delete this folder (copy game_files\UDATA first if you want to keep your saves). If the game
said it keeps its files in %LOCALAPPDATA%\ProjectWrath, delete that folder too. The game
also keeps the Xbox's hard-disk stand-in in %LOCALAPPDATA%\xboxrecomp (a few MB on the disk:
no saves; safe to delete once the game is gone). Nothing else is installed or changed on your
PC (Windows remembers the game's volume in its volume mixer).


Licences
--------
LICENSE.txt says it in plain words: this project's own code is under the MIT licence; the
other people's code in this program keeps its own licence (THIRD_PARTY_LICENSES.txt lists
each one with its full text); no game files and no rights to the game are included or
granted: you supply your own disc.

xemu_apu.dll, next to ProjectWrath.exe, is the game's audio library: the Xbox sound chip's
emulation from the xemu emulator, under the GNU LGPL. The game needs it in the same folder.
Source\xemu_apu-source_....zip is its complete source, with the licence and the steps to
build your own copy and use it in place of this one (its README.txt).
