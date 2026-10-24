"""third_party_licenses.py [--dxvk LICENSE] [--out FILE] -- step 93 (notes 18.96): writes THIRD_PARTY_LICENSES.txt
(default twoc/THIRD_PARTY_LICENSES.txt) from the real licence files of every third-party component built into
crash_twoc.exe or shipped beside it (the Legion Go package; the DXVK test folder). Each text is read from the file
the component itself ships; a missing file stops the script. The Visual C++ runtime's terms aren't in Visual
Studio's install folder, so its part quotes Microsoft's published redistribution list (fetched 2026-10-19).

  python twoc/tools/package/third_party_licenses.py
  --dxvk: DXVK's LICENSE (default Desktop/Crash/vulkan-tools/dxvk-3.1.1/LICENSE, the DXVK 3.1.1 release the DXVK
          test folder's d3d11.dll and dxgi.dll come from)
"""
import argparse, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
TWOC = os.path.dirname(os.path.dirname(HERE))
REPO = os.path.dirname(TWOC)
TP = os.path.join(TWOC, "third_party")
SDL = os.path.join(TWOC, "build", "_deps", "sdl3-src")
XR = os.path.join(REPO, "xboxrecomp")

ap = argparse.ArgumentParser()
ap.add_argument("--dxvk", default=os.path.join(os.path.expanduser("~"), "Desktop", "Crash", "vulkan-tools",
                                               "dxvk-3.1.1", "LICENSE"))
ap.add_argument("--out", default=os.path.join(TWOC, "THIRD_PARTY_LICENSES.txt"))
a = ap.parse_args()


def read(*p):
    path = os.path.join(*p)
    if not os.path.isfile(path):
        sys.exit("missing: " + path)
    return open(path, encoding="utf-8", errors="strict").read().replace("\r\n", "\n").strip("\n")


def between(text, start, end):
    i = text.index(start)
    j = text.index(end, i)
    return text[i:j].strip("\n")


RULE = "=" * 100
out = []


def part(title, what, licence, text):
    out.append(RULE)
    out.append(title)
    out.append(RULE)
    out.append(what.strip("\n"))
    out.append("")
    out.append("Licence: " + licence)
    out.append("")
    out.append(text)
    out.append("")


out.append("""Crash Bandicoot: The Wrath of Cortex, PC port (crash_twoc.exe) - third-party components
====================================================================================

Every third-party component built into crash_twoc.exe or shipped beside it, with its licence's name and its
licence text, copied from the licence file the component ships (twoc/tools/package/third_party_licenses.py
writes this file from them). None of them is relicensed: each keeps its own licence. This project's own code is
under the MIT licence (LICENSE.txt). The game's own data (game_files: default.xbe and Crashdat) is your copy of
the Xbox disc and is not covered here.

  Built into crash_twoc.exe
    1. Dear ImGui 1.92.9b ................ MIT (and its stb_truetype / stb_rect_pack / stb_textedit: MIT or
                                            public domain)
    2. SDL 3.4.18 ........................ zlib (with HIDAPI: BSD-style; SunPro libm notice)
    3. XeGTAO (Intel) .................... MIT
    4. SMAA .............................. MIT
    5. volk 1.4.350 ...................... MIT
    6. Vulkan-Headers 1.4.363 ............ Apache-2.0 or MIT (MIT used here)
    7. xboxrecomp ........................ MIT; its audio (APU) code from xemu, LGPL-2.1-or-later, is not
                                            built in: it is xemu_apu.dll (below)
  Shipped beside it
   (7) xemu_apu.dll (the audio library) . LGPL-2.1-or-later (xemu's APU files), with xboxrecomp's and this
                                            project's MIT glue; its source is in Source\\ (see "The audio
                                            library, xemu_apu.dll" below)
    8. The Visual C++ runtime DLLs (vcruntime140.dll, vcruntime140_1.dll) ... Microsoft Visual Studio 2022
                                            licence terms, "Distributable Code"
    9. DXVK 3.1.1 (the DXVK test folder only: d3d11.dll, dxgi.dll) ........ zlib/libpng

  Written here, no third-party code: FXAA (twoc/src/aa.c, written after Timothy Lottes' FXAA 3.11 description);
  the NVIDIA variable-refresh query (d3d_host.c; nvapi64.dll is the driver's own, loaded at run time; the call's
  ids are taken from NVIDIA's MIT-licensed nvapi headers, no header code is included).
""")

part("1. Dear ImGui 1.92.9b (twoc/third_party/imgui)",
     "https://github.com/ocornut/imgui, tag v1.92.9b. The settings menu (F1). Its imstb_*.h files are Sean Barrett's\n"
     "stb libraries, whose licence (MIT or public domain, your choice) follows ImGui's.",
     "MIT", read(TP, "imgui", "LICENSE.txt") + "\n\n--- imstb_truetype.h, imstb_rectpack.h, imstb_textedit.h (stb) ---\n\n" +
     between(read(TP, "imgui", "imstb_truetype.h"), "This software is available under 2 licenses",
             "------------------------------------------------------------------------------\n*/").strip())

sdl_libm = read(SDL, "src", "libm", "e_atan2.c")
part("2. SDL 3.4.18 (fetched by CMake, built static)",
     "https://libsdl.org, release-3.4.18. Every controller that isn't an Xbox pad (Controls tab). Built with its\n"
     "HIDAPI drivers (src/hidapi; HIDAPI offers three licences, SDL and this build use the BSD-style one) and its\n"
     "own maths functions (src/libm, SunPro's notice below); SDL_malloc is Doug Lea's dlmalloc, public domain.",
     "zlib (SDL); BSD-style (HIDAPI)",
     read(SDL, "LICENSE.txt") + "\n\n--- src/hidapi/LICENSE.txt ---\n\n" + read(SDL, "src", "hidapi", "LICENSE.txt") +
     "\n\n--- src/hidapi/LICENSE-bsd.txt ---\n\n" + read(SDL, "src", "hidapi", "LICENSE-bsd.txt") +
     "\n\n--- src/libm (the notice each file carries) ---\n\n" +
     between(sdl_libm, " * ====================================================", " */").strip())

part("3. XeGTAO (twoc/third_party/xegtao)",
     "https://github.com/GameTechDev/XeGTAO, commit a5b1686. Ambient occlusion: GTAO (and SSAO's depth MIPs and\n"
     "denoiser). XeGTAO.h and XeGTAO.hlsli, unchanged; twoc/src/ao.c's entry points follow its sample's vaGTAO.hlsl\n"
     "and port XeGTAO::GTAOUpdateConstants to C, under this same notice.",
     "MIT", read(TP, "xegtao", "LICENSE.txt"))

part("4. SMAA (twoc/third_party/smaa)",
     "https://github.com/iryoku/smaa, commit 71c806a. Anti-aliasing: SMAA. SMAA.hlsl, AreaTex.h, SearchTex.h, unchanged.",
     "MIT", read(TP, "smaa", "LICENSE.txt"))

part("5. volk 1.4.350 (twoc/third_party/volk)",
     "https://github.com/zeux/volk. The Vulkan probe's loader (RECOMP_VULKAN=probe). volk.c and volk.h, unchanged.",
     "MIT", read(TP, "volk", "LICENSE.md"))

part("6. Vulkan-Headers vulkan-sdk-1.4.363.0 (twoc/third_party/vulkan)",
     "https://github.com/KhronosGroup/Vulkan-Headers. The Vulkan probe's C headers, unchanged.",
     "Apache-2.0 or MIT; used under MIT", read(TP, "vulkan", "LICENSE.md") + "\n\n--- LICENSES/MIT.txt ---\n\n" +
     read(TP, "vulkan", "LICENSES", "MIT.txt"))

notice = read(XR, "NOTICE")
part("7. xboxrecomp (the toolkit the recompilation runs on), and xemu_apu.dll",
     "The Xbox kernel, D3D8 on D3D11 (xbox_d3d8), input and USB libraries, linked static into crash_twoc.exe: MIT.\n"
     "Its audio (APU) files extracted from xemu, LGPL-2.1-or-later, are built into xemu_apu.dll instead, with\n"
     "xboxrecomp's own XAudio2 output and register decoder (MIT): its NOTICE's sections on them, then the LGPL's text.",
     "MIT; the APU files (xemu_apu.dll) LGPL-2.1-or-later",
     read(XR, "LICENSE") + "\n\n--- NOTICE: " +
     between(notice, "LGPL-2.1-or-later — extracted from xemu", "Derived algorithms").strip() +
     "\n\n--- NOTICE: " + between(notice, "Derived algorithms", "Reference material").strip() +
     "\n\n--- LICENSES/LGPL-2.1.txt ---\n\n" + read(XR, "LICENSES", "LGPL-2.1.txt"))

out.append(RULE)
out.append("The audio library, xemu_apu.dll")
out.append(RULE)
out.append("""The game's sound runs on xboxrecomp's emulated MCPX APU: apu_core.c, apu_dsp.c, apu_vp.c and their headers,
code extracted from xemu (https://github.com/xemu-project/xemu) and licensed LGPL-2.1-or-later. Since step 108
it is not part of crash_twoc.exe: it is xemu_apu.dll, a library of its own beside the exe, built from those
files unchanged (with xboxrecomp's MIT output and register decoder and this project's MIT export). The exe loads
it at the start from its own folder and uses it only through a small C interface (xemu_apu_api.h, version 1);
nothing else of the exe is in it.

Its source: the zip in the Source folder of the package for other people (xemu_apu-source_*.zip; made by
twoc/tools/package/make_apu_source_package.ps1): the complete source of the DLL as built, including this
project's changes to xemu's files (xboxrecomp commits made for this project and not published upstream; each
as a patch in changes\\), the LGPL's text, a notice (xemu's attribution, each file's licence, what was changed)
and the build files.

To replace it with your own: change the source, run build.cmd (Visual Studio 2022 with C++), close the game and
copy the new xemu_apu.dll over the one next to crash_twoc.exe. The game's log names the DLL it loaded, with its
size and SHA-256. If your DLL offers another interface version, the game says so and doesn't start.
""")

part("8. The Visual C++ runtime DLLs (vcruntime140.dll, vcruntime140_1.dll; app-local in the package and the DXVK folder)",
     "Microsoft Visual C++ 2015-2022 runtime, from Visual Studio 2022's VC\\Redist\\MSVC\\14.44.35112\\x64\\\n"
     "Microsoft.VC143.CRT, unmodified. They are governed by the \"Distributable Code\" section of the Microsoft\n"
     "Software License Terms for Visual Studio 2022 (here Community; https://visualstudio.microsoft.com/\n"
     "license-terms/vs2022-ga-community/). Those terms are not installed with Visual Studio here; the redistribution\n"
     "list they refer to is Microsoft's published one, quoted below from https://learn.microsoft.com/en-us/\n"
     "visualstudio/releases/2022/redistribution (fetched 2026-10-19).",
     "Microsoft Software License Terms (Visual Studio 2022), Distributable Code",
     """This is the "Distributable List" or "REDIST.txt" that is referenced in the "Distributable Code" section of the
Microsoft Software License Terms for Visual Studio Enterprise 2022, Visual Studio Professional 2022, Visual
Studio Community 2022 ("the software"). If you have a validly licensed copy of such software, you may copy and
distribute with your program the unmodified form of the files listed below, subject to the License Terms for
the software.

Visual C++ Runtime Files
(the merge modules listed first are left out here; then:)
Subject to the License Terms for the software, you may copy and distribute with your program any of the files
within the following folder and its subfolders except as noted below. You may not modify these files.

- [VisualStudioFolder]\\VC\\redist

The same Visual C++ Runtime Redistributable package is available from Latest supported Visual C++
Redistributable downloads and Download Visual Studio Tools. Distribution of the Visual C++ Runtime
Redistributable package, merge modules, and individual binaries obtained from those pages is limited to
licensed Visual Studio users and is subject to its license terms.

Note: You may not distribute the contents of the following folders (please see List of Utilities and Build
Tools for Visual Studio 2022 for allowed usage for development/testing purposes):

- [VisualStudioFolder]VC\\Redist\\MSVC\\[version]\\debug_nonredist
- [VisualStudioFolder]\\VC\\Redist\\MSVC\\[version]\\onecore\\debug_nonredist""")

part("9. DXVK 3.1.1 (the DXVK test folder only: d3d11.dll and dxgi.dll, x64)",
     "https://github.com/doitsujin/dxvk, release v3.1.1, unmodified. Not in the Legion Go package; the DXVK test\n"
     "folder ships it with this same text as \"DXVK LICENSE.txt\".",
     "zlib/libpng", read(a.dxvk))

text = "\n".join(out).rstrip("\n") + "\n"
open(a.out, "w", encoding="utf-8", newline="\r\n").write(text)
print("%s: %d lines, %d bytes" % (a.out, text.count("\n"), os.path.getsize(a.out)))
