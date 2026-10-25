/**
 * The F1 menu's About tab (step 93, notes 18.96). See about.h.
 */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL_version.h>

#include "about.h"
#include "build_info.h"
#include "gamefiles.h"
#include "gpu.h"
#include "cpu_info.h"
#include "pads.h"
#include "setup.h"
#include <shellapi.h>

/* When the exe was linked: its PE header's time stamp, in local time. */
static void link_time(char *buf, size_t cap)
{
    const BYTE *base = (const BYTE *)GetModuleHandleA(NULL);
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
    ULONGLONG t = (ULONGLONG)nt->FileHeader.TimeDateStamp * 10000000ULL + 116444736000000000ULL;
    FILETIME ft, lt;
    SYSTEMTIME st;

    ft.dwLowDateTime = (DWORD)t;
    ft.dwHighDateTime = (DWORD)(t >> 32);
    if (FileTimeToLocalFileTime(&ft, &lt) && FileTimeToSystemTime(&lt, &st))
        snprintf(buf, cap, "%04u-%02u-%02u %02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    else
        snprintf(buf, cap, "unknown");
}

static void gpu_lines(twoc_about_t *a, void *dev)
{
    IDXGIDevice *xd = NULL;
    IDXGIAdapter *ad = NULL;
    DXGI_ADAPTER_DESC d;
    LARGE_INTEGER umd;
    char name[128];

    snprintf(a->gpu, sizeof(a->gpu), "not known");
    snprintf(a->driver, sizeof(a->driver), "not known");
    if (!dev || FAILED(ID3D11Device_QueryInterface((ID3D11Device *)dev, &IID_IDXGIDevice, (void **)&xd)))
        return;
    if (SUCCEEDED(IDXGIDevice_GetAdapter(xd, &ad)) && SUCCEEDED(IDXGIAdapter_GetDesc(ad, &d))) {
        WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof(name), NULL, NULL);
        snprintf(a->gpu, sizeof(a->gpu), "%s, %.1f GB of its own memory (vendor %04X, device %04X)", name,
                 d.DedicatedVideoMemory / 1073741824.0, d.VendorId, d.DeviceId);
        /* the user-mode driver's version, a.b.c.d as Device Manager shows it */
        if (SUCCEEDED(IDXGIAdapter_CheckInterfaceSupport(ad, &IID_IDXGIDevice, &umd))) {
            unsigned v1 = HIWORD(umd.HighPart), v2 = LOWORD(umd.HighPart), v3 = HIWORD(umd.LowPart),
                     v4 = LOWORD(umd.LowPart);
            int n = snprintf(a->driver, sizeof(a->driver), "%u.%u.%u.%u", v1, v2, v3, v4);
            if (d.VendorId == 0x10DE)   /* NVIDIA's number: the last five digits, 32.0.15.6636 -> 566.36 */
                snprintf(a->driver + n, sizeof(a->driver) - n, " (NVIDIA %u.%02u)", ((v3 % 10) * 10000 + v4) / 100,
                         ((v3 % 10) * 10000 + v4) % 100);
        }
    }
    if (ad)
        IDXGIAdapter_Release(ad);
    IDXGIDevice_Release(xd);
}

static void display_line(twoc_about_t *a, HWND hwnd, double hz)
{
    typedef UINT(WINAPI * dpi_t)(HWND);
    dpi_t dpi_for = (dpi_t)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow");
    MONITORINFOEXA mi;
    DEVMODEA dm;
    int n;

    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    memset(&dm, 0, sizeof(dm));
    dm.dmSize = sizeof(dm);
    if (!GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), (MONITORINFO *)&mi) ||
        !EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
        snprintf(a->display, sizeof(a->display), "not known");
        return;
    }
    n = snprintf(a->display, sizeof(a->display), "%lux%lu at %lu Hz", dm.dmPelsWidth, dm.dmPelsHeight,
                 dm.dmDisplayFrequency);
    if (hz > 0)
        n += snprintf(a->display + n, sizeof(a->display) - n, " (%.2f Hz measured)", hz);
    if (dpi_for && hwnd)
        n += snprintf(a->display + n, sizeof(a->display) - n, ", scaling %u%%", dpi_for(hwnd) * 100 / 96);
    if (mi.dwFlags & MONITORINFOF_PRIMARY)
        snprintf(a->display + n, sizeof(a->display) - n, ", the main display");
}

static void reg_text(HKEY k, const char *name, char *buf, DWORD cap)
{
    DWORD type, size = cap;

    buf[0] = 0;
    if (RegQueryValueExA(k, name, NULL, &type, (BYTE *)buf, &size) != ERROR_SUCCESS || type != REG_SZ)
        buf[0] = 0;
    buf[cap - 1] = 0;
}

static void windows_line(twoc_about_t *a)
{
    typedef LONG(WINAPI * rtl_t)(OSVERSIONINFOW *);
    rtl_t rtl = (rtl_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
    OSVERSIONINFOW v;
    char product[96] = "Windows", disp[32] = "", *p;
    DWORD ubr = 0, size = sizeof(ubr);
    HKEY k;

    memset(&v, 0, sizeof(v));
    v.dwOSVersionInfoSize = sizeof(v);
    if (!rtl || rtl(&v) != 0) {
        snprintf(a->windows, sizeof(a->windows), "not known");
        return;
    }
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &k) ==
        ERROR_SUCCESS) {
        reg_text(k, "ProductName", product, sizeof(product));
        reg_text(k, "DisplayVersion", disp, sizeof(disp));
        RegQueryValueExA(k, "UBR", NULL, NULL, (BYTE *)&ubr, &size);
        RegCloseKey(k);
    }
    if (!product[0])
        snprintf(product, sizeof(product), "Windows");
    if (v.dwBuildNumber >= 22000 && (p = strstr(product, "Windows 10")) != NULL)
        p[9] = '1';                     /* Windows 11 still names itself 10 there */
    snprintf(a->windows, sizeof(a->windows), "%s%s%s, build %lu.%lu.%lu.%lu", product, disp[0] ? " " : "", disp,
             v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber, ubr);
}

void twoc_about_collect(twoc_about_t *a, void *dev, HWND hwnd, double display_hz)
{
    static int logged;
    char linked[32];

    link_time(linked, sizeof(linked));
    snprintf(a->build, sizeof(a->build), "%s (%s)%s, xboxrecomp %s; linked %s", TWOC_BUILD_COMMIT,
             TWOC_BUILD_COMMIT_DATE, TWOC_BUILD_DIRTY ? " with changes not committed" : "", TWOC_BUILD_XBOXRECOMP,
             linked);
    snprintf(a->ltcg, sizeof(a->ltcg), "%s", TWOC_BUILD_LTCG ? "on (link-time code generation)" : "off");
    if (pads_sdl_version()[0])
        snprintf(a->sdl, sizeof(a->sdl), "%s", pads_sdl_version());
    else
        snprintf(a->sdl, sizeof(a->sdl), "%d.%d.%d (built in; not running: Controller input is XInput only)",
                 SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
    gpu_lines(a, dev);
    twoc_gpu_about(dev, hwnd, a->gpu_choice, sizeof(a->gpu_choice), a->gpu_display, sizeof(a->gpu_display));
    display_line(a, hwnd, display_hz);
    snprintf(a->cpu, sizeof(a->cpu), "%s", twoc_cpu_text());
    snprintf(a->cpu_power, sizeof(a->cpu_power), "%s", twoc_cpu_power_text());
    windows_line(a);
    snprintf(a->files, sizeof(a->files), "%s", twoc_game_files_result());
    if (!logged++)
        fprintf(stderr, "[ABOUT] build %s; link-time %s; SDL %s\n[ABOUT] graphics %s, driver %s; display %s\n"
                "[ABOUT] graphics card %s; the display's card %s\n[ABOUT] processor %s; power throttling %s\n"
                "[ABOUT] %s; game files %s\n", a->build, a->ltcg, a->sdl, a->gpu, a->driver, a->display,
                a->gpu_choice, a->gpu_display, a->cpu, a->cpu_power, a->windows, a->files);
}

/* ── Step 109 (notes 18.112): problem reports ─────────────────────────── */

static int word_ch(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
           (unsigned char)c >= 0x80;
}

static char *find(char *s, const char *what, int ci)
{
    size_t n = strlen(what);
    if (!ci)
        return strstr(s, what);
    for (; *s; s++)
        if (_strnicmp(s, what, n) == 0)
            return s;
    return NULL;
}

/* Every `what` in `s` (`cap` bytes) replaced by `with`; `ci` ignoring case, `word` only as a whole word. A `what`
 * shorter than 2 characters is left alone. Returns how many. */
static int scrub_one(char *s, size_t cap, const char *what, const char *with, int ci, int word)
{
    size_t wl = strlen(what), rl = strlen(with);
    char *p = s;
    int n = 0;

    if (wl < 2)
        return 0;
    while ((p = find(p, what, ci)) != NULL) {
        size_t len = strlen(s);
        if (word && ((p > s && word_ch(p[-1])) || word_ch(p[wl]))) {
            p += wl;
            continue;
        }
        if (len - wl + rl + 1 > cap)
            break;
        memmove(p + rl, p + wl, len - (size_t)(p - s) - wl + 1);
        memcpy(p, with, rl);
        p += rl;
        n++;
    }
    return n;
}

/* A variable or a Windows name as UTF-8 ("" if none). */
static void env8(const wchar_t *name, char *out, size_t cap)
{
    wchar_t w[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(name, w, MAX_PATH * 2);
    out[0] = 0;
    if (n && n < MAX_PATH * 2)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL);
}

/* No user name, PC name or folder in `s`: the data folder, the exe's, the home folder and the other profile
 * folders as <...>, \Users\NAME as \Users\<user>, the user's name and the PC's as <user> and <pc>, and any other
 * path left (a drive letter or \\server onwards, to the next separator) as <path>. As Collect logs scrubs. */
static int scrub(char *s, size_t cap)
{
    static const struct { const wchar_t *var; const char *as; } k_dirs[] = {
        { L"LOCALAPPDATA", "<localappdata>" }, { L"APPDATA", "<appdata>" }, { L"TEMP", "<temp>" },
        { L"USERPROFILE", "<user-home>" },
    };
    char v[MAX_PATH * 6], users[MAX_PATH * 6 + 16];
    wchar_t w[MAX_PATH * 2], *slash;
    DWORD n;
    size_t i;
    int hits = 0;
    char *p;

    hits += scrub_one(s, cap, twoc_data_dir_utf8(), "<game folder>", 1, 0);
    n = GetModuleFileNameW(NULL, w, MAX_PATH * 2);
    if (n && n < MAX_PATH * 2 && (slash = wcsrchr(w, L'\\')) != NULL) {
        *slash = 0;
        WideCharToMultiByte(CP_UTF8, 0, w, -1, v, (int)sizeof(v), NULL, NULL);
        hits += scrub_one(s, cap, v, "<exe folder>", 1, 0);
    }
    for (i = 0; i < sizeof(k_dirs) / sizeof(k_dirs[0]); i++) {
        env8(k_dirs[i].var, v, sizeof(v));
        hits += scrub_one(s, cap, v, k_dirs[i].as, 1, 0);
    }
    env8(L"USERNAME", v, sizeof(v));
    if (v[0]) {
        snprintf(users, sizeof(users), "\\Users\\%s", v);
        hits += scrub_one(s, cap, users, "\\Users\\<user>", 1, 0);
        snprintf(users, sizeof(users), "/Users/%s", v);
        hits += scrub_one(s, cap, users, "/Users/<user>", 1, 0);
        hits += scrub_one(s, cap, v, "<user>", 0, 1);
    }
    env8(L"COMPUTERNAME", v, sizeof(v));
    hits += scrub_one(s, cap, v, "<pc>", 1, 1);
    n = MAX_PATH;
    if (GetComputerNameExW(ComputerNameDnsHostname, w, &n)) {
        WideCharToMultiByte(CP_UTF8, 0, w, -1, v, (int)sizeof(v), NULL, NULL);
        hits += scrub_one(s, cap, v, "<pc>", 1, 1);
    }
    for (p = s; *p; p++) {              /* any path left: C:\... or \\server\... to the next separator */
        int drive = ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':' &&
                    (p[2] == '\\' || p[2] == '/') && (p == s || !word_ch(p[-1]));
        int unc = p[0] == '\\' && p[1] == '\\' && word_ch(p[2]);
        if (drive || unc) {
            char *e = p;
            while (*e && *e != ',' && *e != ';' && *e != ')' && *e != '"' && *e != '\r' && *e != '\n')
                e++;
            if ((size_t)(e - p) > 6) {
                memmove(p + 6, e, strlen(e) + 1);
                memcpy(p, "<path>", 6);
                hits++;
            }
        }
    }
    return hits;
}

const char *twoc_issues_url(void)
{
    return strncmp(TWOC_ISSUES_URL, "https://", 8) == 0 ? TWOC_ISSUES_URL : "";
}

int twoc_about_copy(const twoc_about_t *a, const char *picture, const char *framerate, const char *changed,
                    HWND hwnd, char *out, size_t cap)
{
    static char t[8192];
    double mhz = 0;
    unsigned long hz = 0;
    const char *m;
    int n = 0, lines = 0, hits, i;
    wchar_t *w;
    HGLOBAL g;
    size_t len;

    if ((m = strstr(a->display, " at ")) != NULL)
        sscanf(m, " at %lu Hz", &hz);
    if ((m = strstr(a->display, "(")) != NULL)
        sscanf(m, "(%lf Hz measured)", &mhz);
    n += snprintf(t + n, sizeof(t) - n, "Project Wrath build info (F1 menu, About, Copy build info)\r\n");
    n += snprintf(t + n, sizeof(t) - n, "Build: %s; link-time %s\r\n", a->build, a->ltcg);
    n += snprintf(t + n, sizeof(t) - n, "Windows: %s\r\n", a->windows);
    n += snprintf(t + n, sizeof(t) - n, "Processor: %s; power throttling: %s\r\n", a->cpu, a->cpu_power);
    n += snprintf(t + n, sizeof(t) - n, "Graphics card: %s\r\nDriver: %s\r\n", a->gpu, a->driver);
    n += snprintf(t + n, sizeof(t) - n, "Graphics card chosen by: %s; the display's card: %s\r\n", a->gpu_choice,
                  a->gpu_display);
    n += snprintf(t + n, sizeof(t) - n, "Display: %s\r\n", a->display);
    if (hz)
        n += snprintf(t + n, sizeof(t) - n, mhz > 0 ? "Refresh rate: %lu Hz (%.2f Hz measured)\r\n"
                                                    : "Refresh rate: %lu Hz\r\n", hz, mhz);
    n += snprintf(t + n, sizeof(t) - n, "Picture: %s\r\n", picture && *picture ? picture : "not known");
    n += snprintf(t + n, sizeof(t) - n, "Frame rate setting: %s\r\n", framerate && *framerate ? framerate : "not known");
    n += snprintf(t + n, sizeof(t) - n, "Settings changed from the defaults: %s\r\n", changed && *changed ? changed
                                                                                                    : "none");
    n += snprintf(t + n, sizeof(t) - n, "Game files: %s\r\n", a->files);
    n += snprintf(t + n, sizeof(t) - n, "Controllers: SDL %s\r\n", a->sdl);
    (void)n;
    hits = scrub(t, sizeof(t));
    for (i = 0; t[i]; i++)
        lines += t[i] == '\n';
    if (out && cap)
        snprintf(out, cap, "%s", t);
    fprintf(stderr, "[ABOUT] Copy build info: %d lines, %d names or folders taken out\n", lines, hits);

    len = (size_t)MultiByteToWideChar(CP_UTF8, 0, t, -1, NULL, 0);
    if (!len || !OpenClipboard(hwnd))
        return -1;
    EmptyClipboard();
    g = GlobalAlloc(GMEM_MOVEABLE, len * sizeof(wchar_t));
    if (g && (w = (wchar_t *)GlobalLock(g)) != NULL) {
        MultiByteToWideChar(CP_UTF8, 0, t, -1, w, (int)len);
        GlobalUnlock(g);
        if (SetClipboardData(CF_UNICODETEXT, g))
            g = NULL;                   /* the clipboard owns it now */
    }
    CloseClipboard();
    if (g) {
        GlobalFree(g);
        return -1;
    }
    return lines;
}

int twoc_about_open_logs(void)
{
    wchar_t path[MAX_PATH * 2 + 16], arg[MAX_PATH * 2 + 32];
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD n = 0;
    INT_PTR r;

    if (h && h != INVALID_HANDLE_VALUE && GetFileType(h) == FILE_TYPE_DISK)
        n = GetFinalPathNameByHandleW(h, path, MAX_PATH * 2, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (n && n < MAX_PATH * 2) {        /* this run's log, selected */
        const wchar_t *p = wcsncmp(path, L"\\\\?\\UNC\\", 8) == 0 ? path + 6 : wcsncmp(path, L"\\\\?\\", 4) == 0 ? path + 4
                                                                                                             : path;
        if (p == path + 6)
            path[6] = L'\\';            /* \\?\UNC\server\x -> \\server\x */
        _snwprintf_s(arg, sizeof(arg) / sizeof(arg[0]), _TRUNCATE, L"/select,\"%s\"", p);
        r = (INT_PTR)ShellExecuteW(NULL, L"open", L"explorer.exe", arg, NULL, SW_SHOWNORMAL);
    } else
        r = (INT_PTR)ShellExecuteW(NULL, L"open", twoc_data_dir(), NULL, NULL, SW_SHOWNORMAL);
    fprintf(stderr, "[ABOUT] Open logs folder: %s\n", r > 32 ? "Explorer opened" : "Windows refused");
    return r > 32 ? 0 : -1;
}

int twoc_about_open_issues(void)
{
    wchar_t w[1024];
    INT_PTR r;

    if (!twoc_issues_url()[0] || !MultiByteToWideChar(CP_UTF8, 0, twoc_issues_url(), -1, w, 1024))
        return -1;
    r = (INT_PTR)ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL);
    fprintf(stderr, "[ABOUT] Report a problem: %s %s\n", twoc_issues_url(), r > 32 ? "opened in the browser"
                                                                                  : "not opened (Windows refused)");
    return r > 32 ? 0 : -1;
}
