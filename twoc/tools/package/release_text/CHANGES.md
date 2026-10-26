# The changes each release lists under "What's new" (make_release.ps1 takes the section of VERSION.txt's version).

## 0.1.0

- The first public release.
- The first-run setup takes your own disc image (a trimmed XISO or a full dump) or extracted folder, checks every
  file against the USA disc's, and copies the game files.
- Widescreen (16:9, 16:10, 21:9), any render size, borderless full screen, frames drawn between the game's own at
  your display's refresh rate, anti-aliasing (FXAA, SMAA, MSAA), ambient occlusion, anisotropic filtering.
- Every controller (Xbox, PlayStation, Switch, Steam) and the keyboard, all rebindable; 5.1 sound; separate music and
  effects volume.
- Saves, their backups and the settings are kept in `%LOCALAPPDATA%\ProjectWrath`, so replacing or deleting the game's
  folder never loses them; save backups with restore; portable mode with `portable.txt`.
- Problem reporting: Copy build info, Collect logs (scrubbed of user and PC names).
