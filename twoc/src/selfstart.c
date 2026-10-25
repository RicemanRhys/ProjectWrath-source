/**
 * Started by itself (step 107, notes 18.110). See selfstart.h.
 */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>

#include "selfstart.h"
#include "setup.h"
#include "settings.h"
#include "names.h"

static int g_self;

int twoc_self_started(void)
{
    return g_self;
}

/* The command line has `flag` (as its own argument). */
static int has_arg(const wchar_t *flag)
{
    int n = 0, i, r = 0;
    wchar_t **a = CommandLineToArgvW(GetCommandLineW(), &n);
    for (i = 1; a && i < n; i++)
        if (_wcsicmp(a[i], flag) == 0)
            r = 1;
    LocalFree(a);
    return r;
}

/* A standard handle a launcher or a command line already pointed somewhere (a file or a pipe). */
static int redirected(DWORD which)
{
    HANDLE h = GetStdHandle(which);
    DWORD t;
    if (h == NULL || h == INVALID_HANDLE_VALUE)
        return 0;
    t = GetFileType(h);
    return t == FILE_TYPE_DISK || t == FILE_TYPE_PIPE;
}

/* The last run's log `name` into logs\run_<time>.<kind>.log, as Play Project Wrath.bat does: the time the error log
 * (ProjectWrath.err.log, names.h) was last written (`by`), or the file's own when there is none. */
static void rotate(const wchar_t *name, const wchar_t *kind, const FILETIME *by)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    FILETIME lt;
    SYSTEMTIME t;
    wchar_t to[MAX_PATH];
    int k;

    if (!GetFileAttributesExW(name, GetFileExInfoStandard, &fa))
        return;
    CreateDirectoryW(L"logs", NULL);
    FileTimeToLocalFileTime(by ? by : &fa.ftLastWriteTime, &lt);
    FileTimeToSystemTime(&lt, &t);
    for (k = 1; k < 100; k++) {
        if (k == 1)
            _snwprintf_s(to, MAX_PATH, _TRUNCATE, L"logs\\run_%04u-%02u-%02u_%02u-%02u-%02u.%s.log", t.wYear, t.wMonth,
                         t.wDay, t.wHour, t.wMinute, t.wSecond, kind);
        else
            _snwprintf_s(to, MAX_PATH, _TRUNCATE, L"logs\\run_%04u-%02u-%02u_%02u-%02u-%02u_%d.%s.log", t.wYear,
                         t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, k, kind);
        if (MoveFileExW(name, to, 0) || GetLastError() != ERROR_ALREADY_EXISTS)
            return;
    }
}

/* A stream onto `path`; NUL when that can't be opened (a folder nobody can write to), so the stream always
 * exists: a failed freopen leaves it closed, and writing to it then ends the process. */
static void reopen(const wchar_t *path, FILE *f)
{
    if (!_wfreopen(path, L"w", f))
        _wfreopen(L"NUL", L"w", f);
}

/* The CRT's streams as the standard handles (restart.c hands them on). A stream that couldn't be opened is left
 * alone: _get_osfhandle of no file ends the process (an invalid parameter). */
static void to_handles(void)
{
    int o = _fileno(stdout), e = _fileno(stderr);
    if (o >= 0)
        SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(o));
    if (e >= 0)
        SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(e));
}

void twoc_self_start(void)
{
    static char power_log[MAX_PATH];
    static const struct { const char *var, *val; } k_env[] = {
        { "RECOMP_VBLANK", "1" }, { "RECOMP_AC97_READY", "1" }, { "RECOMP_D3D_BRIDGE", "1" },
        { "RECOMP_POWER_LOG", power_log },
    };
    int console = has_arg(L"--console"), out = redirected(STD_OUTPUT_HANDLE), err = redirected(STD_ERROR_HANDLE);
    size_t i;
    /* Step 109: the logs' names, ProjectWrath.err.log / .out.log / .power.log (run_twoc.* for crash_twoc.exe) */
    const wchar_t *lb = twoc_log_base_w();
    wchar_t err_log[64], out_log[64], pow_log[64];
    char err8[64], out8[64];

    _snwprintf_s(err_log, 64, _TRUNCATE, L"%s.err.log", lb);
    _snwprintf_s(out_log, 64, _TRUNCATE, L"%s.out.log", lb);
    _snwprintf_s(pow_log, 64, _TRUNCATE, L"%s.power.log", lb);
    WideCharToMultiByte(CP_UTF8, 0, err_log, -1, err8, sizeof(err8), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, out_log, -1, out8, sizeof(out8), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, pow_log, -1, power_log, sizeof(power_log), NULL, NULL);

    g_self = 1;
    if (console) {                      /* the console it was started from, or a new one */
        if (!AttachConsole(ATTACH_PARENT_PROCESS))
            AllocConsole();
        if (!out)
            reopen(L"CONOUT$", stdout);
        if (!err)
            reopen(L"CONOUT$", stderr);
        to_handles();
    } else if (!out || !err) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {     /* started from a console: say where the log is */
            HANDLE c = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (c != INVALID_HANDLE_VALUE) {
                wchar_t line[MAX_PATH * 2 + 160];
                DWORD put;
                _snwprintf_s(line, sizeof(line) / sizeof(line[0]), _TRUNCATE, L"\r\n%s is starting; its log "
                             L"is %s\\%s (--console shows it here instead).\r\n", twoc_app_name_w(), twoc_data_dir(),
                             err_log);
                WriteConsoleW(c, line, (DWORD)wcslen(line), &put, NULL);
                CloseHandle(c);
            }
            FreeConsole();
        }
        {
            WIN32_FILE_ATTRIBUTE_DATA fa;
            FILETIME by;
            int has = GetFileAttributesExW(err_log, GetFileExInfoStandard, &fa);
            by = fa.ftLastWriteTime;
            rotate(err_log, L"err", has ? &by : NULL);
            rotate(out_log, L"out", has ? &by : NULL);
            rotate(L"xbox_kernel.log", L"kernel", has ? &by : NULL);
            rotate(pow_log, L"power", has ? &by : NULL);
        }
        if (!out)
            reopen(out_log, stdout);
        if (!err)
            reopen(err_log, stderr);
        to_handles();
    }
    for (i = 0; i < sizeof(k_env) / sizeof(k_env[0]); i++)
        if (!getenv(k_env[i].var))
            _putenv_s(k_env[i].var, k_env[i].val);
    {
        char where[200];
        if (console)
            snprintf(where, sizeof(where), "to the console (--console)");
        else if (out && err)
            snprintf(where, sizeof(where), "where it was sent");
        else
            snprintf(where, sizeof(where), "to %s and %s (the last run's in logs\\)", err8, out8);
        fprintf(stderr, "[START] started by itself (no launcher): working folder %s%s; the launcher's switches set "
                "(RECOMP_VBLANK, RECOMP_AC97_READY, RECOMP_D3D_BRIDGE, RECOMP_POWER_LOG); output %s\n",
                twoc_data_dir_utf8(), twoc_data_dir_fallback() ? " (%LOCALAPPDATA%: the exe's folder can't be written to)"
                : " (the exe's folder)", where);
    }
    if (twoc_data_dir_note()[0])
        fprintf(stderr, "%s\n", twoc_data_dir_note());     /* step 109: the old data folder's copy */
    fflush(stderr);
}

/* The screen's shape, as the ini's widescreen word. */
static const char *aspect_word(void)
{
    int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
    double r = h > 0 ? (double)w / h : 16.0 / 9.0;
    return r >= 2.2 ? "21:9" : r >= 1.7 ? "1" : r >= 1.55 ? "16:10" : "0";
}

void twoc_first_ini(void)
{
    const char *l = getenv("TWOC_LAUNCHER"), *path, *asp;
    char t[3000];
    HANDLE h;
    DWORD put;

    if (!g_self && !(l && (_strnicmp(l, "Play Crash", 10) == 0 || _strnicmp(l, "Play Project Wrath", 18) == 0)))
        return;
    if (twoc_settings_ignored())
        return;
    path = twoc_settings_path();
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
        return;
    if (twoc_settings_old_path()[0])    /* step 109: an old crash_twoc.ini is read instead (settings.c) */
        return;
    asp = aspect_word();
    snprintf(t, sizeof(t),
             "# %s settings, written by the settings menu (F1 in the game window).\r\n"
             "# Written at the game's first start with the settings that suit most PCs (notes 18.110): the screen's own\r\n"
             "# size and shape in a window, frames drawn between the game's at the display's rate, the render thread\r\n"
             "# by core count, baked draws on (the default). Change them in the F1 menu; delete this file for the\r\n"
             "# game's own defaults (the Xbox's 4:3 picture at 480 lines, 60 frames a second).\r\n"
             "fullscreen = 0           # 1: borderless over the whole screen (Alt+Enter switches at any time)\r\n"
             "widescreen = %s           # the aspect ratio: 0 (4:3), 1 (16:9) or W:H (16:10, 21:9); this screen's\r\n"
             "render_size = native           # game (480 lines), WxH or native\r\n"
             "pacing = auto           # auto, vsync, limiter or limiter+vsync\r\n"
             "anisotropy = 4           # 1 (the game's filtering), 2, 4, 8 or 16\r\n"
             "antialiasing = off           # off, fxaa, smaa, msaa2x, msaa4x, msaa8x, or msaa2x+smaa, msaa4x+smaa, "
             "msaa8x+smaa\r\n"
             "ao = off           # off, ssao or gtao: ambient occlusion\r\n"
             "framerate = display           # 60 (the game's), display (interpolated frames at the display's rate) or "
             "a rate in Hz\r\n"
             "adaptive_framerate = 1           # 1: fewer interpolated frames while game frames run late\r\n"
             "render_thread = auto           # auto (on with 4 or more processor cores), on or off\r\n"
             "hud = edges           # centre (the game's 4:3 places) or edges: the HUD counters at the screen's edges\r\n"
             "fast_title = 1           # 1: the title animation runs fast, so the main menu opens sooner\r\n"
             "gpu = auto           # auto (Windows' choice; the game asks for the high-performance card)\r\n",
             twoc_app_name(), asp);
    h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[CFG] first start: %s couldn't be written (Windows error %lu): the game's defaults\n", path,
                (unsigned long)GetLastError());
        return;
    }
    WriteFile(h, t, (DWORD)strlen(t), &put, NULL);
    CloseHandle(h);
    fprintf(stderr, "[CFG] first start: %s written with the settings that suit most PCs (native size, widescreen %s, "
            "Match display, render thread Auto, baking on)\n", path, asp);
}
