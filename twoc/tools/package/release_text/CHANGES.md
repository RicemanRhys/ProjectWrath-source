# What each release has (make_release.ps1 puts the section of VERSION.txt's version under "What's in <version>").

## 1.0.0

The first public version.

**What it does**

- Crash Bandicoot: The Wrath of Cortex (the original Xbox game, USA release) on Windows, from your own copy of the
  game. No game files are included.
- The first-run setup takes your own disc image (a trimmed XISO or a full dump) or extracted folder, checks every
  file against the USA disc's, and copies the game files.
- Widescreen (16:9, 16:10, 21:9), any render size, borderless full screen, frames drawn between the game's own at
  your display's refresh rate, anti-aliasing (FXAA, SMAA, MSAA), ambient occlusion, anisotropic filtering.
- Every controller (Xbox, PlayStation, Switch, Steam) and the keyboard, all rebindable; 5.1 sound; separate music and
  effects volume.
- Saves, their backups and the settings are kept in `%LOCALAPPDATA%\ProjectWrath`, so replacing or deleting the game's
  folder never loses them; save backups with restore; portable mode with `portable.txt`.
- Problem reporting: Copy build info, Collect logs (scrubbed of user and PC names).

**Tested on**

- A desktop PC with an NVIDIA GeForce RTX 4080 (Windows 11).
- A laptop with an NVIDIA GeForce RTX 3050 Laptop GPU (Windows 11).

**Not tested yet**

- The Lenovo Legion Go and other handhelds.
- AMD and Intel desktop graphics cards.
- Windows 10.
- PAL (European) discs: the setup only accepts the USA disc, so they don't work.

**Known small things**

- Rarely, the sound stalls for about 2 seconds, then carries on by itself.
- There is one report of Crash turning invisible in Drain Damage; it hasn't been reproduced yet.
- In full screen, the first logo can freeze for up to 10 seconds on some PCs; it recovers by itself.

**"Windows protected your PC"**

- The exe isn't signed with a code-signing certificate, so Windows SmartScreen may warn the first few times it runs:
  click **More info**, then **Run anyway**.
