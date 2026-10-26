# Project Wrath {{VERSION}}

An unofficial fan port of Crash Bandicoot: The Wrath of Cortex (original Xbox, USA) to Windows. It is not
affiliated with, endorsed by or sponsored by the owners of the game. **No game files are included: you need your own
copy of the game** (a disc image of the USA Xbox release, or its extracted files).

## Download

`{{ZIP}}` ({{SIZE_MB}} MB): extract it, double-click `ProjectWrath.exe`, choose your disc image. The README inside says
the rest.

## What's new

{{CHANGES}}

## Known issues

- Windows 10, AMD and Intel desktop graphics cards, and handhelds are not tested yet.
- Only the USA Xbox disc works (no PAL or Japanese discs).
- Full screen can freeze on the first logo for up to 10 seconds on some PCs; it recovers by itself.
- The player's character has been seen to vanish for a moment in one boss level.

## Check the download

`SHA256SUMS.txt` lists the SHA-256 of each file of this release. In PowerShell, in the folder you downloaded to:

    Get-FileHash .\{{ZIP}} -Algorithm SHA256

The hash must be `{{ZIP_SHA256}}`. Inside the zip, `PACKAGE.txt` lists the hashes of `ProjectWrath.exe`
({{EXE_SHA256}}) and `xemu_apu.dll` ({{DLL_SHA256}}).

## "Windows protected your PC"

`ProjectWrath.exe` isn't signed with a code-signing certificate, so Windows SmartScreen may warn the first few times
people run it. If you trust where you downloaded it from, click **More info**, then **Run anyway**. Nothing else in
Windows needs changing. If an antivirus program flags it, compare the hashes above; most antivirus makers have a page
for reporting a false positive.

## Source and licences

Built from {{COMMIT}} (toolkit {{TOOLKIT}}). Project Wrath's own code is MIT; `xemu_apu.dll` (the Xbox audio chip,
from xemu) is LGPL-2.1, its complete source is `{{APU_ZIP}}` (also inside the zip's `Source` folder).
