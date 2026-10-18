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
    # The power mode and the CPU's speed, logged from a thread of its own (step 91);
    # powrprof.dll and pdh.dll load at run time, so the exe imports nothing new
    src/power_log.c
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

# The recomp pipeline also generates a dispatch header and function declarations.
# These are included by the generated .c files:
#   src/recomp/gen/recomp_funcs.h    - extern declarations for all recompiled functions
#   src/recomp/recomp_dispatch.h     - binary search dispatch table
#   src/recomp/recomp_types.h        - register model, ICALL macros, MEM helpers

add_executable(${PROJECT_NAME} WIN32
    ${GAME_SOURCES}
    ${RECOMP_GEN_SOURCES}
)

# Link against the xboxrecomp umbrella target (all 6 runtime libraries)
target_link_libraries(${PROJECT_NAME} PRIVATE xboxrecomp SDL3::SDL3-static)

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
