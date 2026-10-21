/**
 * Memory logging (step 99): see mem_log.h.
 */
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>

#include "mem_log.h"
#include "gpu.h"

static SRWLOCK g_lock = SRWLOCK_INIT;   /* g_text: held only to copy it */
static char g_text[3072];

#define MB(x) ((double)(x) / 1048576.0)

/* This process's thread count (a process snapshot: its cntThreads). */
static int thread_count(void)
{
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe;
    DWORD me = GetCurrentProcessId();
    int n = -1;

    if (h == INVALID_HANDLE_VALUE)
        return -1;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(h, &pe))
        do {
            if (pe.th32ProcessID == me) {
                n = (int)pe.cntThreads;
                break;
            }
        } while (Process32NextW(h, &pe));
    CloseHandle(h);
    return n;
}

void mem_log_sample(double now_s)
{
    PROCESS_MEMORY_COUNTERS_EX pm;
    PERFORMANCE_INFORMATION pi;
    DWORD handles = 0;
    twoc_vram_t v;
    char t[3072], part[900];
    int n = 0;

    memset(&pm, 0, sizeof(pm));
    pm.cb = sizeof(pm);
    n += snprintf(t + n, sizeof(t) - n, "+%.1f s: ", now_s);
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pm, sizeof(pm)))
        n += snprintf(t + n, sizeof(t) - n, "process private %.1f MB (peak %.1f), working set %.1f MB (peak %.1f)",
                      MB(pm.PrivateUsage), MB(pm.PeakPagefileUsage), MB(pm.WorkingSetSize), MB(pm.PeakWorkingSetSize));
    else
        n += snprintf(t + n, sizeof(t) - n, "process memory not read");
    memset(&pi, 0, sizeof(pi));
    pi.cb = sizeof(pi);
    if (GetPerformanceInfo(&pi, sizeof(pi)) && n < (int)sizeof(t))
        n += snprintf(t + n, sizeof(t) - n, ", system commit %.2f GB of %.2f",
                      (double)pi.CommitTotal * pi.PageSize / 1073741824.0,
                      (double)pi.CommitLimit * pi.PageSize / 1073741824.0);
    GetProcessHandleCount(GetCurrentProcess(), &handles);
    if (n < (int)sizeof(t))
        n += snprintf(t + n, sizeof(t) - n, "; handles %lu, threads %d, GDI %lu, USER %lu", handles, thread_count(),
                      GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS),
                      GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS));
    if (n < (int)sizeof(t)) {
        if (twoc_gpu_vram(&v))
            n += snprintf(t + n, sizeof(t) - n, "; GPU local %.1f MB (budget %.1f), non-local %.1f MB (budget %.1f)",
                          MB(v.local_use), MB(v.local_budget), MB(v.nonlocal_use), MB(v.nonlocal_budget));
        else
            n += snprintf(t + n, sizeof(t) - n, "; GPU memory not read");
    }
    d3db_mem_text(part, sizeof(part));
    if (part[0] && n < (int)sizeof(t))
        n += snprintf(t + n, sizeof(t) - n, "; %s", part);
    d3dh_mem_text(part, sizeof(part));
    if (part[0] && n < (int)sizeof(t))
        n += snprintf(t + n, sizeof(t) - n, "; %s", part);
    twoc_guest_mem_text(part, sizeof(part));
    if (part[0] && n < (int)sizeof(t))
        snprintf(t + n, sizeof(t) - n, "; %s", part);
    AcquireSRWLockExclusive(&g_lock);
    memcpy(g_text, t, sizeof(g_text));
    ReleaseSRWLockExclusive(&g_lock);
}

void mem_log_last(char *buf, size_t cap)
{
    AcquireSRWLockShared(&g_lock);
    snprintf(buf, cap, "%s", g_text);
    ReleaseSRWLockShared(&g_lock);
}
