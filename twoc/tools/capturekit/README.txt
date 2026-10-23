Crash TWOC capture kit (step 100; exe refreshed in step 104)
============================================================

What this is
------------
A separate copy of the game for catching the playtest problems I couldn't
reproduce on my own. It has its own exe (the latest build), its own copy of
your settings (crash_twoc.ini, copied once from twoc\build\Release) and its
own copy of your save (game\game_files\UDATA, copied once). Your build
folder, your ini and your saves are never used or changed by it. The game
data (Crashdat) is a link to the repo's, so the kit is small.

How to use it
-------------
1. Double-click the numbered .cmd for the problem (below). A console window
   opens and the game starts with your usual settings (Match display, 4K,
   HUD at the edges and so on). Load your game from the main menu as usual.
2. Play to the spot. The moment the problem is on the screen, double-click
   "capture now.cmd". The game then runs at about 1 frame a second for
   3 frames while it writes them out: that's expected, it goes back to
   normal by itself. You can capture again after 10 seconds.
3. Close the game window when you're done. The console tells you the folder
   to send back: captures\<problem>_<date>_<time>\. Zip it and send it
   (a capture at 4K is a few hundred MB: mostly .bmp pictures).

The "b" launchers are the same problem at Frame rate: Game (60), to tell me
whether it happens only with Match display (the in-between frames). If you
have time, try both and say which showed it.

Please also write a line in a text file in the capture folder: what you did
and what you saw (e.g. "died to the boss's wave at 1:40, respawned invisible,
capture at 1:45").

The problems
------------
1  Drain Damage: Crash invisible after the boss kills him
   Go to Drain Damage (the boss). Let the boss's attack kill Crash (not a
   fall into the water: I tried those and Crash came back visible). When he
   respawns invisible, double-click "capture now.cmd", walk a little, and
   capture once more. Then let him die again and capture once he is visible.
   1b: the same at Game (60).

2  Droid Void: the loading music under the level's music
   This one records the game's sound for the whole run (sound.wav in the
   capture folder, about 11 MB a minute). Enter Droid Void the way you did
   (from the warp room, through the vortex scene with the voices if it
   plays). As soon as you hear the loading music still playing in the level,
   double-click "capture now.cmd" (that marks the time) and stay in the level
   another 30 seconds. Then quit. If it doesn't happen, enter the level again
   a couple of times. (I tried: in my runs the vortex sound stopped when the
   level's music started, watched or skipped with Start.)
   2b: the same at Game (60).

3  Crashteroids: Coco's strange green furry texture
   Go to Crashteroids. When Coco (in the cockpit) shows the green furry look
   clearly, capture. If it changes while flying (turning, shooting, near the
   planet), capture again then.
   3b: the same at Game (60).

4  HUD flights: please check the fix
   This build fixes the extra-life head and the bonus round's flying fruit
   with the HUD at the screen edges (they now land on their 16:9 counters).
   Play a bonus round and get an extra life (100 wumpa). If anything still
   misses its counter, capture as it flies. Nothing else needed if it's right.

5  Shaking / flickering textures
   Smokey and the Bandicoot: the road-side textures and the shaking ground
   late in the level. Gold Rush: about 3/4 through, the buildings' textures
   moving left and right. Medieval Madness: half-way, shifting textures.
   When you see it, capture while moving (keep moving through the capture).
   5 runs at your own settings (Match display): the in-between frames are
   what this is about. 5b is the comparison: the same at Frame rate:
   Game (60), with no in-between frames at all. Use 5 to show the problem,
   5b at the same spot to show it isn't there (or that it is).
   What is fixed in this build (step 103): late in Gold Rush and Medieval
   Madness the buildings and ground drawn by one path shifted 1-20 pixels
   (at 4K) in every in-between frame and came back on the game's own
   frames, and wall textures over them came and went with it (your step 102
   captures: Gold Rush and Medieval Madness). The decal fix of step 102 (wall
   marks drawn a hair nearer in the in-between frames) is in too, always on
   at Match display. If Smokey and the Bandicoot's ground still shakes, or
   anything still shifts in Gold Rush or Medieval Madness, capture with 5
   while moving, then once with 5b at the same spot.

6  Fahrenheit Frenzy: the X-ray machine
   Half-way through, at the X-ray machine. When the room looks black (only
   the light beams and the green smoke showing), capture; walk Crash through
   the machine and capture again while you see his skeleton.
   6b: the same at Game (60).
   If you can, also take a screenshot of the same spot in xemu: the "real
   hardware" picture is from a video and I can't tell which console it shows.

7  Rolling capture: the black triangle on a wall (step 104)
   For the glitch that lasts a frame or two while Crash moves (a dark
   triangle forming on a wall; Smokey and the Bandicoot). This launcher
   keeps the last 2 seconds or so in memory all the time: every frame
   shown (the in-between ones too) as a small picture, the game's draws
   of the last half second or so (with Match display), and a check of
   every draw for broken numbers. Play normally at your usual settings.
   Right AFTER you see the glitch, press F9 in the game window (or
   double-click "capture now.cmd"); the seconds just before the key are
   written to captures\rolling_<date>_<time>\. Pressing it a second or so
   late is fine. You can press it again any time after 5 seconds; each
   press makes its own folder. It uses about 300 MB of memory at 240 Hz
   (the same at 4K and 1080p: the pictures are always 480 pixels wide),
   about 60 MB at Game (60), and nothing else is slowed (measured at 1080p,
   240 Hz: every frame still shown on its refresh). F9 is never passed to
   the game. Please send the rolling_ folders and the 7_rolling_ folder
   (its run.err.log has the check's lines). If you also try it at Game
   (60), note it in a text file: the triangle in your picture was taken at
   Game (60) (the counter says "Adaptive: - (Game (60))").

What gets written (in the capture folder)
-----------------------------------------
run.err.log     the game's log (the draw log of the captured frames is in it)
hostsub*.bmp    every in-between picture of 2 game frames
host*.bmp       the pictures of the 3 logged frames
interp.bin      the interpolation's record of 20 frames (about 30 MB)
captures.txt    when you pressed "capture now"
sound.wav       (problem 2 only) the game's sound
power.log       the usual power log
rolling_<time>\  (problem 7, beside the other folders in captures\) frames\*.bmp, every frame
                shown in the last ~2 s at 480 pixels wide; interp.bin, the draws of the last game
                frames (Match display only); anomalies.txt; info.txt (the level, the settings)
