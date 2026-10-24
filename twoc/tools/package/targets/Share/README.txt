Crash Bandicoot: The Wrath of Cortex - an unofficial PC port (Crash TWOC)
========================================================================

What this is
------------
A fan-made PC version of the original Xbox game Crash Bandicoot: The Wrath of Cortex (2001).
The game's own Xbox program was translated into a Windows program ("static recompilation"):
it is the same game, running natively on Windows, with extras such as widescreen, high
frame rates and modern controllers.

It is unofficial. It is not made, approved or supported by the owners of Crash Bandicoot or
of the original game, or by Microsoft. All trademarks belong to their owners.

NO GAME FILES ARE INCLUDED. This folder holds only the program. To play you need your own
copy of the game: an image of the original Xbox disc (USA release) or the files extracted
from one. The first time you start it, the game copies what it needs from your image into
this folder and checks every file. Nothing is downloaded, and nothing is sent anywhere.


What you need
-------------
- A Windows 10 or Windows 11 PC, 64-bit. Nothing to install first: the program carries
  everything it needs besides Windows itself (no Visual C++ Redistributable needed).
- A graphics card that can do Direct3D 11 (nearly every card from the last ten years,
  including the graphics built into most processors).
- 4 GB of memory (RAM).
- About 1.2 GB of free disk space (the game files take 0.93 GB).
- Your own copy of Crash Bandicoot: The Wrath of Cortex for the original Xbox, USA (NTSC-U)
  release, as:
    * a disc image (.iso): a "trimmed" XISO, or a full dump of the disc; or
    * a folder with the extracted game (default.xbe and the Crashdat folder in it).
  Other regions (Europe/PAL, Japan), other versions, the PlayStation 2 or GameCube
  versions, and other Crash games don't work: the port is built from the USA Xbox release's
  program, and the setup checks for exactly that disc.
- A controller is best (any Xbox, PlayStation, Switch or Steam controller works), but the
  keyboard plays too.


Starting for the first time
---------------------------
1. Put this folder somewhere you can write to, for example Documents\Crash TWOC or
   C:\Games\Crash TWOC. Spaces and any letters in the folder names are fine.
   (If you put it in C:\Program Files, the game keeps its files, settings and saves in
   %LOCALAPPDATA%\CrashTWOC instead, and says so.)
2. Double-click crash_twoc.exe.
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
4. The setup reads the image, says what it found (it should say "Crash Bandicoot: tWoC ...
   region North America ... the supported disc"), copies the files (a progress bar with the
   time left; usually well under a minute) and checks every one. Cancel stops it and
   deletes the half-made copy.
5. "Game files ready." Then the game starts by itself.

From then on, double-click crash_twoc.exe and the game starts directly.
"Play Crash.bat" also starts it; it keeps a console window open beside the game, which is
useful if something goes wrong (see below).

The first start also writes crash_twoc.ini, the settings, chosen to suit most PCs: the
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
             image..." (sets the game files up again; your saves are kept)
Quit game is at the bottom of every tab. Settings are saved in crash_twoc.ini; delete it to
start over with the defaults. "Play Crash (safe settings).bat" starts once with every
setting at its default without reading or changing crash_twoc.ini (if a setting stops the
game from starting).


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
  plays for the whole level (the Audio tab can silence it); in Crashteroids Coco looks green
  and furry through the cockpit glass (Extras, Character fur).
- Full screen is "borderless": it covers the screen without changing the display's mode.
- Still being looked into: Crash has been seen to go invisible for a moment in Drain Damage.
  If you see something like it, "Collect logs.bat" (below) and a note of where it happened
  help.


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
- The game won't start after a settings change: "Play Crash (safe settings).bat".
- It runs slowly: see Frame rate above.
- It crashes, freezes or looks wrong: start it with "Play Crash.bat", make it happen
  again, close it, then double-click "Collect logs.bat". It makes a .zip of the game's logs
  and a short summary of the PC (Windows version, processor, graphics card, displays) in
  this folder, with your Windows user name and home folder replaced by <user> and the PC's
  name by <pc>. Look inside before you send it to anyone; nothing is sent by itself.
- The logs are in this folder: run_twoc.err.log (the last run) and logs\ (earlier runs).
  They hold the game's technical messages and the paths of its folders; no personal data
  besides your user name in those paths.


Removing it
-----------
Delete this folder (copy game_files\UDATA first if you want to keep your saves). If the game
said it keeps its files in %LOCALAPPDATA%\CrashTWOC, delete that folder too. Nothing else is
installed or changed on your PC (Windows remembers the game's volume in its volume mixer).


Licences
--------
LICENSE.txt says it in plain words: this project's own code is under the MIT licence; the
other people's code in this program keeps its own licence (THIRD_PARTY_LICENSES.txt lists
each one with its full text); no game files and no rights to the game are included or
granted: you supply your own disc.

xemu_apu.dll, next to crash_twoc.exe, is the game's audio library: the Xbox sound chip's
emulation from the xemu emulator, under the GNU LGPL. The game needs it in the same folder.
Source\xemu_apu-source_....zip is its complete source, with the licence and the steps to
build your own copy and use it in place of this one (its README.txt).
