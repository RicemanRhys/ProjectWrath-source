# xemu_apu.dll (step 108, notes 18.111): the Xbox audio processor (MCPX APU) emulation, built on its own.
#
# The sources are xboxrecomp's src/apu, unchanged: apu_core.c, apu_vp.c, apu_dsp.c and their headers (extracted
# from xemu, GNU LGPL 2.1 or later), apu_xaudio2.c (the output) and apu_mmio_hook.c (the register-access
# decoder), both xboxrecomp's own (MIT), plus xemu_apu_dll.c here (the export, MIT). The same flags as the static
# library xbox_apu that the game's exe linked before step 108 (xboxrecomp/src/apu/CMakeLists.txt), so the code
# is the same, and /Brepro so that a rebuild from the same source with the same compiler is byte for byte the
# same file.
#
# Two ways in:
#   - from twoc/CMakeLists.txt (add_subdirectory), XEMU_APU_SRC = xboxrecomp/src; the DLL lands beside
#     the game's exe (packaged as ProjectWrath.exe: step 109);
#   - on its own, from the source package (make_apu_source_package.ps1): cmake -S . -B build, then
#     cmake --build build --config Release; XEMU_APU_SRC = ./src. build.cmd does both.
cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # On its own: what twoc/CMakeLists.txt and xboxrecomp/CMakeLists.txt set for every target.
    project(xemu_apu C)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    if(MSVC)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        # The Visual C++ runtime linked static, as twoc builds it (step 108): the DLL needs no runtime DLL.
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
    set(XEMU_APU_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src" CACHE PATH "The folder holding apu/, nv2a/ and platform/")
endif()
if(NOT XEMU_APU_SRC)
    message(FATAL_ERROR "XEMU_APU_SRC: the folder holding apu/, nv2a/qemu_shim.h and platform/xbox_winnt.h")
endif()

add_library(xemu_apu SHARED
    ${XEMU_APU_SRC}/apu/apu_core.c
    ${XEMU_APU_SRC}/apu/apu_vp.c
    ${XEMU_APU_SRC}/apu/apu_dsp.c
    ${XEMU_APU_SRC}/apu/apu_mmio_hook.c
    ${XEMU_APU_SRC}/apu/apu_xaudio2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/xemu_apu_dll.c
)
target_include_directories(xemu_apu PRIVATE
    ${XEMU_APU_SRC}/apu
    ${XEMU_APU_SRC}/nv2a        # qemu_shim.h
    ${XEMU_APU_SRC}             # platform/xbox_winnt.h
    ${CMAKE_CURRENT_SOURCE_DIR} # xemu_apu_api.h
)
if(MSVC)
    target_compile_options(xemu_apu PRIVATE
        /W3
        /wd4244  # conversion warnings (float/double/int)
        /wd4146  # unary minus on unsigned
        /wd4267  # size_t to int conversion
        /Brepro  # no time stamps: the same source and compiler give the same file
    )
    target_link_options(xemu_apu PRIVATE /Brepro)
    target_compile_definitions(xemu_apu PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _USE_MATH_DEFINES
    )
endif()
target_link_libraries(xemu_apu PRIVATE kernel32 winmm xaudio2_8 ole32)
