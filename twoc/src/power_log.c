/**
 * Power and chip-speed logging (step 91): see power_log.h.
 */
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "power_log.h"

/* CallNtPowerInformation's levels and its ProcessorInformation entry
 * (ntpoapi.h's PROCESSOR_POWER_INFORMATION, not in the user headers). */
#define PI_SYSTEM_BATTERY_STATE 5
#define PI_PROCESSOR_INFORMATION 11
#define BATTERY_RATE_UNKNOWN 0x80000000u    /* batclass.h's BATTERY_UNKNOWN_RATE */
typedef struct {
    ULONG Number, MaxMhz, CurrentMhz, MhzLimit, MaxIdleState, CurrentIdleState;
} proc_power_t;

typedef LONG(WINAPI *nt_power_fn)(int level, PVOID in, ULONG in_len, PVOID out, ULONG out_len);
typedef DWORD(WINAPI *overlay_fn)(GUID *guid);
typedef DWORD(WINAPI *active_fn)(HKEY root, GUID **guid);
typedef DWORD(WINAPI *friendly_fn)(HKEY root, const GUID *scheme, const GUID *sub, const GUID *setting, UCHAR *buf,
                                   DWORD *size);
typedef void(WINAPI *mode_cb)(int mode, void *ctx);
typedef HRESULT(WINAPI *mode_reg_fn)(ULONG version, mode_cb cb, void *ctx, void **handle);
typedef PDH_STATUS(WINAPI *pdh_open_fn)(LPCWSTR src, DWORD_PTR user, PDH_HQUERY *q);
typedef PDH_STATUS(WINAPI *pdh_add_fn)(PDH_HQUERY q, LPCWSTR path, DWORD_PTR user, PDH_HCOUNTER *c);
typedef PDH_STATUS(WINAPI *pdh_collect_fn)(PDH_HQUERY q);
typedef PDH_STATUS(WINAPI *pdh_value_fn)(PDH_HCOUNTER c, DWORD fmt, LPDWORD type, PPDH_FMT_COUNTERVALUE v);
typedef PDH_STATUS(WINAPI *pdh_array_fn)(PDH_HCOUNTER c, DWORD fmt, LPDWORD size, LPDWORD count,
                                         PPDH_FMT_COUNTERVALUE_ITEM_W items);
typedef HRESULT(WINAPI *describe_fn)(HANDLE thread, PCWSTR name);

static struct {
    nt_power_fn nt_power;
    overlay_fn overlay;
    active_fn active;
    friendly_fn friendly;
    mode_reg_fn mode_reg;
    pdh_open_fn open;
    pdh_add_fn add;
    pdh_collect_fn collect;
    pdh_value_fn value;
    pdh_array_fn array;
} P;

/* One second's reading. */
typedef struct {
    int overlay_ok;                     /* PowerGetEffectiveOverlayScheme answered */
    GUID overlay;
    int eff_mode;                       /* EFFECTIVE_POWER_MODE, -1 not known */
    int plan_ok;
    GUID plan;
    char plan_name[64];
    int ac;                             /* 1 plugged in, 0 on battery, -1 not known */
    int batt_pct, no_batt, charging, saver;
    long life_min;                      /* -1 not known */
    long rate_mw;                       /* the battery's rate: below 0 discharging; 0 not known */
    int cpu_ok;                         /* the PDH processor counters read */
    double mhz, perf_pct, cap_pct, util_pct;    /* effective MHz, % of the rated, "% of Maximum Frequency", load */
    unsigned rated_mhz;                 /* Processor Frequency (the rated speed) */
    unsigned nt_cur, nt_max, nt_limit;  /* CallNtPowerInformation: average current, rated, lowest limit; 0 none */
    int temp_ok;                        /* 1 read, 0 not yet, -1 Windows has no thermal zones */
    double temp_c, passive_pct;
    unsigned throttle;
    char zone[40];
} sample_t;

/* The CPU's effective speed over an interval. */
typedef struct {
    unsigned n;
    double sum, lo, hi;
} acc_t;

static SRWLOCK g_lock = SRWLOCK_INIT;  /* g_now and g_acc_b: held only to copy them */
static sample_t g_now;
static int g_have;
static acc_t g_acc_b;                   /* since the bridge's last interval line */
static volatile LONG g_eff_mode = -1;
static volatile double g_ups = -1, g_game_ms = -1, g_render_ms = -1;
static power_log_hooks_t g_hooks;
static HANDLE g_file = INVALID_HANDLE_VALUE;

static const GUID G_OVL_EFFICIENCY = { 0x961cc777, 0x2547, 0x4f9d, { 0x81, 0x74, 0x7d, 0x86, 0x18, 0x1b, 0x8a, 0x7a } };
static const GUID G_OVL_BALANCED = { 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0 } };
static const GUID G_OVL_BETTER = { 0x3af9b8d9, 0x7c97, 0x431d, { 0xad, 0x78, 0x34, 0xa8, 0xbf, 0xea, 0x43, 0x9f } };
static const GUID G_OVL_BEST = { 0xded574b5, 0x45a0, 0x4f42, { 0x87, 0x37, 0x46, 0x34, 0x5c, 0x09, 0xc2, 0x38 } };

static void guid_text(const GUID *g, char *buf, size_t cap)
{
    snprintf(buf, cap, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", (unsigned long)g->Data1, g->Data2,
             g->Data3, g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6],
             g->Data4[7]);
}

static const char *overlay_name(const sample_t *s, char *buf, size_t cap)
{
    if (!s->overlay_ok)
        return "not known";
    if (IsEqualGUID(&s->overlay, &G_OVL_EFFICIENCY))
        return "Best power efficiency";
    if (IsEqualGUID(&s->overlay, &G_OVL_BALANCED))
        return "Balanced";
    if (IsEqualGUID(&s->overlay, &G_OVL_BETTER))
        return "Better performance";
    if (IsEqualGUID(&s->overlay, &G_OVL_BEST))
        return "Best performance";
    guid_text(&s->overlay, buf, cap);
    return buf;
}

static const char *eff_name(int m)
{
    static const char *const names[] = { "battery saver", "better battery", "balanced", "high performance",
                                         "max performance", "game mode", "mixed reality" };
    return m >= 0 && m < (int)(sizeof(names) / sizeof(names[0])) ? names[m] : "not known";
}

static const char *ac_name(int ac)
{
    return ac == 1 ? "plugged in" : ac == 0 ? "on battery" : "power source unknown";
}

/* The speed class a reading is in: what a mode switch moves. */
enum { SPEED_UNKNOWN, SPEED_LOW, SPEED_REDUCED, SPEED_FULL, SPEED_BOOST };

static double speed_pct(const sample_t *s)
{
    if (s->cpu_ok)
        return s->perf_pct;
    return s->nt_max ? 100.0 * s->nt_cur / s->nt_max : -1;
}

static int speed_class(const sample_t *s)
{
    double p = speed_pct(s);

    return p < 0 ? SPEED_UNKNOWN : p < 50 ? SPEED_LOW : p < 85 ? SPEED_REDUCED : p < 115 ? SPEED_FULL : SPEED_BOOST;
}

static const char *speed_name(int c)
{
    static const char *const names[] = { "not known", "low (under 50% of its rated speed)",
                                         "reduced (50-85%)", "full (85-115%)", "boost (over 115%)" };
    return names[c];
}

static void WINAPI on_mode(int mode, void *ctx)
{
    (void)ctx;
    InterlockedExchange(&g_eff_mode, mode);
}

/* PDH: the processor counters, and the thermal zones' (read every 10 s). */
static PDH_HQUERY g_q, g_qt;
static PDH_HCOUNTER g_c_perf, g_c_freq, g_c_cap, g_c_util, g_c_temp, g_c_passive, g_c_reasons;
static int g_temp_tenths;               /* "High Precision Temperature": tenths of a kelvin */

static void pdh_init(void)
{
    HMODULE m = LoadLibraryA("pdh.dll");

    if (!m)
        return;
    P.open = (pdh_open_fn)(void (*)(void))GetProcAddress(m, "PdhOpenQueryW");
    P.add = (pdh_add_fn)(void (*)(void))GetProcAddress(m, "PdhAddEnglishCounterW");
    P.collect = (pdh_collect_fn)(void (*)(void))GetProcAddress(m, "PdhCollectQueryData");
    P.value = (pdh_value_fn)(void (*)(void))GetProcAddress(m, "PdhGetFormattedCounterValue");
    P.array = (pdh_array_fn)(void (*)(void))GetProcAddress(m, "PdhGetFormattedCounterArrayW");
    if (!P.open || !P.add || !P.collect || !P.value || !P.array) {
        P.open = NULL;
        return;
    }
    if (P.open(NULL, 0, &g_q) == ERROR_SUCCESS) {
        if (P.add(g_q, L"\\Processor Information(_Total)\\% Processor Performance", 0, &g_c_perf) != ERROR_SUCCESS ||
            P.add(g_q, L"\\Processor Information(_Total)\\Processor Frequency", 0, &g_c_freq) != ERROR_SUCCESS)
            g_c_perf = g_c_freq = NULL;
        if (P.add(g_q, L"\\Processor Information(_Total)\\% of Maximum Frequency", 0, &g_c_cap) != ERROR_SUCCESS)
            g_c_cap = NULL;
        if (P.add(g_q, L"\\Processor Information(_Total)\\% Processor Utility", 0, &g_c_util) != ERROR_SUCCESS)
            g_c_util = NULL;
        P.collect(g_q);                 /* the rates need two readings: the first */
    }
    if (P.open(NULL, 0, &g_qt) == ERROR_SUCCESS) {
        g_temp_tenths = 1;
        if (P.add(g_qt, L"\\Thermal Zone Information(*)\\High Precision Temperature", 0, &g_c_temp) != ERROR_SUCCESS) {
            g_temp_tenths = 0;
            if (P.add(g_qt, L"\\Thermal Zone Information(*)\\Temperature", 0, &g_c_temp) != ERROR_SUCCESS)
                g_c_temp = NULL;
        }
        if (P.add(g_qt, L"\\Thermal Zone Information(*)\\% Passive Limit", 0, &g_c_passive) != ERROR_SUCCESS)
            g_c_passive = NULL;
        if (P.add(g_qt, L"\\Thermal Zone Information(*)\\Throttle Reasons", 0, &g_c_reasons) != ERROR_SUCCESS)
            g_c_reasons = NULL;
    }
}

static double pdh_double(PDH_HCOUNTER c, int *ok)
{
    PDH_FMT_COUNTERVALUE v;

    if (!c || P.value(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, NULL, &v) != ERROR_SUCCESS ||
        (v.CStatus != PDH_CSTATUS_VALID_DATA && v.CStatus != PDH_CSTATUS_NEW_DATA)) {
        *ok = 0;
        return 0;
    }
    return v.doubleValue;
}

/* The thermal zones: the hottest one's temperature, its passive limit and
 * throttle reasons. -1 when Windows has none. */
static int read_thermal(sample_t *s)
{
    static unsigned char buf[16384];
    PPDH_FMT_COUNTERVALUE_ITEM_W it = (PPDH_FMT_COUNTERVALUE_ITEM_W)buf;
    DWORD size = sizeof(buf), n = 0, i, best = 0;
    double hot = -1000;

    if (!P.open || !g_qt || !g_c_temp || P.collect(g_qt) != ERROR_SUCCESS ||
        P.array(g_c_temp, PDH_FMT_DOUBLE, &size, &n, it) != ERROR_SUCCESS || n == 0)
        return -1;
    for (i = 0; i < n; i++) {
        double k = it[i].FmtValue.doubleValue / (g_temp_tenths ? 10.0 : 1.0);
        if (it[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA && k > hot) {
            hot = k;
            best = i;
        }
    }
    if (hot <= 0)
        return -1;
    s->temp_c = hot - 273.15;
    WideCharToMultiByte(CP_UTF8, 0, it[best].szName ? it[best].szName : L"", -1, s->zone, sizeof(s->zone), NULL,
                        NULL);
    s->passive_pct = -1;
    s->throttle = 0;
    size = sizeof(buf);
    if (g_c_passive && P.array(g_c_passive, PDH_FMT_DOUBLE, &size, &n, it) == ERROR_SUCCESS && best < n)
        s->passive_pct = it[best].FmtValue.doubleValue;
    size = sizeof(buf);
    if (g_c_reasons && P.array(g_c_reasons, PDH_FMT_DOUBLE, &size, &n, it) == ERROR_SUCCESS && best < n)
        s->throttle = (unsigned)it[best].FmtValue.doubleValue;
    return 1;
}

static void read_sample(sample_t *s, int thermal)
{
    SYSTEM_POWER_STATUS ps;
    GUID *g = NULL;

    s->overlay_ok = P.overlay && P.overlay(&s->overlay) == ERROR_SUCCESS;
    s->eff_mode = (int)g_eff_mode;
    if (P.active && P.active(NULL, &g) == ERROR_SUCCESS && g) {
        if (!s->plan_ok || !IsEqualGUID(g, &s->plan)) {     /* the name only when the plan is new */
            WCHAR w[64];
            DWORD sz = sizeof(w);
            s->plan = *g;
            s->plan_name[0] = 0;
            if (P.friendly && P.friendly(NULL, g, NULL, NULL, (UCHAR *)w, &sz) == ERROR_SUCCESS) {
                w[63] = 0;
                WideCharToMultiByte(CP_UTF8, 0, w, -1, s->plan_name, sizeof(s->plan_name), NULL, NULL);
            }
            if (!s->plan_name[0])
                guid_text(g, s->plan_name, sizeof(s->plan_name));
        }
        s->plan_ok = 1;
        LocalFree(g);
    } else
        s->plan_ok = 0;
    s->ac = -1;
    s->batt_pct = -1;
    s->life_min = -1;
    s->no_batt = s->charging = s->saver = 0;
    if (GetSystemPowerStatus(&ps)) {
        s->ac = ps.ACLineStatus == 1 ? 1 : ps.ACLineStatus == 0 ? 0 : -1;
        s->no_batt = (ps.BatteryFlag & 128) != 0;
        s->batt_pct = ps.BatteryLifePercent <= 100 ? ps.BatteryLifePercent : -1;
        s->charging = ps.BatteryFlag != 255 && (ps.BatteryFlag & 8);
        s->life_min = ps.ACLineStatus == 0 && ps.BatteryLifeTime != (DWORD)-1 ? (long)(ps.BatteryLifeTime / 60) : -1;
        s->saver = (ps.SystemStatusFlag & 1) != 0;
    }
    s->rate_mw = 0;
    s->nt_cur = s->nt_max = s->nt_limit = 0;
    if (P.nt_power) {
        SYSTEM_BATTERY_STATE bs;
        static proc_power_t pp[256];
        SYSTEM_INFO si;
        unsigned i, n;
        if (!s->no_batt && P.nt_power(PI_SYSTEM_BATTERY_STATE, NULL, 0, &bs, sizeof(bs)) == 0 && bs.BatteryPresent &&
            bs.Rate != BATTERY_RATE_UNKNOWN)
            s->rate_mw = (long)(LONG)bs.Rate;
        GetSystemInfo(&si);
        n = si.dwNumberOfProcessors < 256 ? si.dwNumberOfProcessors : 256;
        if (n && P.nt_power(PI_PROCESSOR_INFORMATION, NULL, 0, pp, (ULONG)(n * sizeof(pp[0]))) == 0) {
            unsigned long long cur = 0;
            s->nt_limit = pp[0].MhzLimit;
            for (i = 0; i < n; i++) {
                cur += pp[i].CurrentMhz;
                if (pp[i].MaxMhz > s->nt_max)
                    s->nt_max = pp[i].MaxMhz;
                if (pp[i].MhzLimit < s->nt_limit)
                    s->nt_limit = pp[i].MhzLimit;
            }
            s->nt_cur = (unsigned)(cur / n);
        }
    }
    s->cpu_ok = 0;
    if (P.open && g_q && g_c_perf && P.collect(g_q) == ERROR_SUCCESS) {
        int ok = 1;
        double perf = pdh_double(g_c_perf, &ok), freq = pdh_double(g_c_freq, &ok);
        if (ok && freq > 0) {
            int ok2 = 1;
            s->cpu_ok = 1;
            s->perf_pct = perf;
            s->rated_mhz = (unsigned)(freq + 0.5);
            s->mhz = freq * perf / 100.0;
            s->cap_pct = pdh_double(g_c_cap, &ok2);
            if (!ok2)
                s->cap_pct = -1;
            ok2 = 1;
            s->util_pct = pdh_double(g_c_util, &ok2);
            if (!ok2)
                s->util_pct = -1;
        }
    }
    if (thermal)
        s->temp_ok = read_thermal(s);
}

/* "plugged in, battery 87% (charging)": the 10-s line's start, as step 82 had it. */
static int source_text(const sample_t *s, char *buf, size_t cap)
{
    int n = snprintf(buf, cap, "%s", ac_name(s->ac));

    if (s->no_batt)
        n += snprintf(buf + n, cap - n, ", no battery");
    else if (s->batt_pct >= 0)
        n += snprintf(buf + n, cap - n, ", battery %d%%%s", s->batt_pct, s->charging ? " (charging)" : "");
    if (s->life_min >= 0)
        n += snprintf(buf + n, cap - n, ", about %ld min left", s->life_min);
    if (s->saver)
        n += snprintf(buf + n, cap - n, ", battery saver on");
    return n;
}

static int temp_text(const sample_t *s, char *buf, size_t cap)
{
    int n;

    if (s->temp_ok < 0)
        return snprintf(buf, cap, "no temperature (Windows has no thermal zone counters here)");
    if (s->temp_ok == 0)
        return snprintf(buf, cap, "temperature not read yet");
    n = snprintf(buf, cap, "%.1f C (%s)", s->temp_c, s->zone);
    if (s->passive_pct >= 0)
        n += snprintf(buf + n, cap - n, ", passive limit %.0f%%", s->passive_pct);
    if (s->throttle)
        n += snprintf(buf + n, cap - n, ", throttle reasons 0x%x", s->throttle);
    return n;
}

static int cpu_now_text(const sample_t *s, char *buf, size_t cap)
{
    if (s->cpu_ok)
        return snprintf(buf, cap, "CPU %.0f MHz (%.0f%% of its rated %u MHz)", s->mhz, s->perf_pct, s->rated_mhz);
    if (s->nt_max)
        return snprintf(buf, cap, "CPU %u MHz of %u (Windows' figure)", s->nt_cur, s->nt_max);
    return snprintf(buf, cap, "CPU speed not read");
}

/* Everything but the source: the modes, the plan, the CPU (over `a` when it
 * has readings), the temperature. */
static void detail_text(const sample_t *s, const acc_t *a, char *buf, size_t cap)
{
    char t[48], tt[160];
    int n = 0;

    n += snprintf(buf + n, cap - n, "power mode %s (Windows' effective mode: %s); plan %s", overlay_name(s, t, sizeof(t)),
                  eff_name(s->eff_mode), s->plan_ok ? s->plan_name : "not known");
    if (s->cpu_ok && a && a->n)
        n += snprintf(buf + n, cap - n, "; CPU %.0f MHz on average over %u readings (%.0f-%.0f), %.0f%% of its rated %u"
                      " MHz", a->sum / a->n, a->n, a->lo, a->hi, 100.0 * a->sum / a->n / s->rated_mhz, s->rated_mhz);
    else if (n < (int)cap) {
        n += snprintf(buf + n, cap - n, "; ");
        n += cpu_now_text(s, buf + n, cap - n);
    }
    if (s->cpu_ok && s->cap_pct >= 0 && n < (int)cap)    /* the P-state's share, which a power limit lowers */
        n += snprintf(buf + n, cap - n, ", %% of Maximum Frequency %.0f", s->cap_pct);
    if (s->cpu_ok && s->util_pct >= 0 && n < (int)cap)
        n += snprintf(buf + n, cap - n, ", load %.0f%%", s->util_pct);
    if (s->nt_max && n < (int)cap)
        n += snprintf(buf + n, cap - n, "; Windows' processor figures %u of %u MHz, limit %u", s->nt_cur, s->nt_max,
                      s->nt_limit);
    if (s->rate_mw && n < (int)cap)
        n += snprintf(buf + n, cap - n, "; the battery %s %.1f W", s->rate_mw < 0 ? "delivering" : "charging at",
                      (s->rate_mw < 0 ? -s->rate_mw : s->rate_mw) / 1000.0);
    temp_text(s, tt, sizeof(tt));
    if (n < (int)cap)
        snprintf(buf + n, cap - n, "; %s", tt);
}

static void acc_add(acc_t *a, double v)
{
    if (!a->n || v < a->lo)
        a->lo = v;
    if (!a->n || v > a->hi)
        a->hi = v;
    a->sum += v;
    a->n++;
}

/* A [POWER] line to the log, and through to the disk with RECOMP_POWER_LOG. */
static void out_line(const char *text, int to_stderr)
{
    char line[1400];
    SYSTEMTIME lt;
    int n;

    GetLocalTime(&lt);
    n = snprintf(line, sizeof(line), "[POWER] %02u:%02u:%02u +%.1f s: %s", lt.wHour, lt.wMinute, lt.wSecond,
                 g_hooks.now_s ? g_hooks.now_s() : GetTickCount64() / 1000.0, text);
    if (n < 0 || n >= (int)sizeof(line) - 2)
        n = (int)sizeof(line) - 3;
    if (to_stderr) {
        fprintf(stderr, "%.*s\n", n, line);
        fflush(stderr);
    }
    if (g_file != INVALID_HANDLE_VALUE) {
        DWORD w;
        line[n++] = '\r';
        line[n++] = '\n';
        WriteFile(g_file, line, (DWORD)n, &w, NULL);
    }
}

/* The game's side for a change line: its frame counter, the last interval's rates. */
static int game_text(char *buf, size_t cap)
{
    int n = snprintf(buf, cap, "frame counter %u", g_hooks.frame_counter ? g_hooks.frame_counter() : 0);

    if (g_ups >= 0)
        n += snprintf(buf + n, cap - n, "; the last interval: the game %.1f updates a second", g_ups);
    if (g_game_ms >= 0)
        n += snprintf(buf + n, cap - n, ", the game thread %.2f ms a frame, the render thread %.2f", g_game_ms,
                      g_render_ms);
    else if (g_ups >= 0)
        n += snprintf(buf + n, cap - n, " (thread ms measured only with a frame rate above Game (60))");
    return n;
}

/* Step 95: the CPU's clock against the run's level. The level is the
 * highest average of 60 one-second readings so far, from 30 s after the
 * start (the boot's boost and loads left out); a drop is 10 readings in a
 * row more than 10% under it (a line), and it is over after 10 readings in
 * a row within 5% of it again (another line). The Legion Go's firmware
 * power limit (notes 18.97) cut the clock 14% about 2.5 minutes into play
 * with the temperature falling, inside one speed class, so the class lines
 * never showed it. */
typedef struct {
    double ring[60], sum, level, level_load, low, run_sum, run_load, run_min;
    unsigned n, i, level_at, run, since, down;
} cdrop_t;

/* One reading (`tick` seconds in); 1 with a line in `buf`. */
static int clock_step(cdrop_t *c, unsigned tick, double mhz, double load, char *buf, size_t cap)
{
    if (tick < 30)
        return 0;
    c->sum += mhz - (c->n == 60 ? c->ring[c->i] : 0.0);
    c->ring[c->i] = mhz;
    c->i = (c->i + 1) % 60;
    if (c->n < 60)
        c->n++;
    if (c->n == 60 && c->sum / 60.0 > c->level) {
        c->level = c->sum / 60.0;
        c->level_at = tick;
        c->level_load = load;
    }
    if (c->level <= 0)
        return 0;
    if (!c->down) {
        if (mhz < 0.9 * c->level) {
            c->run_min = c->run && c->run_min < mhz ? c->run_min : mhz;
            c->run_sum += mhz;
            c->run_load += load;
            if (++c->run < 10)
                return 0;
            c->down = 1;
            c->since = tick;
            c->low = c->run_min;
            snprintf(buf, cap, "CPU clock down: %.0f MHz on average over the last 10 s, %.0f%% under the run's level of"
                     " %.0f MHz (its highest 60-s average, to +%u s; load %.0f%% then, %.0f%% now)", c->run_sum / c->run,
                     100.0 * (1.0 - c->run_sum / c->run / c->level), c->level, c->level_at, c->level_load,
                     c->run_load / c->run);
            c->run = 0;
            c->run_sum = c->run_load = 0;
            return 1;
        }
        c->run = 0;
        c->run_sum = c->run_load = 0;
        return 0;
    }
    if (mhz < c->low)
        c->low = mhz;
    if (mhz < 0.95 * c->level) {
        c->run = 0;
        return 0;
    }
    if (++c->run < 10)
        return 0;
    c->down = 0;
    c->run = 0;
    snprintf(buf, cap, "CPU clock back up: within 5%% of the run's level (%.0f MHz) for 10 s, after %u s down (the"
             " lowest reading %.0f MHz, %.0f%% under it); load %.0f%%", c->level, tick - c->since, c->low,
             100.0 * (1.0 - c->low / c->level), load);
    return 1;
}

static DWORD WINAPI sampler(void *arg)
{
    sample_t s, was;
    acc_t acc_f = { 0 };
    char t[64], t2[64], what[512], line[1300], g[200];
    int cand = SPEED_UNKNOWN, cand_n = 0, logged = SPEED_UNKNOWN, held = 0, k;
    ULONGLONG last_speed = 0;
    unsigned tick = 0, fake_at = 0, clock_fake = 0;
    cdrop_t clk;
    HMODULE m;

    (void)arg;
    memset(&clk, 0, sizeof(clk));
    if (getenv("RECOMP_DEV_POWER_FAKE")) {  /* test aid: the power mode read as another for 15 s from N s on */
        fake_at = (unsigned)atoi(getenv("RECOMP_DEV_POWER_FAKE"));
        fprintf(stderr, "[POWER] RECOMP_DEV_POWER_FAKE: the power mode reads as another from +%u s for 15 s (a test)\n",
                fake_at);
    }
    if (getenv("RECOMP_DEV_CLOCK_FAKE")) {  /* step 95, test aid: the CPU's MHz read 15% low for 20 s from N s on */
        clock_fake = (unsigned)atoi(getenv("RECOMP_DEV_CLOCK_FAKE"));
        fprintf(stderr, "[POWER] RECOMP_DEV_CLOCK_FAKE: the CPU's MHz read 15%% low from +%u s for 20 s (a test)\n",
                clock_fake);
    }
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    {
        describe_fn d = (describe_fn)(void (*)(void))GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                                                    "SetThreadDescription");
        if (d)
            d(GetCurrentThread(), L"twoc power log");
    }
    if ((m = LoadLibraryA("powrprof.dll")) != NULL) {
        void *h = NULL;
        P.nt_power = (nt_power_fn)(void (*)(void))GetProcAddress(m, "CallNtPowerInformation");
        P.overlay = (overlay_fn)(void (*)(void))GetProcAddress(m, "PowerGetEffectiveOverlayScheme");
        P.active = (active_fn)(void (*)(void))GetProcAddress(m, "PowerGetActiveScheme");
        P.friendly = (friendly_fn)(void (*)(void))GetProcAddress(m, "PowerReadFriendlyName");
        P.mode_reg = (mode_reg_fn)(void (*)(void))GetProcAddress(m, "PowerRegisterForEffectivePowerModeNotifications");
        if (P.mode_reg)                 /* EFFECTIVE_POWER_MODE_V2; the callback has the mode now, then each change */
            P.mode_reg(2, on_mode, NULL, &h);
    }
    pdh_init();
    memset(&s, 0, sizeof(s));
    s.eff_mode = -1;
    Sleep(1000);                        /* the rates' second reading, and the effective mode's first callback */
    read_sample(&s, 1);
    if (s.cpu_ok)
        acc_add(&acc_f, s.mhz);
    AcquireSRWLockExclusive(&g_lock);
    g_now = s;
    g_have = 1;
    if (s.cpu_ok)
        acc_add(&g_acc_b, s.mhz);
    ReleaseSRWLockExclusive(&g_lock);
    k = source_text(&s, line, sizeof(line));
    k += snprintf(line + k, sizeof(line) - k, "; ");
    detail_text(&s, NULL, line + k, sizeof(line) - k);
    snprintf(what, sizeof(what), "sampling once a second (powrprof %s, the effective mode %s, PDH processor counters"
             " %s, thermal zones %s); at the start:", P.nt_power ? "loaded" : "NOT loaded",
             P.mode_reg ? "registered" : "not available", s.cpu_ok ? "read" : "NOT read",
             s.temp_ok > 0 ? "read" : "none here");
    out_line(what, 1);
    out_line(line, 1);
    if (s.temp_ok < 0)
        out_line("no temperature: Windows has no \"Thermal Zone Information\" counters on this PC (logged once)", 1);
    logged = cand = speed_class(&s);
    cand_n = 1;
    for (;;) {
        Sleep(1000);
        tick++;
        was = s;
        read_sample(&s, tick % 10 == 0);
        if (fake_at && tick >= fake_at && tick < fake_at + 15) {    /* test aid: the mode "switched" for 15 s */
            s.overlay_ok = 1;
            s.overlay = IsEqualGUID(&s.overlay, &G_OVL_BEST) ? G_OVL_EFFICIENCY : G_OVL_BEST;
        }
        if (tick % 10 != 0) {           /* the temperature is read every 10 s: kept between */
            s.temp_ok = was.temp_ok;
            s.temp_c = was.temp_c;
            s.passive_pct = was.passive_pct;
            s.throttle = was.throttle;
            memcpy(s.zone, was.zone, sizeof(s.zone));
        }
        if (clock_fake && s.cpu_ok && tick >= clock_fake && tick < clock_fake + 20)
            s.mhz *= 0.85;              /* test aid */
        if (s.cpu_ok)
            acc_add(&acc_f, s.mhz);
        AcquireSRWLockExclusive(&g_lock);
        g_now = s;
        if (s.cpu_ok)
            acc_add(&g_acc_b, s.mhz);
        ReleaseSRWLockExclusive(&g_lock);
        if (s.cpu_ok && clock_step(&clk, tick, s.mhz, s.util_pct, line, sizeof(line))) {     /* step 95 */
            char tt[160], ln[1300];
            game_text(g, sizeof(g));
            temp_text(&s, tt, sizeof(tt));
            snprintf(ln, sizeof(ln), "%s; %s; %s, %s", line, g, ac_name(s.ac), tt);
            out_line(ln, 1);
        }

        /* A change: the power mode, the effective mode, the plan, the source. */
        k = 0;
        what[0] = 0;
        if (s.overlay_ok != was.overlay_ok || (s.overlay_ok && !IsEqualGUID(&s.overlay, &was.overlay)))
            k += snprintf(what + k, sizeof(what) - k, "%spower mode %s -> %s", k ? "; " : "",
                          overlay_name(&was, t, sizeof(t)), overlay_name(&s, t2, sizeof(t2)));
        if (s.eff_mode != was.eff_mode)
            k += snprintf(what + k, sizeof(what) - k, "%sWindows' effective mode %s -> %s", k ? "; " : "",
                          eff_name(was.eff_mode), eff_name(s.eff_mode));
        if (s.plan_ok != was.plan_ok || (s.plan_ok && !IsEqualGUID(&s.plan, &was.plan)))
            k += snprintf(what + k, sizeof(what) - k, "%splan %s -> %s", k ? "; " : "",
                          was.plan_ok ? was.plan_name : "not known", s.plan_ok ? s.plan_name : "not known");
        if (s.ac != was.ac)
            k += snprintf(what + k, sizeof(what) - k, "%s%s -> %s", k ? "; " : "", ac_name(was.ac), ac_name(s.ac));
        /* The CPU's speed class, once it has held 3 s; a line at most every 10 s (the rest counted). */
        {
            int c = speed_class(&s);
            if (c == cand)
                cand_n++;
            else {
                cand = c;
                cand_n = 1;
            }
            if (cand != logged && cand_n >= 3) {
                if (GetTickCount64() - last_speed >= 10000) {
                    k += snprintf(what + k, sizeof(what) - k, "%sCPU speed class %s -> %s (held 3 s)", k ? "; " : "",
                                  speed_name(logged), speed_name(cand));
                    if (held)
                        k += snprintf(what + k, sizeof(what) - k, " (%d more class changes in the 10 s before)",
                                      held);
                    last_speed = GetTickCount64();
                    held = 0;
                } else
                    held++;
                logged = cand;
            }
        }
        if (k) {
            char cpu[200], tt[160];
            game_text(g, sizeof(g));
            cpu_now_text(&s, cpu, sizeof(cpu));
            temp_text(&s, tt, sizeof(tt));
            snprintf(line, sizeof(line), "change: %s; %s; now %s, %s, %s", what, g, cpu, ac_name(s.ac), tt);
            out_line(line, 1);
        }
        if (tick % 10 == 0 && g_file != INVALID_HANDLE_VALUE) {   /* the file's own 10-s line */
            game_text(g, sizeof(g));
            k = snprintf(line, sizeof(line), "%s; ", g);
            k += source_text(&s, line + k, sizeof(line) - k);
            k += snprintf(line + k, sizeof(line) - k, "; ");
            detail_text(&s, &acc_f, line + k, sizeof(line) - k);
            out_line(line, 0);
            memset(&acc_f, 0, sizeof(acc_f));
        }
    }
}

void power_log_start(const power_log_hooks_t *hooks)
{
    static LONG started;
    const char *v = getenv("RECOMP_POWER_LOG");
    HANDLE t;

    if (InterlockedExchange(&started, 1))
        return;
    if (v && strcmp(v, "0") == 0) {
        fprintf(stderr, "[POWER] off (RECOMP_POWER_LOG=0)\n");
        return;
    }
    g_hooks = *hooks;
    if (v && *v) {
        g_file = CreateFileA(v, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
        fprintf(stderr, "[POWER] lines also to %s, written through to the disk%s\n", v,
                g_file == INVALID_HANDLE_VALUE ? ": NOT opened" : "");
    }
    t = CreateThread(NULL, 64 * 1024, sampler, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (t)
        CloseHandle(t);
    else
        fprintf(stderr, "[POWER] the sampler thread didn't start (%lu)\n", GetLastError());
}

void power_log_interval(char *buf, size_t cap)
{
    sample_t s;
    acc_t a;
    int have, n;

    AcquireSRWLockExclusive(&g_lock);
    have = g_have;
    s = g_now;
    a = g_acc_b;
    memset(&g_acc_b, 0, sizeof(g_acc_b));
    ReleaseSRWLockExclusive(&g_lock);
    if (!have) {
        snprintf(buf, cap, "not read yet (the power log's first reading comes a second after the start)");
        return;
    }
    n = source_text(&s, buf, cap);
    if (n < (int)cap)
        n += snprintf(buf + n, cap - n, "; ");
    if (n < (int)cap)
        detail_text(&s, &a, buf + n, cap - n);
}

void power_log_brief(char *buf, size_t cap)
{
    sample_t s;
    int have, n;
    char t[48];

    AcquireSRWLockExclusive(&g_lock);
    have = g_have;
    s = g_now;
    ReleaseSRWLockExclusive(&g_lock);
    if (!have) {
        snprintf(buf, cap, "%s", g_hooks.now_s ? "Power: not read yet" : "Power: not logged (RECOMP_POWER_LOG=0)");
        return;
    }
    if (s.cpu_ok)
        n = snprintf(buf, cap, "CPU %.0f MHz, %.0f%% of rated", s.mhz, s.perf_pct);
    else if (s.nt_max)
        n = snprintf(buf, cap, "CPU %u MHz, %.0f%% of rated", s.nt_cur, 100.0 * s.nt_cur / s.nt_max);
    else
        n = snprintf(buf, cap, "CPU speed not read");
    if (n < (int)cap) {
        if (s.ac == 0 && s.rate_mw < 0)
            n += snprintf(buf + n, cap - n, "\nBattery %.1f W", -s.rate_mw / 1000.0);
        else
            n += snprintf(buf + n, cap - n, "\n%s", s.ac == 1 ? "Plugged in" : s.ac == 0 ? "On battery" : "Power source unknown");
    }
    if (n < (int)cap)
        n += snprintf(buf + n, cap - n, "\nPower mode: %s", overlay_name(&s, t, sizeof(t)));
    if (s.temp_ok > 0 && n < (int)cap)
        snprintf(buf + n, cap - n, "\n%.0f C", s.temp_c);
}

static volatile LONG g_ms_fresh;

void power_log_game(double ups)
{
    g_ups = ups;
    if (!InterlockedExchange(&g_ms_fresh, 0))
        g_game_ms = g_render_ms = -1;   /* the interval before had no thread ms */
}

void power_log_thread_ms(double game_ms, double render_ms)
{
    g_game_ms = game_ms;
    g_render_ms = render_ms;
    InterlockedExchange(&g_ms_fresh, 1);
}
