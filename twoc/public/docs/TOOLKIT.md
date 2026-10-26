# The toolkit (xboxrecomp)

Project Wrath is built on **xboxrecomp** by sp00nznet (https://github.com/sp00nznet/xboxrecomp): the Xbox kernel
replacement, the Direct3D 8 to Direct3D 11 translator, the NV2A vertex-program translator, the emulated audio chip and
the Python recompilation pipeline. It is MIT licensed (Copyright (c) 2026 sp00nz), except its APU files
(`src/apu/apu_core.c`, `apu_dsp.c`, `apu_vp.c`, `apu_regs.h`, `apu_state.h`, `apu_debug.h`, `fpconv.h`, and
`apu_shim.h` by choice), which come from the xemu emulator under the GNU LGPL 2.1 or later (its NOTICE file).

This project uses upstream's release v0.11.0 (the commit in `toolkit/PINNED.txt`) **plus local commits** (the D3D8
translator's fog, combiners, texture modes, vertex-program multiply and per-draw costs; the voice processor's
resampling, HRTF, 5.1 and gains; kernel counters and accessors). They contain no game content.

## Getting it

`powershell -File toolkit\fetch_toolkit.ps1` clones upstream at the pinned commit and applies `toolkit/patches/*.patch`
with `git am` (a branch named `projectwrath`), making `xboxrecomp\` beside `twoc\`, where `twoc/CMakeLists.txt` looks
for it (`XBOXRECOMP_DIR`).

The same two facts (the pinned commit and the patch set) also serve the other ways of carrying the changes:

- **A public fork** of sp00nznet/xboxrecomp with a `projectwrath` branch made exactly as above: the owner creates it on
  GitHub; then `git submodule add -b projectwrath <fork URL> xboxrecomp` can replace the script.
- **Upstream**: the generic fixes can be offered to upstream as pull requests, one topic at a time; once merged, the
  patch set shrinks.

## The licences when the game is shared

The released package carries the LGPL audio code as its own DLL, `xemu_apu.dll`, with its complete source and build
steps (`Source\xemu_apu-source_*.zip`, made by `twoc/tools/package/make_apu_source_package.ps1`), so a player can
rebuild and replace it. The MIT parts need their copyright notices: `THIRD_PARTY_LICENSES.txt`.
