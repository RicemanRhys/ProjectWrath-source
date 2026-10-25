/**
 * The names the exe shows and writes (step 109). See names.h.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "names.h"

static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static int g_dev;
static wchar_t g_exe_w[MAX_PATH] = L"ProjectWrath.exe";
static char g_exe[MAX_PATH * 3] = "ProjectWrath.exe";
static char g_stem[MAX_PATH * 3] = "ProjectWrath";
static char g_ini[MAX_PATH * 3 + 8] = "ProjectWrath.ini";

static BOOL CALLBACK init(PINIT_ONCE o, PVOID p, PVOID *c)
{
    wchar_t path[MAX_PATH * 2], *leaf, *dot;
    DWORD n = GetModuleFileNameW(NULL, path, sizeof(path) / sizeof(path[0]));

    (void)o;
    (void)p;
    (void)c;
    if (n > 0 && n < sizeof(path) / sizeof(path[0])) {
        leaf = wcsrchr(path, L'\\');
        leaf = leaf ? leaf + 1 : path;
        if (*leaf && wcslen(leaf) < MAX_PATH)
            wcscpy_s(g_exe_w, MAX_PATH, leaf);
    }
    WideCharToMultiByte(CP_UTF8, 0, g_exe_w, -1, g_exe, (int)sizeof(g_exe), NULL, NULL);
    snprintf(g_stem, sizeof(g_stem), "%s", g_exe);
    dot = wcsrchr(g_exe_w, L'.');
    if (dot && _wcsicmp(dot, L".exe") == 0) {
        char *d = strrchr(g_stem, '.');
        if (d)
            *d = 0;
    }
    g_dev = _stricmp(g_stem, "crash_twoc") == 0;
    snprintf(g_ini, sizeof(g_ini), "%s.ini", g_stem);
    return TRUE;
}

static void once(void)
{
    InitOnceExecuteOnce(&g_once, init, NULL, NULL);
}

int twoc_names_dev(void)
{
    once();
    return g_dev;
}

const char *twoc_app_name(void)
{
    return twoc_names_dev() ? "Crash TWOC" : "Project Wrath";
}

const wchar_t *twoc_app_name_w(void)
{
    return twoc_names_dev() ? L"Crash TWOC" : L"Project Wrath";
}

const char *twoc_exe_name(void)
{
    once();
    return g_exe;
}

const wchar_t *twoc_exe_name_w(void)
{
    once();
    return g_exe_w;
}

const char *twoc_exe_stem(void)
{
    once();
    return g_stem;
}

const char *twoc_ini_name(void)
{
    once();
    return g_ini;
}

const wchar_t *twoc_log_base_w(void)
{
    return twoc_names_dev() ? L"run_twoc" : L"ProjectWrath";
}

const wchar_t *twoc_data_folder_w(void)
{
    return twoc_names_dev() ? TWOC_OLD_DATA_FOLDER : L"ProjectWrath";
}

const char *twoc_window_title(void)
{
    return twoc_names_dev() ? "Crash TWOC - D3D bridge (host)" : "Project Wrath";
}
