# Before you give the Share package to anyone

Step 108 (notes 18.111), updated in steps 109 (notes 18.112) and 110 (notes 18.113). Plain words. **None of this is legal advice.** It
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

5. **The copyright holder's name is Rony Industries** (`twoc/LICENSE`, the one place it is written).
6. **Saves, backups and settings live in `%LOCALAPPDATA%\ProjectWrath`** (step 110): replacing, updating or deleting
   the game's folder never loses them; the first start copies an older package's saves there (checked, the old copy
   kept); `portable.txt` beside the exe keeps everything in the game's folder.
7. **The way of sharing: one prebuilt zip** (the Share package, the exe with the translated code, the audio DLL, no
   game files) as a GitHub Release, and a public source repository with no generated code, no game files, no exe, no
   disassembly and no personal details (`docs/PUBLIC_REPO.md` says what goes in; `tools/make_public_tree.ps1`
   exports it, `tools/audit_public_tree.ps1` checks it; `tools/package/make_release.ps1` prepares the release).
   Whether this is allowed is the lawyer's question (`docs/LAWYER_QUESTIONS.md`).
8. **The public identity is `RicemanRhys <176650384+RicemanRhys@users.noreply.github.com>`** (step 111): GitHub's
   noreply address for the owner's account. `tools/make_public_tree.ps1` (`$identityMail`) commits the public tree
   and writes the toolkit patches' author lines with it; `tools/audit_public_tree.ps1` (`$publicId`) fails on any
   other commit identity. The private e-mail is never used there.
9. **Problems are reported at https://github.com/RicemanRhys/ProjectWrath/issues** (step 111): ISSUES_URL in
   `twoc/ISSUES_URL.txt`, the one place it is written. The build reads it for the About tab's "Report a problem"
   button, `make_package.ps1` for the Share README's "Where to report problems" line and `PACKAGE.txt`; the public
   README shows it too (`readme_txt.py` puts the packaging's `@ISSUES_URL@` token in its place, so the packages
   always follow `twoc/ISSUES_URL.txt`). To change it: that file, the public README's line, build, packages again.

## What is still open (not decided here)

- **Whether to publish `seed_functions.json`** (52 function addresses in the game): without it the public tree can't
  regenerate a working program (`docs/PUBLIC_REPO.md`); `make_public_tree.ps1 -IncludeSeeds` adds it. A lawyer's
  question with the others.
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
- (Smaller, optional) Whether to make the public fork of xboxrecomp (recommended, `docs/PUBLIC_REPO.md`: the export
  already carries the changes as patches that rebuild the toolkit exactly), and whether to offer changes upstream.

## What to do before publishing, in order (step 110)

1. **Get the lawyer's answers** (`docs/LAWYER_QUESTIONS.md`: the recompiled code in the exe, the public source's
   game-specific parts and the seeds, protection measures, the name, AI-written code, your country). If the answer
   is "don't ship it compiled", stop: the Share package is exactly that.
2. **Check the name "Project Wrath"** as above (the searches are yours or a lawyer's).
3. ~~Fill in the public identity's noreply address~~ (done in step 111: decided point 8).
4. ~~Choose where problems are reported~~ (done in step 111: decided point 9).
5. **Create the public repository and the toolkit's fork on GitHub** (empty; yours to do), put the repository's
   address into `.github/ISSUE_TEMPLATE/config.yml` (`OWNER/REPOSITORY`) and the toolkit fork's branch
   `projectwrath` from `toolkit/patches` (`toolkit/fetch_toolkit.ps1` shows how).
6. **Export the public tree**: `powershell -NoProfile -ExecutionPolicy Bypass -File twoc\tools\make_public_tree.ps1
   -Out C:\some\scratch\ProjectWrath` (the audit runs by itself and must pass with no warnings left), read it, then
   push that folder's one commit to the new repository yourself.
7. **Prepare the release**: `powershell -NoProfile -ExecutionPolicy Bypass -File
   twoc\tools\package\make_release.ps1 -Out C:\some\scratch\release` (a clean clone, the PGO build, the zip, the
   audit, `SHA256SUMS.txt`, the audio source zip, `RELEASE_NOTES.md`; about 30-60 minutes). Then make the packages
   again for your own devices too if you like (the command at the top of this file).
8. **Read the audit**: it runs by itself at the end of each package; for the Share package it must say
   `AUDIT PASSED` in green, with **no** yellow "NOT READY TO SHARE" warning (neither the copyright name nor
   ISSUES_URL). Run it again on any copy you are about to send:
   `powershell -NoProfile -ExecutionPolicy Bypass -File twoc\tools\package\package_audit.ps1 -Dir "<the folder>"`.
9. **Try it as a stranger would, on a clean PC** (or a new user account): unzip `ProjectWrath-<version>-win64.zip`,
   check its SHA-256 against `SHA256SUMS.txt`, copy the folder to another PC (or another user account),
   read `README.txt` first, double-click `ProjectWrath.exe`, set it up from your disc image, play a minute, open the
   F1 menu (About: Copy build info, paste it somewhere and read it), quit.
10. **Check `Source\` is there** (the audio library's source zip) and that `LICENSE.txt` and
   `THIRD_PARTY_LICENSES.txt` are next to the exe.
11. **Publish the GitHub Release** with the zip, the audio source zip, `SHA256SUMS.txt` and `RELEASE_NOTES.md` (yours
   to do; keep the private repository as your record). Keep a copy of exactly what you gave (and the commit it was
   made from: `PACKAGE.txt` names it), so you can answer for it later, and keep the source zip available for as
   long as you share the program.

Nothing in these steps is published or uploaded by any script here: the last step is yours.
