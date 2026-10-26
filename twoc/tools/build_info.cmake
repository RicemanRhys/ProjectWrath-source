# build_info.cmake (step 93, notes 18.96): run by CMakeLists.txt's twoc_build_info target at every build.
# Writes OUT (build/generated/build_info.h) with the commit the exe's sources come from (the newest commit
# touching twoc/src, twoc/CMakeLists.txt or twoc/seed_functions.json, as the package's PACKAGE.txt names it),
# its date, whether those files had changes not committed, and xboxrecomp's commit; the file is only
# replaced when that changes, so an unchanged tree rebuilds nothing. -DREPO=<repo root> -DXR=<xboxrecomp>
# -DOUT=<header> -DLTCG=<ON|OFF>. Step 110: -DVERSION_FILE=<twoc/VERSION.txt>, the release's version.
set(_paths twoc/src twoc/CMakeLists.txt twoc/seed_functions.json)
execute_process(COMMAND git -C "${REPO}" log -1 --format=%h|%cd --date=format:%Y-%m-%d -- ${_paths}
                OUTPUT_VARIABLE _log OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _r)
if(NOT _r EQUAL 0 OR _log STREQUAL "")
    set(_log "unknown|unknown")
endif()
string(REPLACE "|" ";" _log "${_log}")
list(GET _log 0 _commit)
list(GET _log 1 _date)
execute_process(COMMAND git -C "${REPO}" status --porcelain --untracked-files=no -- ${_paths}
                OUTPUT_VARIABLE _dirty OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(_dirty STREQUAL "")
    set(_dirty 0)
else()
    set(_dirty 1)
endif()
execute_process(COMMAND git -C "${XR}" log -1 --format=%h
                OUTPUT_VARIABLE _xr OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(_xr STREQUAL "")
    set(_xr "unknown")
endif()
# Step 109: the commit's date as numbers, for the exe's file version (src/version.rc): 2026.10.25.0.
if(_date MATCHES "^([0-9]+)-0?([0-9]+)-0?([0-9]+)$")
    set(_vy ${CMAKE_MATCH_1})
    set(_vm ${CMAKE_MATCH_2})
    set(_vd ${CMAKE_MATCH_3})
else()
    set(_vy 0)
    set(_vm 0)
    set(_vd 0)
endif()
if(_dirty)
    set(_vtext "${_commit} (${_date}, with changes not committed)")
else()
    set(_vtext "${_commit} (${_date})")
endif()
# Step 109: ISSUES_URL from twoc/ISSUES_URL.txt (-DISSUES=<file>), the one place it is written: "" while it is the
# placeholder or isn't a plain https:// address (no quotes, spaces or backslashes), so the About tab shows no link.
set(_issues "")
if(ISSUES AND EXISTS "${ISSUES}")
    file(STRINGS "${ISSUES}" _il REGEX "^[ \t]*ISSUES_URL[ \t]*=")
    if(_il)
        list(GET _il 0 _il)
        string(REGEX REPLACE "^[ \t]*ISSUES_URL[ \t]*=[ \t]*" "" _il "${_il}")
        string(STRIP "${_il}" _il)
        if(_il MATCHES "^https://[A-Za-z0-9._~:/?#@!$&'()*+,;=%-]+$" AND NOT _il MATCHES "\\[")
            set(_issues "${_il}")
        endif()
    endif()
endif()
# Step 110 (notes 18.113): the version, MAJOR.MINOR.PATCH, from VERSION.txt's first line that is one ("0.0.0" if none).
set(_ver "0.0.0")
if(VERSION_FILE AND EXISTS "${VERSION_FILE}")
    file(STRINGS "${VERSION_FILE}" _vl REGEX "^[ \t]*[0-9]+\\.[0-9]+\\.[0-9]+[ \t]*$")
    if(_vl)
        list(GET _vl 0 _ver)
        string(STRIP "${_ver}" _ver)
    endif()
endif()
string(REPLACE "." ";" _vparts "${_ver}")
list(GET _vparts 0 _vmaj)
list(GET _vparts 1 _vmin)
list(GET _vparts 2 _vpat)
if(LTCG)
    set(_ltcg 1)
else()
    set(_ltcg 0)
endif()
file(WRITE "${OUT}.tmp"
     "/* Generated at build time by twoc/tools/build_info.cmake. */\n"
     "#define TWOC_BUILD_COMMIT \"${_commit}\"\n"
     "#define TWOC_BUILD_COMMIT_DATE \"${_date}\"\n"
     "#define TWOC_BUILD_DIRTY ${_dirty}\n"
     "#define TWOC_BUILD_XBOXRECOMP \"${_xr}\"\n"
     "#define TWOC_BUILD_VER_Y ${_vy}\n"
     "#define TWOC_BUILD_VER_M ${_vm}\n"
     "#define TWOC_BUILD_VER_D ${_vd}\n"
     "#define TWOC_BUILD_VERSION_TEXT \"${_vtext}\"\n"
     "#define TWOC_VERSION \"${_ver}\"\n"
     "#define TWOC_VERSION_FULL \"${_ver}, ${_vtext}\"\n"
     "#define TWOC_VER_MAJOR ${_vmaj}\n"
     "#define TWOC_VER_MINOR ${_vmin}\n"
     "#define TWOC_VER_PATCH ${_vpat}\n"
     "#define TWOC_ISSUES_URL \"${_issues}\"\n"
     "#define TWOC_BUILD_LTCG ${_ltcg}\n")
file(COPY_FILE "${OUT}.tmp" "${OUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUT}.tmp")
