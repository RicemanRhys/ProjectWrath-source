# build_info.cmake (step 93, notes 18.96): run by CMakeLists.txt's twoc_build_info target at every build.
# Writes OUT (build/generated/build_info.h) with the commit the exe's sources come from (the newest commit
# touching twoc/src, twoc/CMakeLists.txt or twoc/seed_functions.json, as the package's PACKAGE.txt names it),
# its date, whether those files had changes not committed, and xboxrecomp's commit; the file is only
# replaced when that changes, so an unchanged tree rebuilds nothing. -DREPO=<repo root> -DXR=<xboxrecomp>
# -DOUT=<header> -DLTCG=<ON|OFF>.
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
     "#define TWOC_BUILD_LTCG ${_ltcg}\n")
file(COPY_FILE "${OUT}.tmp" "${OUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUT}.tmp")
