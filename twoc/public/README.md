# Project Wrath

**Project Wrath is an unofficial fan port. It is not affiliated with, endorsed by or sponsored by the owners of the
game. You need your own copy of the game.**

It is a PC version of an original Xbox game from 2001: the game's own Xbox program was translated into a Windows
program ("static recompilation"), so it is the same game, running natively on Windows, with extras such as
widescreen, high frame rates and modern controllers. All trademarks belong to their owners.

**No game files are included.** The download holds only the program. The first time you start it, it copies what it
needs from your own disc image into its folder and checks every file. Nothing is downloaded and nothing is sent
anywhere.

## What you need

- Windows 10 or Windows 11, 64-bit. Nothing to install first (no Visual C++ Redistributable needed).
- A graphics card that can do Direct3D 11 (nearly every card of the last ten years, including the graphics built into
  most processors).
- About 1.2 GB of free disk space (the game files take 0.93 GB).
- Your own copy of the game: **Crash Bandicoot: The Wrath of Cortex, USA, original Xbox**, as a disc image (`.iso`: a
  "trimmed" XISO or a full dump of the disc) or as the extracted files (a folder with `default.xbe` and `Crashdat`).
  Other regions (Europe/PAL, Japan), other versions and the PlayStation 2 or GameCube versions are not supported: the
  port is built from the USA Xbox release's program, and the setup checks for exactly that disc.
- A controller is best (Xbox, PlayStation, Switch and Steam controllers all work); the keyboard plays too.

## Three steps

1. Download `ProjectWrath-<version>-win64.zip` and extract it somewhere you can write to, for example
   `Documents\Project Wrath` or `C:\Games\Project Wrath` (not `C:\Program Files`).
2. Double-click `ProjectWrath.exe`. If Windows says "Windows protected your PC", click **More info**, then **Run
   anyway** (the program isn't signed; that is all the message means).
3. The setup screen asks for your game: choose your disc image (or the folder with the extracted game), or drag it
   onto the window. It checks the disc, copies the files (usually well under a minute), and the game starts.

From then on, double-click `ProjectWrath.exe` and the game starts directly. `Play Project Wrath.bat` does the same and
keeps a console window open beside the game, useful if something goes wrong.

## If the setup fails

The setup screen says what happened and what to try, with Retry, "Choose another..." and Quit. The usual cases:

- "This is a different game" or "another region / version": that image isn't the USA Xbox release; only that one
  works.
- "The disc image is cut short (truncated)": the image didn't copy or download completely; make or copy it again.
- "... isn't the disc's: its contents differ": the image is damaged or modified; make it again from the disc.
- "Not enough free disk space": free about 1.2 GB on that drive, or move the game's folder.
- "This folder can't be written to": move the game's folder somewhere you can write to (for example Documents).
- "Another program has the file open": close it (a file manager or an antivirus scan) and Retry.

Nothing is left half-done: a failed or cancelled setup deletes only what it made, and if the PC loses power in the
middle, the next start cleans up and offers Retry. You may delete your disc image afterwards; keeping it lets you set
the game up again (F1 > About > Choose another disc image...).

## What has been tested

- A desktop PC: NVIDIA GeForce RTX 4080, AMD Ryzen 7 7800X3D, Windows 11, 4K at 240 Hz.
- A laptop: NVIDIA GeForce RTX 3050 Laptop (4 GB), Intel Core i5-12500H, Windows 11, 1920 x 1080 at 144 Hz.

**Not tested:** Windows 10; AMD Radeon and Intel Arc desktop graphics cards; Intel's built-in graphics on their own;
the Lenovo Legion Go and other handhelds; PCs with less memory or older processors. PAL and Japanese discs are not
supported. Reports from any of these help (see "Reporting a problem").

## The settings menu (F1)

Press **F1** (or hold **Start** on the controller for about a second) in the game. Tabs:

- **Display**: window or full screen (Alt+Enter also switches), the picture's shape (4:3, 16:10, 16:9, 21:9), its
  size, the frame rate, the graphics card, the menu's size.
- **Graphics**: texture filtering, anti-aliasing (FXAA, SMAA, MSAA), ambient occlusion.
- **Audio**: volume, music and effects, stereo or 5.1.
- **Controls**: every controller button and keyboard key can be changed.
- **Saves**: where your saves are, their backups, Open saves folder.
- **Extras**: the characters' fur, pausing in the background, skipping the title animation, photo mode.
- **About**: this build, your graphics card and display; **Verify game files** (checks every file against the disc);
  **Choose another disc image...**; **Copy build info** and **Open logs folder** for a problem report.

The game itself always runs at 60 updates a second, as on the Xbox. **Frame rate: Match display** (the default) draws
extra frames between the game's own at your display's refresh rate for smoother motion; **Game (60)** shows exactly
the Xbox's 60. If it runs slowly, choose Game (60), a smaller render size, or turn anti-aliasing and ambient
occlusion off. `Play Project Wrath (safe settings).bat` starts once with every setting at its default.

## Saves, backups and settings

Save in the game as on the Xbox (the save console in the warp room hub). Your saves, their backups and your
settings are kept in your user folder, **not** in the game's folder:

| | |
|---|---|
| `%LOCALAPPDATA%\ProjectWrath\saves` | the saves (`UDATA`) and their backups (`UDATA_backups`) |
| `%LOCALAPPDATA%\ProjectWrath\ProjectWrath.ini` | the settings |

Paste `%LOCALAPPDATA%\ProjectWrath` into File Explorer's address bar to open it, or press **Open saves folder** in the
Saves tab. So replacing the game's folder with a newer version, moving it or deleting it never touches your saves
or settings.

- **Backups**: the game copies the save to `saves\UDATA_backups` at each start and a few seconds after each save
  (the newest 10 and one a day for 14 days are kept). The Saves tab lists them and restores one at the next start.
- **Backing up or moving your saves**: close the game, copy the `saves` folder (and `ProjectWrath.ini` for your
  settings), and on the other PC put them in `%LOCALAPPDATA%\ProjectWrath`.
- **Several copies of the game** on one PC all use the same saves and settings. To keep one copy apart (**portable
  mode**), put a file named `portable.txt` next to its `ProjectWrath.exe`: that copy keeps its saves in its own
  `game_files\UDATA` and its settings next to the exe.
- **Updating**: extract the new version into a new folder (or over the old one) and start it: your saves and settings
  are found where they are. A version that kept its saves in the game's folder (`game_files\UDATA`) has them copied to
  `%LOCALAPPDATA%\ProjectWrath\saves` at the first start of this one (every file checked; it says so once; the old
  copy is left where it was). If both places already hold saves, the game uses the ones in `%LOCALAPPDATA%` and leaves
  the other alone.
- If `%LOCALAPPDATA%\ProjectWrath` can't be used (a full disk, a read-only folder), the game says why and keeps the
  saves in its own folder for that run: nothing is lost.

## Controls

Controller (Xbox layout; other controllers by position: the bottom button is A): left stick or D-pad move, A jump,
X spin, B crouch / slide, Start pause; hold Start for the settings menu.
Keyboard: W A S D or the arrows move, Space (or K) jump, J (or E) spin, L (or Shift) crouch, Enter (or P) pause,
F1 the settings menu, Alt+Enter full screen.

## Known quirks

- From the original game (the Xbox does them too): one level's background drone plays for the whole level (the
  Audio tab can silence it); in the asteroid level the pilot looks furry through the cockpit glass (Extras, Character
  fur).
- Full screen is "borderless": it covers the screen without changing the display's mode.
- Being looked into: the player's character has been seen to vanish for a moment in one boss level.
- The first logo can freeze for up to 10 seconds in full screen on some PCs; it recovers by itself (Alt+Tab twice if
  not). The game starts in a window by default.

## Reporting a problem

Where to report problems: **[ISSUES URL TO BE CHOSEN BY THE OWNER]**

1. Make the problem happen again if you can (start with `Play Project Wrath.bat`) and note where in the game.
2. F1 > About > **Copy build info**, and paste it into your report (no user name, PC name or folders in it).
3. Close the game and double-click `Collect logs.bat`: it makes `ProjectWrath_logs_<date>_<time>.zip` with the logs
   and a short PC summary, your user name and PC name replaced. Look inside, then attach it.
4. Say what you did, what happened and what you expected; whether it also happens with Frame rate: Game (60); and
   whether **Verify game files** says all the files are the disc's.

Never attach game files, disc images or saves. The game makes no network connections and sends nothing by itself.

## FAQ

**The first logo freezes.** Wait about 10 seconds: the game notices and recovers by itself. If it stays frozen,
press Alt+Tab twice, or Alt+Enter for a window. It has only been seen in full screen; the game starts in a window by
default.

**A message says xemu_apu.dll is missing or can't be loaded.** `xemu_apu.dll` (the game's audio library) must be
in the same folder as `ProjectWrath.exe`. Extract the download again; if it keeps disappearing, your antivirus may
have moved it.

**Windows says "Windows protected your PC".** SmartScreen shows this for programs that aren't signed with a
code-signing certificate and that few people have run yet. It doesn't say the program is harmful. If you trust where
you got it, click More info, then Run anyway.

**An antivirus program warns about ProjectWrath.exe or xemu_apu.dll.** New, unsigned programs are sometimes flagged
by mistake. `PACKAGE.txt` lists both files' SHA-256: compare them with yours (in PowerShell:
`Get-FileHash .\ProjectWrath.exe`). Most antivirus makers have a page for reporting a false positive. If in doubt,
don't run it.

**Where are my saves, settings and logs?** Saves and settings: `%LOCALAPPDATA%\ProjectWrath` (the Saves tab shows
the exact folder). Logs: `ProjectWrath.err.log` and `logs\` in the game's folder (F1 > About > Open logs folder).

## Removing it

Delete the game's folder. Your saves and settings stay in `%LOCALAPPDATA%\ProjectWrath`: delete that folder too to
remove them (copy its `saves` folder first if you want to keep them). Nothing else is installed or changed on your
PC (Windows remembers the game's volume in its volume mixer). Earlier test builds kept the Xbox hard-disk stand-in
in `%LOCALAPPDATA%\xboxrecomp`: safe to delete.

## Licences and thanks

Project Wrath's own code is under the MIT licence (`LICENSE`). The other people's code it uses keeps its own licence
(`THIRD_PARTY_LICENSES.txt` has each one's full text). No game files and no rights to the game are included or
granted: you supply your own disc.

With thanks to:

- **xemu** (https://xemu.app): the emulated Xbox audio chip, `xemu_apu.dll`, under the GNU LGPL 2.1; its complete
  source is in the download's `Source` folder, with the steps to build and replace it.
- **xboxrecomp** by sp00nznet (https://github.com/sp00nznet/xboxrecomp): the recompilation toolkit, kernel
  replacement and Direct3D 8 translator this port is built on (MIT).
- **Dear ImGui** by Omar Cornut and contributors (the settings menu), **SDL** (controllers), **XeGTAO** by Intel
  (ambient occlusion), **SMAA** by Jorge Jimenez and others (anti-aliasing), **volk** and the **Vulkan headers**, and
  everyone listed in `THIRD_PARTY_LICENSES.txt`.

## About how it was made

<!-- OWNER: this section's final wording is yours to edit. -->
This port was developed with the help of an AI coding assistant (Claude Code, by Anthropic), working under my
direction: I chose what to build, ran and tested every version on my own PCs and decided what went in. *(Draft
wording, to be edited by the owner.)*

## For developers

The source is in this repository without the translated game code: you regenerate it from your own disc. See
`CONTRIBUTING.md` (building) and `docs/DEVELOPING.md` (how it works).
