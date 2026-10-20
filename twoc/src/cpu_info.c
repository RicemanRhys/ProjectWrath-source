/**
 * The processor and the process's power throttling (step 97). See cpu_info.h.
 */
#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_info.h"

static char g_cpu[256] = "not known";
static char g_power[192] = "not known";
static int g_hybrid;

/* ProcessPowerThrottling (processthreadsapi.h, Windows 10 1709 on), declared here so the build needs no
 * particular SDK target; GetProcessInformation / SetProcessInformation are found at run time. */
typedef struct {
    ULONG Version, ControlMask, StateMask;
} pt_state_t;
#define PT_VERSION 1
#define PT_EXECUTION_SPEED 0x1
#define PT_CLASS 4                      /* PROCESS_INFORMATION_CLASS ProcessPowerThrottling */
/* The opt-out without RECOMP_CPU_THROTTLING (=windows: Windows' rules; =optout: the opt-out). */
#define OPT_OUT_DEFAULT 0
typedef BOOL(WINAPI *procinfo_t)(HANDLE, int, LPVOID, DWORD);

const char *twoc_cpu_text(void) { return g_cpu; }
const char *twoc_cpu_power_text(void) { return g_power; }
int twoc_cpu_hybrid(void) { return g_hybrid; }

static void brand(char *out, size_t cap)
{
    int r[12];
    char b[49], *p;
    size_t n;

    __cpuid(r, 0x80000000);
    if ((unsigned)r[0] < 0x80000004u) {
        snprintf(out, cap, "unknown processor");
        return;
    }
    __cpuid(r, 0x80000002);
    __cpuid(r + 4, 0x80000003);
    __cpuid(r + 8, 0x80000004);
    memcpy(b, r, 48);
    b[48] = 0;
    for (p = b; *p == ' '; p++)
        ;
    snprintf(out, cap, "%s", p);
    n = strlen(out);
    while (n && out[n - 1] == ' ')
        out[--n] = 0;
}

/* The power throttling state the process has asked for: "" when it never asked. */
static void throttle_text(procinfo_t get, char *out, size_t cap, ULONG *control, ULONG *state)
{
    pt_state_t st;

    memset(&st, 0, sizeof(st));
    st.Version = PT_VERSION;
    *control = *state = 0;
    if (!get || !get(GetCurrentProcess(), PT_CLASS, &st, sizeof(st))) {
        snprintf(out, cap, "not readable (error %lu)", get ? GetLastError() : 0ul);
        return;
    }
    *control = st.ControlMask;
    *state = st.StateMask;
    if (!(st.ControlMask & PT_EXECUTION_SPEED))
        snprintf(out, cap, "Windows' own rules (it may throttle a process it thinks is in the background)");
    else if (st.StateMask & PT_EXECUTION_SPEED)
        snprintf(out, cap, "throttled (EcoQoS: lower clocks, efficiency cores)");
    else
        snprintf(out, cap, "not throttled (the game opted out of Windows' power throttling)");
}

/* g_cpu and g_hybrid from GetSystemCpuSetInformation's `len` bytes at `buf` (NULL: not known) for the CPU `name`. */
static void describe(const BYTE *buf, ULONG len, const char *name)
{
    char classes[160] = "";
    unsigned threads = 0, cores = 0, nclass = 0, k;
    struct { unsigned cls, threads, cores; } cl[4];
    static unsigned core_key[256];      /* (group << 8 | core index) of each core seen */
    ULONG off = 0;

    memset(cl, 0, sizeof(cl));
    while (buf && off + 8 <= len) {
        const SYSTEM_CPU_SET_INFORMATION *ci = (const SYSTEM_CPU_SET_INFORMATION *)(buf + off);
        if (ci->Size == 0)
            break;
        if (ci->Type == CpuSetInformation) {
            unsigned cls = ci->CpuSet.EfficiencyClass, key = (unsigned)ci->CpuSet.Group << 8 | ci->CpuSet.CoreIndex;
            unsigned j, c;
            threads++;
            for (c = 0; c < nclass && cl[c].cls != cls; c++)
                ;
            if (c == nclass && nclass < 4)
                cl[nclass++].cls = cls;
            if (c < 4)
                cl[c].threads++;
            for (j = 0; j < cores && core_key[j] != key; j++)
                ;
            if (j == cores && cores < 256) {
                core_key[cores++] = key;
                if (c < 4)
                    cl[c].cores++;
            }
        }
        off += ci->Size;
    }
    g_hybrid = nclass > 1;
    if (g_hybrid) {                     /* the most performant class first */
        unsigned a, b;
        for (a = 0; a < nclass; a++)
            for (b = a + 1; b < nclass; b++)
                if (cl[b].cls > cl[a].cls) {
                    unsigned t;
                    t = cl[a].cls, cl[a].cls = cl[b].cls, cl[b].cls = t;
                    t = cl[a].threads, cl[a].threads = cl[b].threads, cl[b].threads = t;
                    t = cl[a].cores, cl[a].cores = cl[b].cores, cl[b].cores = t;
                }
        for (k = 0; k < nclass; k++) {
            size_t n = strlen(classes);
            snprintf(classes + n, sizeof(classes) - n, "%s%u %s core%s (%u thread%s, efficiency class %u)",
                     k ? ", " : "", cl[k].cores, k == 0 ? "performance" : k == nclass - 1 ? "efficiency" : "middle",
                     cl[k].cores == 1 ? "" : "s", cl[k].threads, cl[k].threads == 1 ? "" : "s", cl[k].cls);
        }
    }
    if (threads)
        snprintf(g_cpu, sizeof(g_cpu), "%s; %u cores, %u threads%s%s", name, cores, threads,
                 g_hybrid ? "; hybrid: " : " (one kind of core)", classes);
    else
        snprintf(g_cpu, sizeof(g_cpu), "%s; cores not known (GetSystemCpuSetInformation)", name);
}

void twoc_cpu_start(void)
{
    typedef BOOL(WINAPI *cpusets_t)(void *, ULONG, PULONG, HANDLE, ULONG);
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    cpusets_t sets = (cpusets_t)(void *)GetProcAddress(k32, "GetSystemCpuSetInformation");
    procinfo_t get = (procinfo_t)(void *)GetProcAddress(k32, "GetProcessInformation");
    procinfo_t set = (procinfo_t)(void *)GetProcAddress(k32, "SetProcessInformation");
    const char *e = getenv("RECOMP_CPU_THROTTLING");
    char name[64], before[160];
    ULONG len = 0, c0, s0, c1, s1;
    BYTE *buf = NULL;

    brand(name, sizeof(name));
    if (sets && !sets(NULL, 0, &len, GetCurrentProcess(), 0) && GetLastError() == ERROR_INSUFFICIENT_BUFFER &&
        (buf = (BYTE *)malloc(len)) != NULL && sets(buf, len, &len, GetCurrentProcess(), 0))
        describe(buf, len, name);
    else
        describe(NULL, 0, name);
    free(buf);
    fprintf(stderr, "[CPU] %s\n", g_cpu);

    throttle_text(get, before, sizeof(before), &c0, &s0);
    if (e && *e ? _stricmp(e, "windows") == 0 : !OPT_OUT_DEFAULT) {
        snprintf(g_power, sizeof(g_power), "%s%s", before, e && *e ? " (RECOMP_CPU_THROTTLING=windows)" : "");
        fprintf(stderr, "[CPU] power throttling: %s\n", g_power);
        return;
    }
    {
        pt_state_t st;
        memset(&st, 0, sizeof(st));
        st.Version = PT_VERSION;
        st.ControlMask = PT_EXECUTION_SPEED;    /* this process decides ... */
        st.StateMask = 0;                       /* ... and it is never throttled */
        if (!set || !set(GetCurrentProcess(), PT_CLASS, &st, sizeof(st))) {
            DWORD err = set ? GetLastError() : 0;
            snprintf(g_power, sizeof(g_power), "%s; the opt-out failed (error %lu)", before, err);
            fprintf(stderr, "[CPU] power throttling: %s\n", g_power);
            return;
        }
    }
    throttle_text(get, g_power, sizeof(g_power), &c1, &s1);
    fprintf(stderr, "[CPU] power throttling: %s (before: %s)\n", g_power, before);
}
