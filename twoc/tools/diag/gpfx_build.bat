@echo off
rem Builds gpfx.exe (notes 18.75). Needs dsp56300 (github.com/mborgerson/dsp56300, MIT) built first:
rem   cargo build --release -p dsp56300-emu-ffi --no-default-features
rem Set DSP56300_SRC to the checkout and DSP56300_LIB to the folder holding dsp56300_emu_ffi.lib
rem (target\release, or CARGO_TARGET_DIR\release). Run from a VS x64 developer prompt.
if "%DSP56300_SRC%"=="" (echo set DSP56300_SRC & exit /b 1)
if "%DSP56300_LIB%"=="" set DSP56300_LIB=%DSP56300_SRC%\target\release
cl /nologo /O2 /std:c11 /I "%DSP56300_SRC%\crates\emu-ffi\include" "%~dp0gpfx.c" /Fe"%~dp0gpfx.exe" /Fo"%TEMP%\gpfx.obj" ^
  /link /LIBPATH:"%DSP56300_LIB%" dsp56300_emu_ffi.lib legacy_stdio_definitions.lib kernel32.lib ntdll.lib ^
  userenv.lib ws2_32.lib dbghelp.lib advapi32.lib bcrypt.lib
