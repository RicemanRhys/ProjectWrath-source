Crash TWOC capture kit (step 100)
=================================

What this is
------------
A separate copy of the game for catching the playtest problems I couldn't
reproduce on my own. It has its own exe (this step's build), its own copy of
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
   What I found: in Gold Rush late in the level, decals on walls (a brick
   patch) vanish in the in-between frames and come back on every game frame:
   a flicker. This build halves it; the full fix is the next step. The
   captures tell me whether what you see in Smokey and Medieval Madness is
   the same thing.
   5b: the same at Game (60): does it happen there at all?

6  Fahrenheit Frenzy: the X-ray machine
   Half-way through, at the X-ray machine. When the room looks black (only
   the light beams and the green smoke showing), capture; walk Crash through
   the machine and capture again while you see his skeleton.
   6b: the same at Game (60).
   If you can, also take a screenshot of the same spot in xemu: the "real
   hardware" picture is from a video and I can't tell which console it shows.

What gets written (in the capture folder)
-----------------------------------------
run.err.log     the game's log (the draw log of the captured frames is in it)
hostsub*.bmp    every in-between picture of 2 game frames
host*.bmp       the pictures of the 3 logged frames
interp.bin      the interpolation's record of 20 frames (about 30 MB)
captures.txt    when you pressed "capture now"
sound.wav       (problem 2 only) the game's sound
power.log       the usual power log
