# Before you give the Share package to anyone

Step 108 (notes 18.111). Plain words. **None of this is legal advice.** It records what you decided, what is still
open, and what to do, in order. The legal questions are marked as such; they are for a lawyer (ideally one who
knows software licensing and game emulation or porting). `docs/SHARING.md` has the background for each point.

## What is decided (your three choices, made in step 108)

1. **This project's own code is under the MIT licence.** `twoc/LICENSE` holds the text. `twoc/NOTICE` and
   `docs/SHARING.md` (part 2a) say which files that covers: the port's own source, tools and documents; not the
   third-party code, not the recompiled code, not the game.
2. **The LGPL audio code from xemu is its own DLL, `xemu_apu.dll`, shipped with its source and licence.** Every
   package has the DLL next to the exe; the Share package has its complete source in `Source\` (a zip with the
   LGPL text, a notice of what was changed, and a script that rebuilds it). Anyone can rebuild the DLL and put
   their copy in place of it; nothing else needs rebuilding. Proved in step 108: rebuilt from the zip alone, the DLL
   came out byte for byte the same, and the game ran with it.
3. **The Visual C++ runtime is linked statically.** No Microsoft runtime DLL ships, and players install nothing
   besides the game itself. The package audit fails if a runtime DLL ever appears in a package.

## What is still open (not decided here)

- **The copyright holder's name.** `twoc/LICENSE` reads `Copyright (c) 2026 [NAME TO BE CHOSEN BY THE OWNER]`.
  It is the one place the name is written; every package's `LICENSE.txt` is made from it. The package audit warns
  loudly ("NOT READY TO SHARE") while the placeholder is there.
- **The project's name.** The program, its folders and windows say "Crash TWOC" (`docs/SHARING.md` part 4 on
  names and trademarks).
- **The recompiled code inside `crash_twoc.exe`** (C translated by machine from the game's own program, compiled
  in). Whether giving that to others is allowed is **a question for a lawyer** (`docs/SHARING.md` part 3 lists the
  ways other projects handle it). The MIT licence is not claimed for it, and `LICENSE.txt` says so.
- **Where people report problems** (an issue tracker, a Discord, an e-mail address, or nowhere): `docs/SHARING.md`
  part 7.
- (Smaller, optional) Whether to publish xboxrecomp's local changes as a fork, and whether to offer the audio
  changes upstream. The licence doesn't need either: the DLL's source travels with the package.

## What to do before giving the Share package to anyone, in order

1. **Get the lawyer's answer on the recompiled code** (open point 3). If the answer is "don't ship it compiled",
   stop here: the Share package as it is now is exactly that.
2. **Choose the copyright holder's name** and write it into `twoc/LICENSE` in place of
   `[NAME TO BE CHOSEN BY THE OWNER]` (keep the rest of the line: `Copyright (c) 2026 Your Name`). Commit it.
3. **Choose the project's name**, if it should change from "Crash TWOC", and decide whether the names in the
   program, its windows and its folders follow (a code change; ask for it as its own step).
4. **Choose where problems are reported**, and add that line to `twoc/tools/package/targets/Share/README.txt`
   (the "If something goes wrong" part).
5. **Make the package again** from PowerShell, so it carries all of the above:
   `powershell -NoProfile -ExecutionPolicy Bypass -File twoc\tools\package\make_package.ps1 -Target Share -Exe twoc\build_pgo\Release\crash_twoc.exe -Force`
   (after a `-Target LegionGo -Pgo -Force` run has made that PGO exe, or leave `-Exe` out for the ordinary build).
6. **Run the audit and read it**: it runs by itself at the end; it must say `AUDIT PASSED` in green, with **no**
   yellow "NOT READY TO SHARE" warning. Run it again on any copy you are about to send:
   `powershell -NoProfile -ExecutionPolicy Bypass -File twoc\tools\package\package_audit.ps1 -Dir "<the folder>"`.
7. **Try it as a stranger would**: copy the folder to another PC (or another user account), read `README.txt`
   first, double-click `crash_twoc.exe`, set it up from your disc image, play a minute, open the F1 menu, quit.
8. **Check `Source\` is there** (the audio library's source zip) and that `LICENSE.txt` and
   `THIRD_PARTY_LICENSES.txt` are next to the exe.
9. **Zip the folder and give it** the way you chose. Keep a copy of exactly what you gave (and the commit it was
   made from: `PACKAGE.txt` names it), so you can answer for it later, and keep the source zip available for as
   long as you share the program.

Nothing in these steps is published or uploaded by any script here: the last step is yours.
