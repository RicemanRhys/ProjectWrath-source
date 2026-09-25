/**
 * D3D mirror bridge (Option B, notes 18.31 and 18.32)
 *
 * The game's statically linked D3D keeps running as lifted code. recomp_manual.c
 * wraps the 15 entry points the bridge needs (plus a few census-only probes):
 * each wrapper calls the lifted body, sub_X_gen, and with RECOMP_D3D_BRIDGE on
 * also calls d3db_enter/d3db_leave around it. Nothing here writes guest memory.
 *
 * Two switches, both off by default:
 *
 * RECOMP_D3D_BRIDGE=1, the mirror (session S2, notes 18.33). CreateDevice
 * creates the host device and its window (d3d_host.c), Clear is mirrored for
 * the back buffer with the XDK flags mapped, and Swap presents. Clears to other
 * targets (the 128x128 render-to-texture ones) wait for S4. Every 10 s a
 * [D3DB] host line compares guest swaps with host presents. Each draw, clear
 * and swap also checks the game's D3D lock 0x4EA440 (see check_lock) and
 * counts the D3DX draws separately. RECOMP_D3D_BRIDGE_TINT=1
 * (a check, off by default) clears to the guest frame index instead of the
 * game's colour (R = k & 0xFF, G = k >> 8 & 0xFF, B = 0x80), so a host dump or
 * a look at the window shows which guest frame is on it.
 *
 * RECOMP_D3D_CENSUS=1, the census (session S1, step 30), with or without the
 * mirror: call counts, primitive types, vertex shaders and FVFs, texture
 * formats, the RS/TSS indices a draw sees change, render targets, locks and
 * copies, and the threads that draw. Every 10 s one block of [D3DB] c lines is
 * written, one per key seen in that interval:
 *
 *   [D3DB] c <seconds since start> <count in interval> <total> <key>
 *
 * Counts are calls for "call"/"draw"/"tid" keys, summed vertices for "verts"
 * keys, and changes (seen at a draw) for "rs"/"tss"/"tex"/"vs"/"rt"/... keys.
 * A few one-off events (CreateDevice's parameters, the first state-block
 * tokens, the first locks and copies) are printed as they happen.
 *
 * With both unset, d3db_on is 0 and the wrappers only call _gen.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <windows.h>

#define RECOMP_GENERATED_CODE
#include "recomp/gen/recomp_funcs.h"

#include "d3d_bridge.h"
#include "d3d_host.h"

/* ── Guest D3D layout (notes 18.31, step29_option_b_plan.md section 1) ── */

#define D3D_DEVICE_PTR      0x0010EBF0u     /* [ ] = &device (0x10C110) */
#define D3D_RENDERSTATE     0x0010EE18u     /* D3D__RenderState[146], XDK order */
#define D3D_TEXTURESTATE    0x0010EC18u     /* D3D__TextureState[4][32] */
#define D3D_STREAMS         0x0010F280u     /* 12 bytes per stream: stride +0, VB header +8 */
#define D3D_RS_COUNT        146
#define D3D_TSS_COUNT       32
#define D3D_STREAM_COUNT    16

#define DEV_FLAGS           0x008u
#define DEV_PIXEL_SHADER    0x370u
#define DEV_VS_HANDLE       0x384u
#define DEV_VIEWPORT        0x9D0u
#define DEV_TEXTURES        0xA78u
#define DEV_VBLANK_CB       0x2430u
#define DEV_RENDER_TARGET   0x2070u
#define DEV_DEPTH_SURFACE   0x2074u
#define DEV_BACK_BUFFER     0x207Cu
#define DEV_FRONT_BUFFER    0x2080u
#define DEV_THIRD_BUFFER    0x2084u

/* Resource header: Common, Data, Lock, Format, Size, Parent (surfaces). */
#define RES_COMMON          0x00u
#define RES_DATA            0x04u
#define RES_FORMAT          0x0Cu
#define RES_SIZE            0x10u
#define RES_PARENT          0x14u

#define GAME_D3D_LOCK       0x004EA440u     /* the game's CS around its D3D calls */
#define D3DX_LO             0x0010F5A0u     /* the D3DX library's code */
#define D3DX_HI             0x00130D58u
#define GAME_FRAME_COUNTER  0x0025A660u     /* one inc per main-loop pass (18.29) */
#define HOST_PERIOD_MS      10000

/* XDK D3DPRESENT_PARAMETERS, as dwords (18.32's CreateDevice line). */
#define PP_WIDTH            0
#define PP_HEIGHT           1
#define PP_INTERVAL         12              /* FullScreen_PresentationInterval */

int d3db_on;
static int d3db_census, d3db_host;
static int g_tint;                  /* RECOMP_D3D_BRIDGE_TINT: colour clears show the frame index */

static const char *const d3db_name[D3DB_COUNT] = {
    "CreateDevice", "Clear", "Swap", "DrawVerticesUP", "DrawVertices",
    "DrawIndexedVertices", "Begin", "SetVertexData2f", "SetVertexData4f", "End",
    "CopyRects", "SetVertexShaderConstant", "TextureLockRect", "SurfaceLockRect",
    "FastTexturePush",
    "probe_FD680", "probe_FEC90", "probe_VBlankCallback_39910",
    "probe_BeginStateBlock_1012CC", "probe_EndStateBlock_10191E",
    "ApplyStateBlock", "DeleteStateBlock", "probe_Dxt1NoiseEnable_FDC90",
    "probe_Splash_875A0",
};

/* ── Census counters: string key -> interval and total counts ── */

#define CEN_SLOTS    8192                   /* power of two */
#define CEN_KEY      96
#define CEN_PERIOD_MS 10000

typedef struct {
    char     key[CEN_KEY];                  /* "" = free */
    uint64_t interval, total;
} cen_slot_t;

static cen_slot_t g_cen[CEN_SLOTS];
static SRWLOCK g_cen_lock = SRWLOCK_INIT;
static ULONGLONG g_t0_ms, g_last_dump_ms;
static volatile LONG g_cen_full_logged;

static void cen_add(const char *key, uint64_t n)
{
    uint32_t h = 2166136261u, i;
    const char *p;

    for (p = key; *p; p++)
        h = (h ^ (uint8_t)*p) * 16777619u;
    AcquireSRWLockExclusive(&g_cen_lock);
    for (i = 0; i < CEN_SLOTS; i++) {
        cen_slot_t *s = &g_cen[(h + i) & (CEN_SLOTS - 1)];
        if (!s->key[0]) {
            if (i > CEN_SLOTS / 2)
                break;                      /* nearly full: drop new keys */
            strncpy(s->key, key, CEN_KEY - 1);
        } else if (strncmp(s->key, key, CEN_KEY - 1) != 0) {
            continue;
        }
        s->interval += n;
        s->total += n;
        ReleaseSRWLockExclusive(&g_cen_lock);
        return;
    }
    ReleaseSRWLockExclusive(&g_cen_lock);
    if (!InterlockedExchange(&g_cen_full_logged, 1))
        fprintf(stderr, "[D3DB] census table full; new keys dropped from here on\n");
}

static void cen_addf(uint64_t n, const char *fmt, ...)
{
    char key[CEN_KEY];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(key, sizeof(key), fmt, ap);
    va_end(ap);
    cen_add(key, n);
}

static double now_s(void)
{
    return (GetTickCount64() - g_t0_ms) / 1000.0;
}

/* One-off event line, at most `max` per tag counter. */
static void event(volatile LONG *counter, LONG max, const char *fmt, ...)
{
    va_list ap;

    if (InterlockedIncrement(counter) > max)
        return;
    fprintf(stderr, "[D3DB] +%.1f s ", now_s());
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* Every CEN_PERIOD_MS, from whichever thread gets there, and at each phase
 * mark: every key seen in the interval, sorted by key. The keys are copied
 * under the census lock and written after it, so no thread counting a key
 * waits on the CRT's stderr lock. */
static int cen_cmp(const void *a, const void *b)
{
    return strcmp(((const cen_slot_t *)a)->key, ((const cen_slot_t *)b)->key);
}

static void cen_dump(int force, const char *label)
{
    static cen_slot_t snap[CEN_SLOTS];
    static SRWLOCK dump_lock = SRWLOCK_INIT;
    ULONGLONG now = GetTickCount64();
    int n = 0, i;

    if (!force && now - g_last_dump_ms < CEN_PERIOD_MS)
        return;
    if (force)
        AcquireSRWLockExclusive(&dump_lock);
    else if (!TryAcquireSRWLockExclusive(&dump_lock))
        return;
    if (!force && now - g_last_dump_ms < CEN_PERIOD_MS) {
        ReleaseSRWLockExclusive(&dump_lock);
        return;
    }
    g_last_dump_ms = now;
    AcquireSRWLockExclusive(&g_cen_lock);
    for (i = 0; i < CEN_SLOTS; i++)
        if (g_cen[i].key[0] && g_cen[i].interval) {
            snap[n++] = g_cen[i];
            g_cen[i].interval = 0;
        }
    ReleaseSRWLockExclusive(&g_cen_lock);
    qsort(snap, n, sizeof(snap[0]), cen_cmp);
    fprintf(stderr, "[D3DB] census +%.1f s: %d keys this interval%s%s\n", (now - g_t0_ms) / 1000.0, n,
            label ? ", ends at mark: " : "", label ? label : "");
    for (i = 0; i < n; i++)
        fprintf(stderr, "[D3DB] c %.1f %llu %llu %s\n", (now - g_t0_ms) / 1000.0,
                (unsigned long long)snap[i].interval, (unsigned long long)snap[i].total, snap[i].key);
    fflush(stderr);
    ReleaseSRWLockExclusive(&dump_lock);
}

/* ── Reading D3D's own state ────────────────────────────────── */

static uint32_t dev(void)
{
    return MEM32(D3D_DEVICE_PTR);
}

static uint32_t res_data(uint32_t hdr)
{
    return hdr ? MEM32(hdr + RES_DATA) : 0;
}

/* "fmt=XX WxH lv=N [cube]" from a texture or surface header. Swizzled
 * formats carry log2 sizes in Format; linear ones carry Size. */
static void describe_format(uint32_t hdr, char *out, size_t cap)
{
    uint32_t f, sz, w, h;

    if (!hdr) {
        snprintf(out, cap, "null");
        return;
    }
    f = MEM32(hdr + RES_FORMAT);
    sz = MEM32(hdr + RES_SIZE);
    if (sz) {
        w = (sz & 0xFFF) + 1;
        h = ((sz >> 12) & 0xFFF) + 1;
    } else {
        w = 1u << ((f >> 20) & 15);
        h = 1u << ((f >> 24) & 15);
    }
    snprintf(out, cap, "fmt=%02X %ux%u lv=%u%s%s type=%u", (f >> 8) & 0xFF, w, h,
             (f >> 16) & 15, (f & 4) ? " cube" : "", sz ? " linear" : "",
             (MEM32(hdr + RES_COMMON) >> 16) & 7);
}

/* Which surface a header is, by Data: the back/front/third buffer, a texture
 * level (it has a Parent), or something else. */
static const char *surface_kind(uint32_t hdr)
{
    uint32_t d = dev(), data;

    if (!hdr)
        return "null";
    data = res_data(hdr);
    if (data == res_data(MEM32(d + DEV_BACK_BUFFER)))  return "back";
    if (data == res_data(MEM32(d + DEV_FRONT_BUFFER))) return "front";
    if (data == res_data(MEM32(d + DEV_THIRD_BUFFER))) return "third";
    if (MEM32(hdr + RES_PARENT))                       return "texlevel";
    return "other";
}

/* Render-target Data values seen so far, so a lock or copy can tell a
 * readback of rendered pixels. */
#define RT_SEEN_MAX 64
static uint32_t g_rt_seen[RT_SEEN_MAX];
static volatile LONG g_rt_seen_n;

static int rt_seen(uint32_t data)
{
    LONG i, n = g_rt_seen_n;
    for (i = 0; i < n && i < RT_SEEN_MAX; i++)
        if (g_rt_seen[i] == data)
            return 1;
    return 0;
}

static void rt_remember(uint32_t data)
{
    LONG i;
    if (!data || rt_seen(data))
        return;
    i = InterlockedIncrement(&g_rt_seen_n) - 1;
    if (i < RT_SEEN_MAX)
        g_rt_seen[i] = data;
}

/* ── Draw-time scrape: what a draw sees, counted per change ──── */

/* The state the bridge will translate at each draw (design point 6). S1 only
 * notes which of it changes between draws. All draws come from one thread
 * (checked by the "tid" keys), and so does this. */
static struct {
    int      valid;
    uint32_t rs[D3D_RS_COUNT];
    uint32_t tss[4][D3D_TSS_COUNT];
    uint32_t tex[4], texfmt[4];
    uint32_t vs, ps, rt, zs, streams, stride0;
    uint32_t vp[6];
} g_last;

static void snapshot_defaults(void)
{
    int i, s;
    for (i = 0; i < D3D_RS_COUNT; i++)
        g_last.rs[i] = MEM32(D3D_RENDERSTATE + 4 * i);
    for (s = 0; s < 4; s++)
        for (i = 0; i < D3D_TSS_COUNT; i++)
            g_last.tss[s][i] = MEM32(D3D_TEXTURESTATE + 4 * (s * D3D_TSS_COUNT + i));
    g_last.valid = 1;
}

/* Fast-path texture pushes (0xFEC00) since the stage was last bound through
 * SetTexture: the Data it pushed, per stage. */
static uint32_t g_fast_data[4];

static void scrape(const char *path, uint32_t prim, uint32_t verts)
{
    uint32_t d = dev(), v, i, s;
    char desc[80];

    if (!g_last.valid)
        snapshot_defaults();

    cen_addf(1, "draw %s prim=%u", path, prim);
    cen_addf(verts, "verts %s", path);
    cen_addf(1, "tid draw %lu", GetCurrentThreadId());

    for (i = 0; i < D3D_RS_COUNT; i++) {
        v = MEM32(D3D_RENDERSTATE + 4 * i);
        if (v != g_last.rs[i]) {
            g_last.rs[i] = v;
            cen_addf(1, "rs %03u=%08X", i, v);
        }
    }
    for (s = 0; s < 4; s++)
        for (i = 0; i < D3D_TSS_COUNT; i++) {
            v = MEM32(D3D_TEXTURESTATE + 4 * (s * D3D_TSS_COUNT + i));
            if (v != g_last.tss[s][i]) {
                g_last.tss[s][i] = v;
                cen_addf(1, "tss %u.%02u=%08X", s, i, v);
            }
        }
    for (s = 0; s < 4; s++) {
        uint32_t t = MEM32(d + DEV_TEXTURES + 4 * s);
        uint32_t tf = t ? MEM32(t + RES_FORMAT) : 0;
        if (t != g_last.tex[s] || tf != g_last.texfmt[s]) {
            g_last.tex[s] = t;
            g_last.texfmt[s] = tf;
            describe_format(t, desc, sizeof(desc));
            cen_addf(1, "tex %u %s", s, desc);
        }
        if (t && g_fast_data[s] && g_fast_data[s] != res_data(t))
            cen_addf(1, "draw fastpath-overrides-stage %u", s);
    }
    cen_addf(1, "draw textures=%u%u%u%u", !!g_last.tex[0], !!g_last.tex[1], !!g_last.tex[2], !!g_last.tex[3]);

    v = MEM32(d + DEV_VS_HANDLE);
    if (v != g_last.vs) {
        g_last.vs = v;
        if (v & 1)
            cen_addf(1, "vs program obj=%08X", v - 1);
        else
            cen_addf(1, "vs fvf=%08X", v);
    }
    cen_addf(1, "draw vs=%s", (MEM32(d + DEV_VS_HANDLE) & 1) ? "program" : "fvf");
    v = MEM32(d + DEV_PIXEL_SHADER);
    if (v != g_last.ps) {
        g_last.ps = v;
        cen_addf(1, "ps %s", v ? "set" : "none");
    }
    cen_addf(1, "draw ps=%s", v ? "set" : "none");

    v = MEM32(d + DEV_RENDER_TARGET);
    if (v != g_last.rt) {
        g_last.rt = v;
        rt_remember(res_data(v));
        describe_format(v, desc, sizeof(desc));
        cen_addf(1, "rt %s data=%08X %s", surface_kind(v), res_data(v), desc);
    }
    cen_addf(1, "draw rt=%s", surface_kind(v));
    v = MEM32(d + DEV_DEPTH_SURFACE);
    if (v != g_last.zs) {
        g_last.zs = v;
        describe_format(v, desc, sizeof(desc));
        cen_addf(1, "zs data=%08X %s", res_data(v), desc);
    }

    for (i = 0, v = 0; i < 6; i++)
        if (MEM32(d + DEV_VIEWPORT + 4 * i) != g_last.vp[i]) {
            g_last.vp[i] = MEM32(d + DEV_VIEWPORT + 4 * i);
            v = 1;
        }
    if (v)
        cen_addf(1, "viewport %u,%u %ux%u z=%08X..%08X",
                 g_last.vp[0], g_last.vp[1], g_last.vp[2], g_last.vp[3], g_last.vp[4], g_last.vp[5]);
}

/* Streams matter only to the stream-fetch paths. */
static void scrape_streams(const char *path)
{
    uint32_t mask = 0, i;
    for (i = 0; i < D3D_STREAM_COUNT; i++)
        if (MEM32(D3D_STREAMS + 12 * i + 8))
            mask |= 1u << i;
    cen_addf(1, "streams %s mask=%04X stride0=%u", path, mask, MEM32(D3D_STREAMS));
}

/* ── Wrapper hooks ──────────────────────────────────────────── */

/* Immediate mode: vertices (SetVertexData4f to register -1, which launches
 * one) between Begin and End. */
static uint32_t g_imm_prim, g_imm_verts;
static volatile LONG g_ev_cd, g_ev_sb, g_ev_lock, g_ev_copy, g_ev_vsc, g_ev_fast;

/* Live state-block tokens: returned by EndStateBlock and not yet deleted.
 * A token is a pointer to the block, so 0 marks a free slot and 1 a deleted
 * one. */
#define SB_SLOTS 16384
static uint32_t g_sb_live[SB_SLOTS];
static SRWLOCK g_sb_lock = SRWLOCK_INIT;

static uint32_t *sb_slot(uint32_t token, int insert)
{
    uint32_t i, h = (token >> 2) * 2654435761u;
    uint32_t *tomb = NULL;

    for (i = 0; i < SB_SLOTS; i++) {
        uint32_t *s = &g_sb_live[(h + i) & (SB_SLOTS - 1)];
        if (*s == token)
            return s;
        if (*s == 1 && !tomb)
            tomb = s;
        if (*s == 0)
            return insert ? (tomb ? tomb : s) : NULL;
    }
    return insert ? tomb : NULL;
}

/* op: 0 look up, 1 add, 2 remove. Returns whether the token was live. */
static int sb_live(uint32_t token, int op)
{
    uint32_t *s;
    int was;

    if (token < 2)
        return 0;
    AcquireSRWLockExclusive(&g_sb_lock);
    s = sb_slot(token, op == 1);
    was = s && *s == token;
    if (op == 1 && s)
        *s = token;
    else if (op == 2 && was)
        *s = 1;
    ReleaseSRWLockExclusive(&g_sb_lock);
    return was;
}

static int env_on(const char *name)
{
    const char *v = getenv(name);
    return v && *v && strcmp(v, "0") != 0;
}

void d3db_init(void)
{
    d3db_host = env_on("RECOMP_D3D_BRIDGE");
    d3db_census = env_on("RECOMP_D3D_CENSUS");
    d3db_on = d3db_host || d3db_census;
    g_tint = d3db_host && env_on("RECOMP_D3D_BRIDGE_TINT");
    if (d3db_on)
        coop_watch_cs(GAME_D3D_LOCK);
    g_t0_ms = g_last_dump_ms = GetTickCount64();
    if (d3db_host)
        fprintf(stderr, "[D3DB] bridge on: host mirror (S2: device, window, Clear, Swap)%s\n",
                g_tint ? "; colour clears tinted with the frame index" : "");
    if (d3db_census)
        fprintf(stderr, "[D3DB] census on: a [D3DB] c block every %d s\n", CEN_PERIOD_MS / 1000);
}

/* ── The mirror (S2) ────────────────────────────────────────── */

static volatile LONG g_swaps, g_clears_other_rt;
static volatile LONG g_nolock, g_d3dx_draws, g_interleaved;
static volatile LONG g_ev_d3dx, g_ev_clear, g_ev_interleave;
static ULONGLONG g_last_host_ms;

/* Per thread: draws, clears and swaps, and how many had the game's lock. */
#define LOCK_THREADS 16
static struct {
    volatile LONG tid, calls, locked;
} g_lock_by_thread[LOCK_THREADS];

static void count_thread(unsigned long tid, int locked)
{
    int i;
    for (i = 0; i < LOCK_THREADS; i++) {
        LONG t = g_lock_by_thread[i].tid;
        if (t == 0)
            t = InterlockedCompareExchange(&g_lock_by_thread[i].tid, (LONG)tid, 0);
        if (t == 0 || t == (LONG)tid) {
            InterlockedIncrement(&g_lock_by_thread[i].calls);
            if (locked)
                InterlockedIncrement(&g_lock_by_thread[i].locked);
            return;
        }
    }
}

/* The DXGI sync interval: 0 while the executor paces the game, else the
 * game's own (XDK D3DPRESENT_INTERVAL_*). RECOMP_D3D_BRIDGE_INTERVAL
 * overrides both. */
static unsigned present_interval(uint32_t xdk)
{
    const char *v = getenv("RECOMP_D3D_BRIDGE_INTERVAL");

    if (v && *v)
        return (unsigned)strtoul(v, NULL, 0);
    if (env_on("RECOMP_PB_EXEC"))
        return 0;
    switch (xdk) {
    case 0x80000000u: return 0;         /* IMMEDIATE */
    case 2:           return 2;
    case 4:           return 3;
    case 8:           return 4;
    default:          return 1;         /* DEFAULT, ONE */
    }
}

/* -DumpDir sets RECOMP_FB_DUMP to "<dir>\shot" for the executor's dumps; the
 * host's go next to them as "<dir>\host". */
static const char *host_dump_prefix(char *buf, size_t cap)
{
    const char *p = getenv("RECOMP_FB_DUMP");
    const char *slash;
    size_t n;

    if (!p || !*p)
        return NULL;
    slash = strrchr(p, '\\');
    if (!slash || (strrchr(p, '/') && strrchr(p, '/') > slash))
        slash = strrchr(p, '/');
    n = slash ? (size_t)(slash - p) + 1 : 0;
    snprintf(buf, cap, "%.*shost", (int)n, p);
    return buf;
}

static void mirror_create_device(const d3db_args_t *a)
{
    uint32_t pp = a->arg[4];
    char prefix[512];

    if (eax != 0 || !pp) {
        fprintf(stderr, "[D3DB] guest CreateDevice returned %08X: no host device\n", eax);
        return;
    }
    d3dh_create(MEM32(pp + 4 * PP_WIDTH), MEM32(pp + 4 * PP_HEIGHT),
                present_interval(MEM32(pp + 4 * PP_INTERVAL)),
                host_dump_prefix(prefix, sizeof(prefix)));
}

/* (Count, pRects, Flags, Color, Z, Stencil). The census never saw rects. */
static void mirror_clear(const d3db_args_t *a)
{
    uint32_t rt = MEM32(dev() + DEV_RENDER_TARGET);
    float z;

    if (strcmp(surface_kind(rt), "back") != 0) {
        InterlockedIncrement(&g_clears_other_rt);   /* render-to-texture: S4 */
        return;
    }
    memcpy(&z, &a->arg[4], 4);
    event(&g_ev_clear, 12, "Clear flags %X colour %08X z %g stencil %u -> host", a->arg[2], a->arg[3],
          (double)z, a->arg[5]);
    if (g_tint) {                   /* frame k (0-based): R = k & 0xFF, G = k >> 8 & 0xFF, B = 0x80 */
        uint32_t k = (uint32_t)g_swaps;
        d3dh_clear(a->arg[2], 0xFF000080u | ((k & 0xFF) << 16) | (((k >> 8) & 0xFF) << 8), z, a->arg[5]);
        return;
    }
    d3dh_clear(a->arg[2], a->arg[3], z, a->arg[5]);
}

static void mirror_swap(void)
{
    LONG n = InterlockedIncrement(&g_swaps);
    ULONGLONG now;

    d3dh_present(now_s(), (unsigned)n);
    now = GetTickCount64();
    if (now - g_last_host_ms >= HOST_PERIOD_MS) {
        static SRWLOCK line_lock = SRWLOCK_INIT;
        static LONG last_swaps;
        static unsigned last_presents;
        d3dh_stats_t st;
        if (!TryAcquireSRWLockExclusive(&line_lock))
            return;
        if (now - g_last_host_ms >= HOST_PERIOD_MS) {
            double dt = g_last_host_ms ? (now - g_last_host_ms) / 1000.0 : now_s();
            d3dh_stats(&st);
            fprintf(stderr, "[D3DB] host +%.1f s: guest swaps %ld (+%ld, %.1f/s), host presents %u (+%u),"
                    " frame counter %u; clears %u (colour %u, z/stencil %u, partial colour %u),"
                    " other-target clears %ld; no game lock %ld (%ld while another thread held it),"
                    " D3DX draws %ld; dumps %u, present fails %u, other status %u\n",
                    now_s(), (long)n, (long)(n - last_swaps), (n - last_swaps) / dt,
                    st.presents, st.presents - last_presents, MEM32(GAME_FRAME_COUNTER),
                    st.clears, st.clears_colour, st.clears_depth, st.clears_partial_colour,
                    (long)g_clears_other_rt, (long)g_nolock, (long)g_interleaved, (long)g_d3dx_draws,
                    st.dumps, st.present_fails, st.present_status);
            {
                char buf[512];
                int i, len = 0;
                for (i = 0; i < LOCK_THREADS && g_lock_by_thread[i].tid && len < (int)sizeof(buf) - 40; i++)
                    len += snprintf(buf + len, sizeof(buf) - len, " %ld:%ld/%ld",
                                    (long)g_lock_by_thread[i].tid, (long)g_lock_by_thread[i].locked,
                                    (long)g_lock_by_thread[i].calls);
                fprintf(stderr, "[D3DB] host threads (tid:with game lock/calls):%s\n", len ? buf : " none");
            }
            fflush(stderr);
            last_swaps = n;
            last_presents = st.presents;
            g_last_host_ms = now;
        }
        ReleaseSRWLockExclusive(&line_lock);
    }
}

/* Threads and locks (18.33). A loading screen's thread holds the game's D3D
 * lock 0x4EA440 around all of its D3D calls; the main thread takes it only
 * for some of its own (state blocks, resources). So an unlocked call is
 * normal and only counted. What would break native D3D (and the mirror's
 * reads of its state) is an unlocked call while another thread holds the
 * lock: that is counted and logged. D3DX's draws (vtable methods 0x11127A,
 * 0x11199E) are counted by return address; none ran from splash to hub. */
static void check_lock(int id, const d3db_args_t *a)
{
    int d3dx = a->ret >= D3DX_LO && a->ret < D3DX_HI;
    int locked = coop_holds_cs(GAME_D3D_LOCK);

    count_thread(a->tid, locked);
    if (d3dx) {
        InterlockedIncrement(&g_d3dx_draws);
        event(&g_ev_d3dx, 10, "D3DX %s from %08X, thread %lu, game D3D lock %s", d3db_name[id], a->ret,
              a->tid, locked ? "held" : "NOT held");
    }
    if (!locked && coop_cs_holders() > 0) {
        /* Another thread (a loading screen) is inside its locked D3D section
         * right now: native D3D itself would be interleaved. */
        InterlockedIncrement(&g_interleaved);
        event(&g_ev_interleave, 20, "%s from %08X on thread %lu while another thread holds the game's D3D lock",
              d3db_name[id], a->ret, a->tid);
    }
    if (!locked)
        InterlockedIncrement(&g_nolock);
}

void d3db_enter(int id, d3db_args_t *a)
{
    int i;

    a->ecx = ecx;
    a->edx = edx;
    for (i = 0; i < 6; i++)
        a->arg[i] = MEM32(esp + 4 + 4 * i);
    a->ret = MEM32(esp);
    a->tid = GetCurrentThreadId();

    switch (id) {
    case D3DB_CLEAR: case D3DB_SWAP: case D3DB_DRAW_UP: case D3DB_DRAW:
    case D3DB_DRAW_INDEXED: case D3DB_BEGIN: case D3DB_END: case D3DB_COPYRECTS:
        check_lock(id, a);
        break;
    }
    if (!d3db_census)
        return;

    cen_addf(1, "call %s", d3db_name[id]);
    cen_addf(1, "tid %s %lu", d3db_name[id], a->tid);

    switch (id) {
    case D3DB_PROBE_SPLASH:
        d3db_mark("splash routine sub_000875A0 entered");
        break;
    case D3DB_DRAW_UP:              /* (Prim, VertexCount, pData, Stride) */
        scrape("UP", a->arg[0], a->arg[1]);
        cen_addf(1, "stride UP %u", a->arg[3]);
        break;
    case D3DB_DRAW:                 /* (Prim, StartVertex, VertexCount) */
        scrape("Vertices", a->arg[0], a->arg[2]);
        scrape_streams("Vertices");
        break;
    case D3DB_DRAW_INDEXED:         /* (Prim, IndexCount, pIndexData) */
        scrape("Indexed", a->arg[0], a->arg[1]);
        scrape_streams("Indexed");
        break;
    case D3DB_BEGIN:                /* (Prim) */
        g_imm_prim = a->arg[0];
        g_imm_verts = 0;
        break;
    case D3DB_DATA2F:               /* (Register, a, b) */
        cen_addf(1, "imm 2f reg=%d", (int)a->arg[0]);
        break;
    case D3DB_DATA4F:               /* (Register, a, b, c, d); -1 = VERTEX4F */
        if (a->arg[0] == 0xFFFFFFFFu)
            g_imm_verts++;
        cen_addf(1, "imm 4f reg=%d", (int)a->arg[0]);
        break;
    case D3DB_END:
        scrape("Immediate", g_imm_prim, g_imm_verts);
        break;
    case D3DB_CLEAR: {              /* (Count, pRects, Flags, Color, Z, Stencil) */
        uint32_t rt = MEM32(dev() + DEV_RENDER_TARGET);
        rt_remember(res_data(rt));
        cen_addf(1, "clear flags=%X rects=%u rt=%s", a->arg[2], a->arg[0], surface_kind(rt));
        break;
    }
    case D3DB_SWAP:                 /* (Flags) */
        cen_addf(1, "swap flags=%X", a->arg[0]);
        break;
    case D3DB_COPYRECTS: {          /* (pSrc, pSrcRects, cRects, pDst, pDstPoints) */
        char sd[80], dd[80];
        describe_format(a->arg[0], sd, sizeof(sd));
        describe_format(a->arg[3], dd, sizeof(dd));
        cen_addf(1, "copyrects src=%s%s dst=%s rects=%u", surface_kind(a->arg[0]),
                 rt_seen(res_data(a->arg[0])) ? "(rendered)" : "", surface_kind(a->arg[3]), a->arg[2]);
        event(&g_ev_copy, 20, "CopyRects src %08X (%s, %s, data %08X%s) rects %08X x%u -> dst %08X (%s, %s, data %08X) points %08X",
              a->arg[0], surface_kind(a->arg[0]), sd, res_data(a->arg[0]),
              rt_seen(res_data(a->arg[0])) ? ", a render target" : "",
              a->arg[1], a->arg[2], a->arg[3], surface_kind(a->arg[3]), dd, res_data(a->arg[3]), a->arg[4]);
        break;
    }
    case D3DB_VS_CONSTANT:          /* (Register, pConstantData, ConstantCount) */
        cen_addf(1, "vsc reg=%d n=%u skipshadow=%u", (int)a->arg[0], a->arg[2],
                 (MEM32(dev() + DEV_FLAGS) >> 4) & 1);
        event(&g_ev_vsc, 10, "SetVertexShaderConstant reg %d count %u data %08X, device flags %08X",
              (int)a->arg[0], a->arg[2], a->arg[1], MEM32(dev() + DEV_FLAGS));
        break;
    case D3DB_TEX_LOCK: {           /* (pTexture, Level, pLockedRect, pRect, Flags) */
        char td[80];
        describe_format(a->arg[0], td, sizeof(td));
        cen_addf(1, "lock texture %s level=%u flags=%X%s", td, a->arg[1], a->arg[4],
                 rt_seen(res_data(a->arg[0])) ? " RENDERED" : "");
        event(&g_ev_lock, 40, "LockRect texture %08X (%s, data %08X) level %u rect %08X flags %X",
              a->arg[0], td, res_data(a->arg[0]), a->arg[1], a->arg[3], a->arg[4]);
        break;
    }
    case D3DB_SURF_LOCK: {          /* (pSurface, pLockedRect, pRect, Flags) */
        char sd[80];
        describe_format(a->arg[0], sd, sizeof(sd));
        cen_addf(1, "lock surface %s %s flags=%X%s", surface_kind(a->arg[0]), sd, a->arg[3],
                 rt_seen(res_data(a->arg[0])) ? " RENDERED" : "");
        event(&g_ev_lock, 40, "LockRect surface %08X (%s, %s, data %08X) rect %08X flags %X",
              a->arg[0], surface_kind(a->arg[0]), sd, res_data(a->arg[0]), a->arg[2], a->arg[3]);
        break;
    }
    case D3DB_FAST_TEXTURE: {       /* __fastcall (ecx = push header, edx, [esp+4]) */
        uint32_t method = a->ecx & 0x1FFC, count = (a->ecx >> 18) & 0x7FF;
        if (method >= 0x1B00 && method < 0x1C00 && (method & 0x3F) == 0 && count == 2) {
            uint32_t stage = (method - 0x1B00) >> 6;
            g_fast_data[stage & 3] = a->edx;
            cen_addf(1, "fastpath stage=%u fmt=%02X", stage, (a->arg[0] >> 8) & 0xFF);
        } else {
            cen_addf(1, "fastpath other header=%08X", a->ecx);
        }
        event(&g_ev_fast, 5, "fast texture push header %08X, %08X, %08X", a->ecx, a->edx, a->arg[0]);
        break;
    }
    case D3DB_APPLY_SB: {           /* (Token) */
        int live = sb_live(a->arg[0], 0);
        cen_addf(1, "sb apply live=%d", live);
        event(&g_ev_sb, 30, "ApplyStateBlock token %08X (%s)", a->arg[0],
              live ? "returned by EndStateBlock, not deleted" : "NOT a live EndStateBlock token");
        break;
    }
    case D3DB_DELETE_SB: {          /* (Token) */
        int live = sb_live(a->arg[0], 2);
        cen_addf(1, "sb delete live=%d", live);
        event(&g_ev_sb, 30, "DeleteStateBlock token %08X (%s)", a->arg[0],
              live ? "returned by EndStateBlock, not deleted" : "NOT a live EndStateBlock token");
        break;
    }
    }
}

void d3db_leave(int id, d3db_args_t *a)
{
    if (d3db_host) {
        switch (id) {
        case D3DB_CREATE_DEVICE: mirror_create_device(a); break;
        case D3DB_CLEAR:         mirror_clear(a);         break;
        case D3DB_SWAP:          mirror_swap();           break;
        }
    }
    if (!d3db_census)
        return;

    switch (id) {
    case D3DB_CREATE_DEVICE: {      /* (Adapter, DeviceType, hFocusWindow, Behavior, pPP, ppDevice) */
        uint32_t pp = a->arg[4], i;
        event(&g_ev_cd, 4, "CreateDevice(adapter %u, type %u, window %08X, behavior %08X, pp %08X, out %08X) = %08X, device %08X",
              a->arg[0], a->arg[1], a->arg[2], a->arg[3], pp, a->arg[5], eax,
              a->arg[5] ? MEM32(a->arg[5]) : 0);
        if (pp)
            for (i = 0; i < 16; i += 4)
                event(&g_ev_cd, 8, "  pp[%2u..]: %08X %08X %08X %08X", i,
                      MEM32(pp + 4 * i), MEM32(pp + 4 * i + 4), MEM32(pp + 4 * i + 8), MEM32(pp + 4 * i + 12));
        snapshot_defaults();
        break;
    }
    case D3DB_PROBE_FD680:          /* (1 arg) */
        cen_addf(1, "probe FD680 arg=%X ret=%X", a->arg[0], eax);
        break;
    case D3DB_PROBE_FEC90:          /* (1 arg) */
        cen_addf(1, "probe FEC90 arg=%08X device+2430=%08X", a->arg[0], MEM32(dev() + DEV_VBLANK_CB));
        break;
    case D3DB_PROBE_VBLANK_CB:
        break;                      /* the call and tid keys are the census */
    case D3DB_BEGIN_SB:
        cen_addf(1, "sb begin recording=%u", (MEM32(dev() + DEV_FLAGS) >> 5) & 1);
        break;
    case D3DB_END_SB: {             /* (pToken) */
        uint32_t token = a->arg[0] ? MEM32(a->arg[0]) : 0;
        cen_addf(1, "sb end recording=%u ret=%X", (MEM32(dev() + DEV_FLAGS) >> 5) & 1, eax);
        if (sb_live(token, 1))
            cen_addf(1, "sb end token-already-live");
        event(&g_ev_sb, 30, "EndStateBlock -> token %08X (count dword %u), ret %X", token,
              token ? MEM32(token) : 0, eax);
        break;
    }
    case D3DB_PROBE_DXT1NOISE:      /* (Value) */
        cen_addf(1, "probe FDC90 value=%X rs139=%X", a->arg[0], MEM32(D3D_RENDERSTATE + 4 * 139));
        break;
    case D3DB_SWAP:
        cen_addf(1, "swap ret=%s", eax ? "nonzero" : "0");
        break;
    }
    if (id == D3DB_PROBE_SPLASH)
        d3db_mark("splash routine sub_000875A0 returned");
    cen_dump(0, NULL);
}

/* A phase boundary (the splash routine, the main menu): close the current
 * census interval here so each phase gets blocks of its own. */
void d3db_mark(const char *label)
{
    if (!d3db_census)
        return;
    fprintf(stderr, "[D3DB] +%.1f s mark: %s\n", now_s(), label);
    cen_dump(1, label);
}
