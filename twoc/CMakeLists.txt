cmake_minimum_required(VERSION 3.20)

# ============================================================================
# Crash Bandicoot: The Wrath of Cortex (Xbox, NTSC-U) - Static Recompilation
#
# Copy this template to your game project root and customize:
#   1. Change the project name below
#   2. Set XBOXRECOMP_DIR to where you cloned xboxrecomp
#   3. Adjust source file lists as you add game-specific code
#   4. Add any game-specific compile definitions or link libraries
# ============================================================================

project(crash_twoc C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# C++ only for the settings overlay (Dear ImGui) and the audio session (COM); notes 18.57
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Link-time code generation in Release (notes 18.87): every target below (the
# game's recompiled code, the mirror, xboxrecomp's libraries, SDL) compiled
# with /GL and linked with /LTCG. ~7% off the game thread at cast_bug's start,
# its own recompiled code ~14%; /fp stays precise, so the floating point is
# the same. -DTWOC_LTCG=OFF builds without it.
option(TWOC_LTCG "Link-time code generation in Release builds" ON)
if(MSVC AND TWOC_LTCG)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# The Visual C++ runtime linked static (/MT; step 108, notes 18.111): every target below (the exe, xboxrecomp's
# libraries, SDL, xemu_apu.dll) takes the runtime into itself, so no Microsoft runtime DLL (vcruntime140*.dll,
# msvcp140*.dll) ships beside the game and players need no Visual C++ Redistributable. Set before any target is
# made (FetchContent and add_subdirectory inherit it). -DTWOC_STATIC_CRT=OFF builds with the DLL runtime (/MD).
option(TWOC_STATIC_CRT "Link the Visual C++ runtime statically (/MT)" ON)
if(MSVC AND TWOC_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Profile-guided optimisation (step 95, notes 18.98), for make_package.ps1
# -Pgo in a build folder of its own: TWOC_PGO=GEN links the exe instrumented
# (/GENPROFILE, its counts database TWOC_PGO_PGD), the training runs write
# .pgc files beside that, and TWOC_PGO=USE links it again optimised by them
# (/USEPROFILE). Needs link-time code generation. /fp stays precise. Empty
# (the default): an ordinary build.
set(TWOC_PGO "" CACHE STRING "Profile-guided optimisation: empty, GEN (instrumented) or USE")
set(TWOC_PGO_PGD "" CACHE FILEPATH "The PGO counts database (.pgd) for TWOC_PGO")
if(TWOC_PGO AND NOT (MSVC AND TWOC_LTCG))
    message(FATAL_ERROR "TWOC_PGO needs MSVC and TWOC_LTCG")
endif()

# Common definitions
add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# ── xboxrecomp toolkit ──────────────────────────────────────────
#
# Option A: xboxrecomp as a subdirectory (simplest)
#   Place xboxrecomp/ next to your game project and use a relative path.
#
# Option B: find_package (if you install xboxrecomp system-wide)
#   find_package(xboxrecomp REQUIRED)
#
# Adjust this path to point to your xboxrecomp clone:
set(XBOXRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../xboxrecomp" CACHE PATH
    "Path to the xboxrecomp toolkit root directory")

# Add xboxrecomp as a subdirectory -- this builds all 6 runtime libraries
# and provides the 'xboxrecomp' umbrella target to link against.
add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)

# ── SDL3, for every controller the PC has (notes 18.73) ──────────
#
# The release tarball, fetched at configure time into the build folder and
# built static with only what the gamepad API needs. 3.4.18 has the 2026
# Steam Controller outside Steam (its HIDAPI driver, rumble fixed in 3.4.10,
# SDL_GAMEPAD_TYPE_STEAM in 3.4.18). Offline: -DFETCHCONTENT_SOURCE_DIR_SDL3=
# <an unpacked SDL3-3.4.18>.
include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
foreach(_sdl_off AUDIO VIDEO GPU RENDER CAMERA POWER DIALOG TRAY)
    set(SDL_${_sdl_off} OFF CACHE BOOL "" FORCE)
endforeach()
foreach(_sdl_on JOYSTICK HAPTIC HIDAPI SENSOR)
    set(SDL_${_sdl_on} ON CACHE BOOL "" FORCE)
endforeach()
FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(SDL3)
# Step 107 (notes 18.110): no path of the PC it was built on in the exe (package_audit.ps1 checks). SDL's GPU
# code names its own source files through __FILE__; MSVC's /d1trimfile takes the repo's folder off their front.
if(MSVC AND TARGET SDL3-static)
    get_filename_component(_twoc_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    file(TO_NATIVE_PATH "${_twoc_root}/" _twoc_root_native)
    target_compile_options(SDL3-static PRIVATE "/d1trimfile:${_twoc_root_native}")
endif()

# ── Recompiled game executable ──────────────────────────────────

# Collect generated recompiled code.
# The recomp pipeline outputs files into src/recomp/gen/.
# With --split 1000, you get gen_0000.c, gen_0001.c, etc.
# CONFIGURE_DEPENDS so re-running the recompiler picks up added/removed files
# without a manual re-configure. A plain GLOB is evaluated once at configure
# time, so a newly generated file (an extra chunk, recomp_stubs_unresolved.c)
# is silently left out of the build and shows up as unresolved externals.
file(GLOB RECOMP_GEN_SOURCES CONFIGURE_DEPENDS "src/recomp/gen/*.c")

# Core game sources
set(GAME_SOURCES
    src/main.c
    src/recomp_manual.c
    src/d3d_bridge.c
    src/d3d_host.c
    # Player settings, the settings overlay and the game's volume (notes 18.57)
    src/settings.c
    src/controls.c
    # Every controller through SDL3 next to XInput (notes 18.73)
    src/pads.c
    src/overlay.cpp
    src/host_audio.cpp
    # Anti-aliasing: FXAA and SMAA at the present (notes 18.61)
    src/aa.c
    # Ambient occlusion: SSAO and XeGTAO's GTAO after the world's opaque draws (notes 18.95)
    src/ao.c
    # The save's backups and the game files' check at the start (notes 18.86)
    src/saves.c
    src/gamefiles.c
    # The first-run setup: the disc image reader, the manifest check and the safe extraction, its screen (notes 18.110)
    src/xdvdfs.c
    src/setup.c
    src/setup_ui.cpp
    # Started by itself (no launcher): the launcher's switches, the logs, the first run's ini; and the narrow
    # Windows functions in UTF-8 (the manifest), so a folder named in any language works (notes 18.110)
    src/selfstart.c
    src/crash_twoc.manifest
    # The power mode and the CPU's speed, logged from a thread of its own (step 91);
    # powrprof.dll and pdh.dll load at run time, so the exe imports nothing new
    src/power_log.c
    # Memory: a [MEM] line each 10 s, sampled on the power log's thread (step 99)
    src/mem_log.c
    # The F1 menu's About tab: the build, the graphics card and driver, the display, Windows (step 93)
    src/about.c
    # The emulated APU (from xemu, LGPL) is xemu_apu.dll beside the exe: loaded and checked here (step 108)
    src/apu_dll.c
    # The graphics cards: listed at the start, the one the device is made on, the display's (step 97)
    src/gpu.c
    # The processor (hybrid or not) and the process's power throttling, opted out (step 97)
    src/cpu_info.c
    # The F1 menu's Quit game and Restart now: a clean end, the launcher's code 75 or a start of its own (step 97)
    src/restart.c
    # Vulkan, stage 1: a probe behind RECOMP_VULKAN=probe (notes 18.89); volk loads
    # vulkan-1.dll at run time, so the exe imports nothing new
    src/vk_probe.c
    third_party/volk/volk.c
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_dx11.cpp
    # Add game-specific sources here, e.g.:
    # src/asset_loader.c
    # src/custom_renderer.c
)

# SMAA's shader source as a byte array for aa.c, which compiles it with
# D3DCompile at run time (notes 18.61). Written again only when it changes.
set(SMAA_HLSL "${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa/SMAA.hlsl")
set(TWOC_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(READ "${SMAA_HLSL}" _smaa_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _smaa_hex "${_smaa_hex}")
string(REGEX REPLACE "((0x..,){24})" "\\1\n" _smaa_hex "${_smaa_hex}")
file(WRITE "${TWOC_GENERATED_DIR}/smaa_hlsl.h.tmp"
     "/* Generated by CMakeLists.txt from third_party/smaa/SMAA.hlsl. */\nstatic const unsigned char g_smaa_hlsl[] = {\n${_smaa_hex}0 };\n")
configure_file("${TWOC_GENERATED_DIR}/smaa_hlsl.h.tmp" "${TWOC_GENERATED_DIR}/smaa_hlsl.h" COPYONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SMAA_HLSL}")

# XeGTAO's two shader files the same way, for ao.c (notes 18.95).
set(_xegtao_out "/* Generated by CMakeLists.txt from third_party/xegtao. */\n")
foreach(_xe IN ITEMS h hlsli)
    set(_xe_file "${CMAKE_CURRENT_SOURCE_DIR}/third_party/xegtao/XeGTAO.${_xe}")
    file(READ "${_xe_file}" _xe_hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _xe_hex "${_xe_hex}")
    string(REGEX REPLACE "((0x..,){24})" "\\1\n" _xe_hex "${_xe_hex}")
    string(APPEND _xegtao_out "static const unsigned char g_xegtao_${_xe}[] = {\n${_xe_hex}0 };\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_xe_file}")
endforeach()
file(WRITE "${TWOC_GENERATED_DIR}/xegtao_hlsl.h.tmp" "${_xegtao_out}")
configure_file("${TWOC_GENERATED_DIR}/xegtao_hlsl.h.tmp" "${TWOC_GENERATED_DIR}/xegtao_hlsl.h" COPYONLY)

# The supported disc's manifest for the first-run setup (setup.c, notes 18.110): data/disc_usa.tsv (every file's
# path, size and SHA-256; no game data) as a C table. Written again only when it changes.
set(TWOC_DISC_TSV "${CMAKE_CURRENT_SOURCE_DIR}/data/disc_usa.tsv")
file(STRINGS "${TWOC_DISC_TSV}" _dm_lines ENCODING UTF-8)
set(_dm_meta "")
set(_dm_rows "")
set(_dm_dirs "")
foreach(_dm IN LISTS _dm_lines)
    string(REGEX REPLACE "\r$" "" _dm "${_dm}")    # a checkout with CRLF line ends
    if(_dm MATCHES "^#! ([a-z_0-9]+) (.*)$")
        string(TOUPPER "${CMAKE_MATCH_1}" _dm_k)
        if(_dm_k STREQUAL "EMPTY_DIR")      # a folder of the disc with no file in it
            string(APPEND _dm_dirs "    \"${CMAKE_MATCH_2}\",\n")
        elseif(_dm_k STREQUAL "TITLE" OR _dm_k STREQUAL "XBE_SHA256")
            string(APPEND _dm_meta "#define DM_${_dm_k} \"${CMAKE_MATCH_2}\"\n")
        elseif(_dm_k STREQUAL "TITLE_ID" OR _dm_k STREQUAL "REGION" OR _dm_k STREQUAL "VERSION")
            string(APPEND _dm_meta "#define DM_${_dm_k} 0x${CMAKE_MATCH_2}u\n")
        else()
            string(APPEND _dm_meta "#define DM_${_dm_k} ${CMAKE_MATCH_2}ull\n")
        endif()
    elseif(_dm MATCHES "^([^\t#][^\t]*)\t([0-9]+)\t([0-9a-f]+)$")
        string(APPEND _dm_rows "    { \"${CMAKE_MATCH_1}\", ${CMAKE_MATCH_2}u, \"${CMAKE_MATCH_3}\" },\n")
    endif()
endforeach()
file(WRITE "${TWOC_GENERATED_DIR}/disc_manifest.h.tmp"
     "/* Generated by CMakeLists.txt from data/disc_usa.tsv. */\n${_dm_meta}"
     "typedef struct { const char *path; unsigned size; const char *sha256; } dm_file_t;\n"
     "static const dm_file_t k_dm_files[] = {\n${_dm_rows}};\n"
     "static const char *const k_dm_empty_dirs[] = {\n${_dm_dirs}    NULL\n};\n")
configure_file("${TWOC_GENERATED_DIR}/disc_manifest.h.tmp" "${TWOC_GENERATED_DIR}/disc_manifest.h" COPYONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${TWOC_DISC_TSV}")

# The recomp pipeline also generates a dispatch header and function declarations.
# These are included by the generated .c files:
#   src/recomp/gen/recomp_funcs.h    - extern declarations for all recompiled functions
#   src/recomp/recomp_dispatch.h     - binary search dispatch table
#   src/recomp/recomp_types.h        - register model, ICALL macros, MEM helpers

add_executable(${PROJECT_NAME} WIN32
    ${GAME_SOURCES}
    ${RECOMP_GEN_SOURCES}
)

# The commit and date the About tab shows (step 93): build/generated/build_info.h, made again at every
# build by tools/build_info.cmake and replaced only when it changes.
add_custom_target(twoc_build_info
    COMMAND ${CMAKE_COMMAND} -DREPO=${CMAKE_CURRENT_SOURCE_DIR}/.. -DXR=${XBOXRECOMP_DIR}
            -DOUT=${TWOC_GENERATED_DIR}/build_info.h -DLTCG=${TWOC_LTCG}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_info.cmake
    BYPRODUCTS ${TWOC_GENERATED_DIR}/build_info.h
    VERBATIM)
add_dependencies(${PROJECT_NAME} twoc_build_info)

# xboxrecomp's runtime libraries, each by name: the umbrella target 'xboxrecomp' less xbox_apu (step 108, notes
# 18.111: the APU, from xemu under the LGPL, is the DLL below, not linked into the exe) and xbox_dsound (a
# DirectSound stand-in on the APU's software mixer, which the game never used: it has its own DirectSound).
target_link_libraries(${PROJECT_NAME} PRIVATE
    xbox_kernel xbox_d3d8 xbox_input xbox_usb xbox_nv2a xbox_video SDL3::SDL3-static)
set_target_properties(xbox_apu xbox_dsound PROPERTIES EXCLUDE_FROM_ALL TRUE)

# xemu_apu.dll (step 108, notes 18.111): xboxrecomp's src/apu, unchanged, built as a DLL of its own (apu_dll/)
# beside crash_twoc.exe, which loads it at the start (src/apu_dll.c). Nothing links to it: the exe finds it by
# full path, so the build only has to make it first.
set(XEMU_APU_SRC "${XBOXRECOMP_DIR}/src")
add_subdirectory(apu_dll)
set_target_properties(xemu_apu PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_dependencies(${PROJECT_NAME} xemu_apu)

# Include paths for generated recomp headers
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/recomp
    ${XBOXRECOMP_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vulkan/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/volk
    ${CMAKE_CURRENT_SOURCE_DIR}/apu_dll
    ${XBOXRECOMP_DIR}/src/audio     # dsound_xbox.h (xbox_runtime.h), once xbox_dsound's own include path
    ${TWOC_GENERATED_DIR}
)
set_source_files_properties(src/vk_probe.c third_party/volk/volk.c PROPERTIES
    COMPILE_DEFINITIONS VK_USE_PLATFORM_WIN32_KHR)

# Windows SDK libraries needed at link time
target_link_libraries(${PROJECT_NAME} PRIVATE
    d3d11
    dxgi
    dxguid
    xinput
    winmm
    # SHA-256 of default.xbe in the game files' check (gamefiles.c)
    bcrypt
    # Lets the fault handler resolve the host RIP back to the generated
    # function's symbol, i.e. to the guest function that faulted.
    dbghelp
)

# MSVC-specific: large address aware, increase section limit for big generated files
if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE
        /LARGEADDRESSAWARE
    )
    if(TWOC_PGO STREQUAL "GEN")     # step 95: profile-guided optimisation (above)
        target_link_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:/GENPROFILE:PGD=${TWOC_PGO_PGD}>)
    elseif(TWOC_PGO STREQUAL "USE")
        target_link_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:/USEPROFILE:PGD=${TWOC_PGO_PGD}>)
    endif()
    # The generated code files can be very large. Increase object file section limit.
    # /MP: compile the five ~190K-line generated files in parallel.
    target_compile_options(${PROJECT_NAME} PRIVATE /bigobj /MP)
endif()

# ── Game-specific additions ─────────────────────────────────────
#
# Add any game-specific libraries, loaders, or custom renderers below.
# Examples:
#
# # Custom texture loader for your game's format
# target_sources(${PROJECT_NAME} PRIVATE src/texture_loader.c)
#
# # If your game uses RenderWare, you may need a RW bridge layer
# target_sources(${PROJECT_NAME} PRIVATE src/rw_bridge.c)
#
# # Additional compile definitions for debugging
# target_compile_definitions(${PROJECT_NAME} PRIVATE
#     ENABLE_ICALL_TRACE=1
#     VERBOSE_KERNEL_LOG=1
# )
