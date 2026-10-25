/**
 * The names the exe shows and writes (step 109, notes 18.112): the project is
 * Project Wrath, and the packages ship this build's crash_twoc.exe copied as
 * ProjectWrath.exe, so what it calls itself follows the name it runs under:
 *
 *   - the ini and the mute marker are named after the running exe:
 *     <exe name>.ini and <exe name>.muted (ProjectWrath.ini beside
 *     ProjectWrath.exe, crash_twoc.ini beside crash_twoc.exe);
 *   - everything else (window and message-box titles, the About header, the
 *     log names, the data folder in %LOCALAPPDATA%, the restart history) is
 *     Project Wrath's, unless the exe is the developer's crash_twoc.exe: that
 *     one keeps the old names ("Crash TWOC - D3D bridge (host)", run_twoc.*.log,
 *     %LOCALAPPDATA%\CrashTWOC), which run_twoc.ps1 and the harness use.
 *
 * Worked out once, from GetModuleFileNameW, on the first call; safe from any
 * thread.
 */
#ifndef TWOC_NAMES_H
#define TWOC_NAMES_H

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1: the running exe is crash_twoc.exe (the developer's build), with the old names. */
int twoc_names_dev(void);
/* "Project Wrath" (or "Crash TWOC"): window and message-box titles, texts. */
const char *twoc_app_name(void);
const wchar_t *twoc_app_name_w(void);
/* The running exe's own file name, "ProjectWrath.exe" (UTF-8; wide), and without ".exe". */
const char *twoc_exe_name(void);
const wchar_t *twoc_exe_name_w(void);
const char *twoc_exe_stem(void);
/* The ini's file name, "<exe name>.ini", and the old one a renamed exe reads until it writes its own. */
const char *twoc_ini_name(void);
#define TWOC_OLD_INI_NAME "crash_twoc.ini"
#define TWOC_OLD_INI_NAME_W L"crash_twoc.ini"
/* The logs' base name when the exe starts by itself: "ProjectWrath" (ProjectWrath.err.log), or "run_twoc". */
const wchar_t *twoc_log_base_w(void);
/* The data folder's name in %LOCALAPPDATA%: "ProjectWrath", or "CrashTWOC". */
const wchar_t *twoc_data_folder_w(void);
#define TWOC_OLD_DATA_FOLDER L"CrashTWOC"
/* The game window's title: "Project Wrath", or "Crash TWOC - D3D bridge (host)". */
const char *twoc_window_title(void);

#ifdef __cplusplus
}
#endif

#endif
