/**
 * The graphics card (step 97). See gpu.h.
 */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu.h"
#include "settings.h"

/* xbox_d3d8 (d3d8_internal.h, local): the adapter its next CreateDevice uses. */
extern void d3d8_SetPreferredAdapter(IDXGIAdapter *adapter);

static twoc_gpu_t g_gpu[TWOC_GPU_MAX];
static int g_n;
static char g_asked[128];               /* "" Automatic */
static char g_note[256];
static char g_used[128];                /* the card the device was made on */

int twoc_gpu_count(void) { return g_n; }
const twoc_gpu_t *twoc_gpu_at(int i) { return i >= 0 && i < g_n ? &g_gpu[i] : NULL; }
const char *twoc_gpu_asked(void) { return g_asked; }
const char *twoc_gpu_note(void) { return g_note; }
const char *twoc_gpu_in_use(void) { return g_used; }

void twoc_gpu_label(const twoc_gpu_t *g, char *buf, size_t cap)
{
    snprintf(buf, cap, "%s, %.1f GB", g->name, g->vram / 1073741824.0);
}

/* `s` without its outer spaces, into `out`. */
static void trimmed(const char *s, char *out, size_t cap)
{
    size_t n;

    while (*s == ' ' || *s == '\t')
        s++;
    snprintf(out, cap, "%s", s);
    n = strlen(out);
    while (n && (out[n - 1] == ' ' || out[n - 1] == '\t'))
        out[--n] = 0;
}

int twoc_gpu_find(const char *name)
{
    char want[128];
    int i;

    trimmed(name ? name : "", want, sizeof(want));
    for (i = 0; want[0] && i < g_n; i++)
        if (_stricmp(g_gpu[i].name, want) == 0)
            return i;
    return -1;
}

static int same_luid(LUID a, LUID b)
{
    return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
}

/* The list's entry for `luid`, or -1. */
static int by_luid(LUID luid)
{
    int i;

    for (i = 0; i < g_n; i++)
        if (same_luid(g_gpu[i].luid, luid))
            return i;
    return -1;
}

/* Each adapter's place in Windows' order for `pref` (DXGI 1.6; Windows 10 1803 on). */
static void rank(IDXGIFactory1 *f, DXGI_GPU_PREFERENCE pref)
{
    IDXGIFactory6 *f6 = NULL;
    IDXGIAdapter1 *a = NULL;
    DXGI_ADAPTER_DESC1 d;
    UINT i;
    int k;

    if (FAILED(IDXGIFactory1_QueryInterface(f, &IID_IDXGIFactory6, (void **)&f6)))
        return;
    for (i = 0; SUCCEEDED(IDXGIFactory6_EnumAdapterByGpuPreference(f6, i, pref, &IID_IDXGIAdapter1, (void **)&a));
         i++) {
        if (SUCCEEDED(IDXGIAdapter1_GetDesc1(a, &d)) && (k = by_luid(d.AdapterLuid)) >= 0) {
            if (pref == DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE)
                g_gpu[k].high_perf_rank = (int)i;
            else
                g_gpu[k].min_power_rank = (int)i;
        }
        IDXGIAdapter1_Release(a);
        a = NULL;
    }
    IDXGIFactory6_Release(f6);
}

void twoc_gpu_start(void)
{
    IDXGIFactory1 *f = NULL;
    IDXGIAdapter1 *a = NULL, *chosen = NULL;
    DXGI_ADAPTER_DESC1 d;
    const char *e = getenv("RECOMP_GPU");
    char want[128];
    UINT i;
    int k, hp_order = 0;

    g_n = 0;
    g_note[0] = 0;
    trimmed(e ? e : "", want, sizeof(want));
    if (_stricmp(want, "auto") == 0 || _stricmp(want, "automatic") == 0)
        want[0] = 0;
    snprintf(g_asked, sizeof(g_asked), "%s", want);
    if (FAILED(CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&f))) {
        fprintf(stderr, "[GPU] DXGI's adapters can't be listed (CreateDXGIFactory1 failed): Automatic%s\n",
                want[0] ? ", not the card asked for" : "");
        return;
    }
    for (i = 0; SUCCEEDED(IDXGIFactory1_EnumAdapters1(f, i, &a)); i++) {
        if (SUCCEEDED(IDXGIAdapter1_GetDesc1(a, &d))) {
            char name[128];
            WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof(name), NULL, NULL);
            if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                fprintf(stderr, "[GPU] adapter %u: %s (software, not offered)\n", i, name);
            } else if (g_n < TWOC_GPU_MAX) {
                twoc_gpu_t *g = &g_gpu[g_n++];
                trimmed(name, g->name, sizeof(g->name));
                g->vram = d.DedicatedVideoMemory;
                g->shared = d.SharedSystemMemory;
                g->luid = d.AdapterLuid;
                g->vendor = d.VendorId;
                g->device = d.DeviceId;
                g->high_perf_rank = g->min_power_rank = -1;
                if (want[0] && !chosen && _stricmp(g->name, want) == 0) {
                    chosen = a;
                    IDXGIAdapter1_AddRef(chosen);
                }
            }
        }
        IDXGIAdapter1_Release(a);
        a = NULL;
    }
    rank(f, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE);
    rank(f, DXGI_GPU_PREFERENCE_MINIMUM_POWER);
    for (k = 0; k < g_n; k++) {
        const twoc_gpu_t *g = &g_gpu[k];
        hp_order |= g->high_perf_rank >= 0;
        fprintf(stderr, "[GPU] graphics card %d: %s, %.1f GB of its own memory (%.1f GB shared), vendor %04X device"
                " %04X, LUID %08lX:%08lX%s%s\n", k, g->name, g->vram / 1073741824.0, g->shared / 1073741824.0,
                g->vendor, g->device, (unsigned long)g->luid.HighPart, (unsigned long)g->luid.LowPart,
                g_n > 1 && g->high_perf_rank == 0 ? "; Windows' high-performance card" : "",
                g_n > 1 && g->min_power_rank == 0 ? "; Windows' power-saving card" : "");
    }
    if (!g_n)
        fprintf(stderr, "[GPU] no hardware graphics card listed by DXGI\n");
    if (want[0] && chosen) {
        d3d8_SetPreferredAdapter((IDXGIAdapter *)chosen);
        fprintf(stderr, "[GPU] Graphics card: \"%s\" (gpu, crash_twoc.ini or RECOMP_GPU): the device is made on it\n",
                want);
    } else if (want[0]) {
        snprintf(g_note, sizeof(g_note), "The graphics card chosen before, \"%s\", isn't on this PC now: Automatic "
                 "is used.", want);
        fprintf(stderr, "[GPU] Graphics card: \"%s\" (gpu, crash_twoc.ini or RECOMP_GPU) isn't on this PC: Automatic"
                " instead\n", want);
    } else if (g_n <= 1) {
        fprintf(stderr, "[GPU] Graphics card: Automatic%s, with one graphics card: D3D11's default adapter, as "
                "before\n", twoc_settings_ignored() ? " (safe settings)" : "");
    } else {
        fprintf(stderr, "[GPU] Graphics card: Automatic%s: D3D11's default adapter, which Windows picks (its Graphics "
                "setting for this exe, else the driver's choice; the exe asks for the high-performance card)%s\n",
                twoc_settings_ignored() ? " (safe settings)" : "", hp_order ? "" : "; DXGI 1.6's GPU order not available");
    }
    if (chosen)
        IDXGIAdapter1_Release(chosen);  /* xbox_d3d8 keeps its own reference */
    IDXGIFactory1_Release(f);
}

/* gdi32's D3DKMT entries (d3dkmthk.h's structures, declared here: the header wants the WDK's types). */
typedef struct {
    WCHAR DeviceName[32];
    UINT hAdapter;
    LUID AdapterLuid;
    UINT VidPnSourceId;
} kmt_open_gdi_t;
typedef LONG (APIENTRY *kmt_open_gdi_fn)(kmt_open_gdi_t *);
typedef LONG (APIENTRY *kmt_close_fn)(const UINT *);

/*
 * Step 99: the adapter that drives monitor `mon`, as the display driver has it (its GDI name -> the kernel
 * adapter's LUID). DXGI's EnumOutputs can't be trusted for this: on a hybrid laptop a process on the NVIDIA GPU
 * gets the Intel's screen listed under the NVIDIA adapter (notes 18.101). -1 when not found.
 */
static int display_adapter(HMONITOR mon)
{
    static kmt_open_gdi_fn open_fn;
    static kmt_close_fn close_fn;
    static int looked;
    MONITORINFOEXW mi;
    kmt_open_gdi_t o;
    int k;

    if (!looked) {
        HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
        looked = 1;
        if (gdi) {
            open_fn = (kmt_open_gdi_fn)GetProcAddress(gdi, "D3DKMTOpenAdapterFromGdiDisplayName");
            close_fn = (kmt_close_fn)GetProcAddress(gdi, "D3DKMTCloseAdapter");
        }
    }
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!open_fn || !mon || !GetMonitorInfoW(mon, (MONITORINFO *)&mi))
        return -1;
    memset(&o, 0, sizeof(o));
    wcsncpy(o.DeviceName, mi.szDevice, 31);
    if (open_fn(&o) != 0)
        return -1;
    k = by_luid(o.AdapterLuid);
    if (close_fn)
        close_fn(&o.hAdapter);
    return k;
}

/* The adapter (index in the list) of the device, and of the monitor `hwnd` is on; -1 each when not known.
   `listed` gets the adapter DXGI lists the monitor under (EnumOutputs), which can differ. */
static void where(void *dev, HWND hwnd, int *used, int *disp, int *listed, char *used_name, size_t cap)
{
    IDXGIDevice *xd = NULL;
    IDXGIAdapter *ad = NULL;
    IDXGIFactory1 *f = NULL;
    IDXGIAdapter1 *a = NULL;
    IDXGIOutput *o = NULL;
    DXGI_ADAPTER_DESC d;
    DXGI_ADAPTER_DESC1 d1;
    DXGI_OUTPUT_DESC od;
    HMONITOR mon = hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) : NULL;
    UINT i, j;

    *used = *disp = *listed = -1;
    used_name[0] = 0;
    if (dev && SUCCEEDED(ID3D11Device_QueryInterface((ID3D11Device *)dev, &IID_IDXGIDevice, (void **)&xd))) {
        if (SUCCEEDED(IDXGIDevice_GetAdapter(xd, &ad)) && SUCCEEDED(IDXGIAdapter_GetDesc(ad, &d))) {
            *used = by_luid(d.AdapterLuid);
            WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, used_name, (int)cap, NULL, NULL);
        }
        if (ad)
            IDXGIAdapter_Release(ad);
        IDXGIDevice_Release(xd);
    }
    if (!mon)
        return;
    *disp = display_adapter(mon);
    if (FAILED(CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&f)))
        return;
    for (i = 0; *listed < 0 && SUCCEEDED(IDXGIFactory1_EnumAdapters1(f, i, &a)); i++) {
        for (j = 0; *listed < 0 && SUCCEEDED(IDXGIAdapter1_EnumOutputs(a, j, &o)); j++) {
            if (SUCCEEDED(IDXGIOutput_GetDesc(o, &od)) && od.Monitor == mon &&
                SUCCEEDED(IDXGIAdapter1_GetDesc1(a, &d1)))
                *listed = by_luid(d1.AdapterLuid);
            IDXGIOutput_Release(o);
        }
        IDXGIAdapter1_Release(a);
    }
    IDXGIFactory1_Release(f);
    if (*disp < 0) {                    /* no D3DKMT answer: DXGI's, the old way */
        *disp = *listed;
        *listed = -2;
    }
}

void twoc_gpu_device_made(void *dev, HWND hwnd)
{
    char name[128];
    int used, disp, listed;

    where(dev, hwnd, &used, &disp, &listed, name, sizeof(name));
    snprintf(g_used, sizeof(g_used), "%s", used >= 0 ? g_gpu[used].name : name);
    fprintf(stderr, "[GPU] the device is on %s%s%s; the window's display is on %s%s%s\n",
            used >= 0 ? "graphics card " : "", used >= 0 ? "" : name[0] ? name : "an adapter not listed",
            used >= 0 ? g_gpu[used].name : "",
            disp >= 0 ? "graphics card " : "", disp >= 0 ? g_gpu[disp].name : "a card not known",
            used >= 0 && disp >= 0 && used != disp ? " (Windows copies each frame)" : "");
    if (listed >= 0 && disp >= 0 && listed != disp)     /* step 99: a hybrid laptop's DXGI view */
        fprintf(stderr, "[GPU] DXGI lists the window's display under graphics card %s; the display driver has it on %s\n",
                g_gpu[listed].name, g_gpu[disp].name);
    else if (listed == -2)
        fprintf(stderr, "[GPU] the display driver's card for the window's display isn't known (D3DKMT): DXGI's outputs "
                "say which\n");
}

void twoc_gpu_about(void *dev, HWND hwnd, char *used_buf, size_t used_cap, char *disp_buf, size_t disp_cap)
{
    char name[128];
    int used, disp, listed;

    where(dev, hwnd, &used, &disp, &listed, name, sizeof(name));
    if (g_asked[0] && !g_note[0])
        snprintf(used_buf, used_cap, "chosen in the Display tab");
    else if (g_note[0])
        snprintf(used_buf, used_cap, "Automatic (\"%s\" isn't on this PC)", g_asked);
    else
        snprintf(used_buf, used_cap, g_n > 1 ? "Automatic (Windows' choice; the game asks for the high-performance "
                                               "card)" : "Automatic (the only graphics card)");
    if (disp < 0)
        snprintf(disp_buf, disp_cap, "not known");
    else if (disp == used)
        snprintf(disp_buf, disp_cap, "%s (the same card)", g_gpu[disp].name);
    else
        snprintf(disp_buf, disp_cap, "%s (Windows copies each frame)", g_gpu[disp].name);
}
