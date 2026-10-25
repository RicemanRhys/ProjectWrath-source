# Before you give the Share package to anyone

Step 108 (notes 18.111), updated in step 109 (notes 18.112). Plain words. **None of this is legal advice.** It
records what you decided, what is still open, and what to do, in order. The legal questions are marked as such; they
are for a lawyer (ideally one who knows software licensing and game emulation or porting). `docs/SHARING.md` has the
background for each point.

**After choosing the copyright name or the URL**, in PowerShell from the repo's folder (it builds, makes every
package again and audits each; about 15 minutes):
`powershell -NoProfile -ExecutionPolicy Bypass -File twoc\tools\package\make_package.ps1 -Target LegionGo -Pgo -Force; powershell -NoProfile -ExecutionPolicy Bypass -File twoc\tools\package\make_package.ps1 -Target Nitro5 -Exe twoc\build_pgo\Release\crash_twoc.exe -Force; powershell -NoProfile -ExecutionPolicy Bypass -File twoc\tools\package\make_package.ps1 -Target Share -Exe twoc\build_pgo\Release\crash_twoc.exe -Force; foreach ($d in 'LegionGoPackage\ProjectWrath-LegionGo', 'Nitro5Package\ProjectWrath-Nitro5', 'SharePackage\ProjectWrath-Share') { powershell -NoProfile -ExecutionPolicy Bypass -File twoc\tools\package\package_audit.ps1 -Dir "$env:USERPROFILE\Desktop\Crash\$d" }`

## What is decided

1. **This project's own code is under the MIT licence** (step 108). `twoc/LICENSE` holds the text. `twoc/NOTICE`
   and `docs/SHARING.md` (part 2a) say which files that covers: the port's own source, tools and documents; not
   the third-party code, not the recompiled code, not the game.
2. **The LGPL audio code from xemu is its own DLL, `xemu_apu.dll`, shipped with its source and licence** (step
   108). Every package has the DLL next to the exe; the Share package has its complete source in `Source\` (a zip
   with the LGPL text, a notice of what was changed, and a script that rebuilds it). Anyone can rebuild the DLL and
   put their copy in place of it; nothing else needs rebuilding. Proved in step 108: rebuilt from the zip alone,
   the DLL came out byte for byte the same, and the game ran with it.
3. **The Visual C++ runtime is linked statically** (step 108). No Microsoft runtime DLL ships, and players install
   nothing besides the game itself. The package audit fails if a runtime DLL ever appears in a package.
4. **The project's name is Project Wrath** (step 109). Everything a player sees or touches says so: the exe
   `ProjectWrath.exe` (with Windows file properties: product "Project Wrath", the description "Project Wrath - an
   unofficial fan port (not affiliated with the owners of the game)", no company), the window "Project Wrath", the
   settings `ProjectWrath.ini`, the logs `ProjectWrath.*.log`, `%LOCALAPPDATA%\ProjectWrath`, the launchers "Play
   Project Wrath.bat", the zips `ProjectWrath_logs_*.zip`, the package folders `ProjectWrath-Share` and so on. The
   wording is "Project Wrath is an unofficial fan port. It is not affiliated with, endorsed by or sponsored by the
   owners of the game. You need your own copy of the game", and the game is named only to say which disc is needed.
   The repository, its folders and its build keep their working names (`twoc`, the CMake target `crash_twoc`); the
   packages copy the built exe as ProjectWrath.exe. An older `crash_twoc.ini` or `%LOCALAPPDATA%\CrashTWOC` is read
   or copied, never changed (notes 18.112). The audit fails if the old names show in a Share package.

## What is still open (not decided here)

- **The copyright holder's name.** `twoc/LICENSE` reads `Copyright (c) 2026 [NAME TO BE CHOSEN BY THE OWNER]`.
  It is the one place the name is written; every package's `LICENSE.txt` is made from it. The package audit warns
  loudly ("NOT READY TO SHARE") while the placeholder is there. Change it in `twoc/LICENSE` only.
- **Where people report problems: ISSUES_URL.** `twoc/ISSUES_URL.txt` reads
  `ISSUES_URL = [ISSUES URL TO BE CHOSEN BY THE OWNER]`. It is the one place the address is written: the build
  reads it for the About tab's "Report a problem" button (shown only once it is a real `https://` address), and
  `make_package.ps1` for the Share README's "Where to report problems" line (now "to be announced") and
  `PACKAGE.txt`. The audit warns loudly while it is the placeholder. Change it in `twoc/ISSUES_URL.txt` only.
  When the repository is published, also put its address into `.github/ISSUE_TEMPLATE/config.yml` (its FAQ link,
  `OWNER/REPOSITORY`).
- **Whether the name "Project Wrath" is free to use.** Not checked here, and not something a script can settle:
  search the exact name on GitHub (repositories and organisations), on the web (games, mods, apps, companies), in
  the app stores, and in the trademark databases (the USPTO's search at uspto.gov for the USA, EUIPO's eSearch for
  the EU, WIPO's Global Brand Database for the rest). **That is your job or a lawyer's**, not this project's tools'.
- **The recompiled code inside the exe** (C translated by machine from the game's own program, compiled in).
  Whether giving that to others is allowed is **a question for a lawyer** (`docs/SHARING.md` part 3 lists the ways
  other projects handle it). The MIT licence is not claimed for it, and `LICENSE.txt` says so.
- **Mentions of the game's characters and levels.** The settings menu's help lines name the game's own character
  where a setting is about him (Character fur "Off for Crash only", the 3D sound help, the title animation), and
  the README names levels in its known quirks. They describe the game's content, not the project; whether to keep
  them is your call (`docs/SHARING.md` part 4).
- (Smaller, optional) Whether to publish xboxrecomp's local changes as a fork, and whether to offer the audio
  changes upstream. The licence doesn't need either: the DLL's source travels with the package.

## What to do before giving the Share package to anyone, in order

1. **Get the lawyer's answer on the recompiled code** (open point 4). If the answer is "don't ship it compiled",
   stop here: the Share package as it is now is exactly that.
2. **Check the name "Project Wrath"** as above (the searches are yours or a lawyer's).
3. **Choose the copyright holder's name** and write it into `twoc/LICENSE` in place of
   `[NAME TO BE CHOSEN BY THE OWNER]` (keep the rest of the line: `Copyright (c) 2026 Your Name`). Commit it.
4. **Choose where problems are reported** and write the address into `twoc/ISSUES_URL.txt` in place of
   `[ISSUES URL TO BE CHOSEN BY THE OWNER]` (an `https://` address, for example the repository's issues page; the
   issue forms in `.github/ISSUE_TEMPLATE/` are ready for a GitHub repository). Commit it.
5. **Make the packages again** from PowerShell, so they carry all of the above: the command at the top of this file
   (the PGO build, then the three packages from that one exe, each audited).
6. **Read the audit**: it runs by itself at the end of each package; for the Share package it must say
   `AUDIT PASSED` in green, with **no** yellow "NOT READY TO SHARE" warning (neither the copyright name nor
   ISSUES_URL). Run it again on any copy you are about to send:
   `powershell -NoProfile -ExecutionPolicy Bypass -File twoc\tools\package\package_audit.ps1 -Dir "<the folder>"`.
7. **Try it as a stranger would**: copy the `ProjectWrath-Share` folder to another PC (or another user account),
   read `README.txt` first, double-click `ProjectWrath.exe`, set it up from your disc image, play a minute, open the
   F1 menu (About: Copy build info, paste it somewhere and read it), quit.
8. **Check `Source\` is there** (the audio library's source zip) and that `LICENSE.txt` and
   `THIRD_PARTY_LICENSES.txt` are next to the exe.
9. **Zip the folder and give it** the way you chose. Keep a copy of exactly what you gave (and the commit it was
   made from: `PACKAGE.txt` names it), so you can answer for it later, and keep the source zip available for as
   long as you share the program.

Nothing in these steps is published or uploaded by any script here: the last step is yours.
