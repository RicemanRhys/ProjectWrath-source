# Questions for a lawyer (or a free legal-advice service)

Project Wrath is an unofficial fan port of an original Xbox game (Crash Bandicoot: The Wrath of Cortex, USA release,
2001) to Windows, made by one person as a hobby, not for money. Nothing has been published yet. This page lists what
would be shared and the questions it raises, so someone qualified can answer them. **It decides nothing.**

**Where I live:** Australia. [TO BE CONFIRMED BY THE OWNER: which country's laws apply to me, to GitHub (USA) and to
players elsewhere; whether Australian law, US law or both matter here.]

## What would be distributed

1. **A public source repository (GitHub)** with my own hand-written code: the Windows "host" (graphics, sound
   output, controls, settings menu, the first-run setup, save backups), under the MIT licence. It contains **no**
   translated game code, **no** game files, **no** disc images. It does contain, because the program needs them:
   - hand-written replacements for about 50 of the game's functions and wrappers around others, written against the
     game's memory addresses (no game code copied into them);
   - tables of addresses in the game's program (where the game keeps its camera, its HUD, its D3D state);
   - a list of the disc's 2,267 file names with their sizes and SHA-256 checksums (no contents), used to check a
     player's disc;
   - possibly (not decided) 52 function addresses needed to regenerate the translated code.
2. **A prebuilt download (a zip on GitHub Releases)**: `ProjectWrath.exe`, which **contains the game's program
   translated from Xbox machine code into C and compiled for Windows** ("static recompilation"), plus my own code;
   `xemu_apu.dll`, an audio-chip emulator from the xemu project under the GNU LGPL 2.1 (its full source included in the
   zip). No textures, sounds, music, levels, videos, logos or other game assets; no disc image.
3. **The toolkit's changes**: the port is built on "xboxrecomp" by sp00nznet (MIT licence; its audio files LGPL from
   xemu). I made 41 changes to it (graphics and audio fixes), to be published as a fork or patch files.

## What the player supplies

Their own copy of the game: an image of the original Xbox disc (USA) or the files extracted from it. On first start
the program reads that image, checks every file against the checksum list, and copies the files into its own folder.
It downloads nothing and sends nothing.

## Facts about technical protection (investigated, stated as found)

- The setup reads only a **disc image or a folder**, never a physical disc. It reads the image's file system
  (XDVDFS), which is not encrypted, and the program file's certificate (title, region, version) to identify it.
- Physical Xbox discs use a security scheme that a normal PC DVD drive can't get past; making an image needs special
  hardware or a modified console. **The player does that outside this program**; the program contains nothing for it
  and never reads or uses the discs' security data.
- The game's program file (`default.xbe`) carries Microsoft's digital signature, which the Xbox checks before running
  it. **The port does not check that signature**: it loads the program's parts directly. The file's start address and
  import table address are scrambled with a publicly known constant; the loader unscrambles the import table address
  (as every Xbox tool does). Nothing is decrypted.
- The toolkit's stand-in for the Xbox system software contains **no Xbox keys** (its key slots are zeros) and a
  signature-check function that always says "valid". **This game uses neither**: of the Xbox's cryptographic
  functions it calls only the SHA-1 hash.
- The setup accepts only the USA release (by its certificate and checksums); that is an identity check, not a region
  lock being bypassed.

## Questions

1. **The translated code.** Is distributing an executable that contains the game's program, mechanically
   translated and recompiled, distributing a copy or adaptation of the game's copyrighted code? Does it matter that
   players must own the game and supply its files, or that no assets are included? Is there any exception (for
   example interoperability, format-shifting, private use) that covers it in Australia or the USA? If not, what
   alternatives are safer (source only, with each player regenerating the code from their own disc; or the program
   translating the player's disc on their own PC at first start)?
2. **The public source.** May the repository include the hand-written functions written against the game's addresses,
   the address tables, the disc's file names and checksums, and the 52 function addresses? Which, if any, should stay
   private?
3. **Protection measures.** Given the facts above, does any part of the program, or of the instructions to players,
   amount to circumventing a technological protection measure under Australian law or the US DMCA? Is telling players
   to "use your own disc image" a problem, given how images are made?
4. **The game's licence.** Does the game's end-user licence (on the disc, the manual or the console's terms) bind
   someone who makes or uses this port, and does it forbid reverse engineering in a way that matters here?
5. **The name and wording.** Is "Project Wrath" a problem given the game's title ("...The Wrath of Cortex") and the
   owners' trademarks? Is the wording "an unofficial fan port; not affiliated with, endorsed by or sponsored by the
   owners of the game" enough, and where should it appear? The README and program mention the game and its
   characters by name only to say which disc is needed and what a setting does.
6. **Code written with an AI assistant.** Much of the code was written with an AI coding assistant (Claude Code, by
   Anthropic) under my direction and testing. Who owns it; can I license it under MIT; do I need to say so; does it
   change any of the answers above?
7. **The toolkit and xemu.** Are my 41 changes to the MIT toolkit mine to license as MIT? Do the LGPL audio files,
   shipped as a DLL with their source, satisfy the LGPL as distributed?
8. **Takedown exposure.** If the game's owners object, what would they likely do (a DMCA notice to GitHub, a letter
   to me), what are my obligations, and what should I keep or delete? Could I be liable for what players do?
9. **Money.** The project is free and has no donations. Should it stay that way (no donations, no ads, no paid
   builds) to reduce risk?

## What I would do on a takedown notice

1. Keep the private repository as it is (it is my record of the work); don't delete history in a panic.
2. Don't argue in public: no posts, no issue threads about it.
3. Comply with GitHub's process (it disables the content); don't re-upload anything elsewhere.
4. Ask a lawyer before replying to the sender or filing any counter-notice.
5. Keep copies of the notice and every message, with dates.
