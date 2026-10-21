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

/* Step 99: each logical processor's kind (0 the most performant class ... g_nrank - 1 the least; -1 not known),
 * by group and number as GetCurrentProcessorNumberEx gives them. */
#define LP_GROUPS 4
static signed char g_rank[LP_GROUPS][64];
static int g_nrank;
static unsigned g_fake_p;               /* RECOMP_DEV_CPU_CLASSES: the first N read as performance cores */

/* ProcessPowerThrottling (processthreadsapi.h, Windows 10 1709 on), declared here so the build needs no
 * particular SDK target; GetProcessInformation / SetProcessInformation are found at run time. */
typedef struct {
    ULONG Version, ControlMask, StateMask;
} pt_state_t;
#define PT_VERSION 1
#define PT_EXECUTION_SPEED 0x1
#define PT_CLASS 4                      /* PROCESS_INFORMATION_CLASS ProcessPowerThrottling */
/* The opt-out without RECOMP_CPU_THROTTLING (=windows: Windows' rules; =optout: the opt-out). */
#define OPT_OUT_DEFAULT 1
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
    static unsigned char lp_cls[LP_GROUPS][64];
    ULONG off = 0;

    memset(cl, 0, sizeof(cl));
    memset(g_rank, -1, sizeof(g_rank));
    while (buf && off + 8 <= len) {
        const SYSTEM_CPU_SET_INFORMATION *ci = (const SYSTEM_CPU_SET_INFORMATION *)(buf + off);
        if (ci->Size == 0)
            break;
        if (ci->Type == CpuSetInformation) {
            unsigned cls = ci->CpuSet.EfficiencyClass, key = (unsigned)ci->CpuSet.Group << 8 | ci->CpuSet.CoreIndex;
            unsigned j, c;
            if (g_fake_p)               /* test aid: a hybrid CPU's map on this one */
                cls = threads < g_fake_p ? 1 : 0;
            if (ci->CpuSet.Group < LP_GROUPS && ci->CpuSet.LogicalProcessorIndex < 64) {
                lp_cls[ci->CpuSet.Group][ci->CpuSet.LogicalProcessorIndex] = (unsigned char)cls;
                g_rank[ci->CpuSet.Group][ci->CpuSet.LogicalProcessorIndex] = 0;
            }
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
    g_nrank = nclass ? (int)nclass : 0;
    {                                   /* step 99: each logical processor's place in that order */
        unsigned gr, i;
        for (gr = 0; gr < LP_GROUPS; gr++)
            for (i = 0; i < 64; i++)
                if (g_rank[gr][i] >= 0)
                    for (k = 0; k < nclass; k++)
                        if (cl[k].cls == lp_cls[gr][i])
                            g_rank[gr][i] = (signed char)k;
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
    if (getenv("RECOMP_DEV_CPU_CLASSES") && atoi(getenv("RECOMP_DEV_CPU_CLASSES")) > 0) {     /* step 99, a test aid */
        g_fake_p = (unsigned)atoi(getenv("RECOMP_DEV_CPU_CLASSES"));
        fprintf(stderr, "[CPU] RECOMP_DEV_CPU_CLASSES=%u: the first %u logical processors read as performance cores,"
                " the rest as efficiency cores (a test)\n", g_fake_p, g_fake_p);
    }
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

/* Step 99: the game and render threads' samples since the last interval line, by logical processor. */
static volatile LONG g_lp[2][LP_GROUPS * 64];

int twoc_cpu_ranks(void) { return g_nrank; }

int twoc_cpu_rank_of(unsigned group, unsigned number)
{
    return group < LP_GROUPS && number < 64 ? g_rank[group][number] : -1;
}

void twoc_cpu_where(int who)
{
    PROCESSOR_NUMBER pn;

    GetCurrentProcessorNumberEx(&pn);
    if (pn.Group < LP_GROUPS && pn.Number < 64)
        InterlockedIncrement(&g_lp[who & 1][pn.Group * 64 + pn.Number]);
}

/* One thread's samples as text; its counts start again. 0 when it had none. */
static int where_one(int who, const char *name, char *buf, size_t cap)
{
    LONG c[LP_GROUPS * 64];
    unsigned long n = 0, by_rank[4] = { 0 }, top = 0, top2 = 0;
    int i, used = 0, ti = -1, ti2 = -1, k = 0;

    for (i = 0; i < LP_GROUPS * 64; i++) {
        c[i] = InterlockedExchange(&g_lp[who][i], 0);
        if (!c[i])
            continue;
        n += (unsigned long)c[i];
        used++;
        if (g_rank[i / 64][i % 64] >= 0 && g_rank[i / 64][i % 64] < 4)
            by_rank[g_rank[i / 64][i % 64]] += (unsigned long)c[i];
        if ((unsigned long)c[i] > top) {
            top2 = top, ti2 = ti;
            top = (unsigned long)c[i], ti = i;
        } else if ((unsigned long)c[i] > top2)
            top2 = (unsigned long)c[i], ti2 = i;
    }
    if (!n)
        return 0;
    if (g_nrank > 1) {
        int r;
        k += snprintf(buf + k, cap - k, "the %s thread on", name);
        for (r = 0; r < g_nrank && r < 4 && k < (int)cap; r++)
            k += snprintf(buf + k, cap - k, "%s %s cores %.1f%%", r ? "," : "",
                          r == 0 ? "performance" : r == g_nrank - 1 ? "efficiency" : "middle", 100.0 * by_rank[r] / n);
        if (k < (int)cap)
            k += snprintf(buf + k, cap - k, " of %lu samples", n);
    } else
        k += snprintf(buf + k, cap - k, "the %s thread sampled %lu time%s", name, n, n == 1 ? "" : "s");
    if (k < (int)cap)
        k += snprintf(buf + k, cap - k, ", on %d logical processor%s (most on %s%d %.0f%%", used, used == 1 ? "" : "s",
                      ti >= 64 ? "group 1+ " : "CPU ", ti % 64, 100.0 * top / n);
    if (ti2 >= 0 && k < (int)cap)
        k += snprintf(buf + k, cap - k, ", then CPU %d %.0f%%", ti2 % 64, 100.0 * top2 / n);
    if (k < (int)cap)
        k += snprintf(buf + k, cap - k, ")");
    return k;
}

int twoc_cpu_where_text(char *buf, size_t cap)
{
    int k = 0, j;

    buf[0] = 0;
    if (g_nrank <= 1)
        k += snprintf(buf, cap, "one kind of core (no performance / efficiency split): ");
    j = where_one(0, "game", buf + k, cap - k);
    k += j;
    if (k < (int)cap && j)
        k += snprintf(buf + k, cap - k, "; ");
    j = where_one(1, "render", buf + k, cap - k);
    if (!j && k >= 2 && buf[k - 2] == ';')
        buf[k -= 2] = 0;
    return k + j;
}
