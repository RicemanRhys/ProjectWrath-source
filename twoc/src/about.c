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
#include "pads.h"

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
    snprintf(a->build, sizeof(a->build), "twoc %s (%s)%s, xboxrecomp %s; linked %s", TWOC_BUILD_COMMIT,
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
    windows_line(a);
    snprintf(a->files, sizeof(a->files), "%s", twoc_game_files_result());
    if (!logged++)
        fprintf(stderr, "[ABOUT] build %s; link-time %s; SDL %s\n[ABOUT] graphics %s, driver %s; display %s\n"
                "[ABOUT] graphics card %s; the display's card %s\n[ABOUT] %s; game files %s\n", a->build, a->ltcg,
                a->sdl, a->gpu, a->driver, a->display, a->gpu_choice, a->gpu_display, a->windows, a->files);
}
