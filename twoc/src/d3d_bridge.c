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
 * [D3DB] host line compares guest swaps with host presents.
 *
 * S3 (notes 18.34) adds DrawVerticesUP: after the lifted call, the draw is
 * repeated through xbox_d3d8 with the state read from D3D's shadows and
 * translated from XDK to PC numbering and values (translate_rs/_tss, xl_*).
 * Textures are described here (format, size, levels, a host pointer to the
 * bytes and a content signature) and cached and uploaded in d3d_host.c. A draw
 * to another target, with a vertex program or pixel shader, or with a cube or
 * palette texture isn't mirrored yet; each kind is counted on a [D3DB] host
 * draws line, and so is each XDK value the translator couldn't map. Each draw, clear
 * and swap also checks the game's D3D lock 0x4EA440 (see check_lock) and
 * counts the D3DX draws separately. RECOMP_D3D_BRIDGE_TINT=1
 * (a check, off by default) clears to the guest frame index instead of the
 * game's colour (R = k & 0xFF, G = k >> 8 & 0xFF, B = 0x80), so a host dump or
 * a look at the window shows which guest frame is on it.
 *
 * S4 part 1 (notes 18.35) adds render-to-texture, pixel shaders and P8:
 *   - A draw or clear whose render target is a texture level (the title's
 *     128x128 A8R8G8B8 targets) goes to a host render target made for that
 *     Data (describe_target, d3d_host.c), and a texture with the same Data
 *     samples it. No guest depth surface means no depth or stencil test.
 *   - A pixel shader is read from D3D__RenderState[0..56] and RS[117], where
 *     SetPixelShader copies the def, and re-encoded for xbox_d3d8's combiner
 *     translator, whose field order differs from the XDK's (translate_ps).
 *     What it can't express is counted per kind ("pixel-shader gaps").
 *   - A P8 texture is described with its stage's palette (device +0xA88).
 *   - MIPFILTER NONE gives the host level 0 alone (xbox_d3d8 still mips).
 *   - Every draw path is counted on the [D3DB] host draws line; indexed,
 *     immediate and DrawVertices draws aren't mirrored yet.
 * Three checks, off by default: RECOMP_D3D_BRIDGE_PSPROBE (draw each pixel
 * shader once with fixed inputs; tools/diag/pscheck.py), _TEXCHECK (write
 * each P8 texture as uploaded) and _RTLOG (log render-target draws in full;
 * tools/diag/rtcheck.py). With -DumpDir, _DUMP_RT writes the host render
 * targets with each host dump.
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
static int g_point;                 /* RECOMP_D3D_BRIDGE_POINT: point sampling (a check, S3) */
static int g_psprobe;               /* RECOMP_D3D_BRIDGE_PSPROBE: probe each pixel shader (a check, S4) */
static int g_texcheck;              /* RECOMP_D3D_BRIDGE_TEXCHECK: write each P8 texture (a check, S4) */
static int g_rtlog;                 /* RECOMP_D3D_BRIDGE_RTLOG: log render-target draws in full (a check, S4) */
static char g_check_prefix[512];    /* where the checks write: "<dump dir>\host", as the host dumps */

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
    /* S4: the whole shape of each draw in one key, so a phase's draws can be
     * sorted by what the mirror needs (target, vertex program, pixel shader,
     * texture formats per stage; "--" for no texture). */
    {
        char fm[4][3];
        for (s = 0; s < 4; s++)
            snprintf(fm[s], sizeof(fm[s]), g_last.tex[s] ? "%02X" : "--", (g_last.texfmt[s] >> 8) & 0xFF);
        cen_addf(1, "combo %s prim=%u rt=%s vs=%s ps=%s tex=%s.%s.%s.%s", path, prim,
                 surface_kind(MEM32(d + DEV_RENDER_TARGET)), (MEM32(d + DEV_VS_HANDLE) & 1) ? "prog" : "fvf",
                 MEM32(d + DEV_PIXEL_SHADER) ? "set" : "none", fm[0], fm[1], fm[2], fm[3]);
    }

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
    g_point = d3db_host && env_on("RECOMP_D3D_BRIDGE_POINT");
    g_psprobe = d3db_host && env_on("RECOMP_D3D_BRIDGE_PSPROBE");
    g_texcheck = d3db_host && env_on("RECOMP_D3D_BRIDGE_TEXCHECK");
    g_rtlog = d3db_host && env_on("RECOMP_D3D_BRIDGE_RTLOG");
    if (d3db_on)
        coop_watch_cs(GAME_D3D_LOCK);
    g_t0_ms = g_last_dump_ms = GetTickCount64();
    if (d3db_host)
        fprintf(stderr, "[D3DB] bridge on: host mirror (S4 part 1: device, window, Clear, Swap, DrawVerticesUP,"
                " render targets, pixel shaders, P8)%s%s%s\n", g_tint ? "; colour clears tinted with the frame index" : "",
                g_psprobe ? "; pixel-shader probe" : "", g_texcheck ? "; P8 texture check" : "");
    if (d3db_census)
        fprintf(stderr, "[D3DB] census on: a [D3DB] c block every %d s\n", CEN_PERIOD_MS / 1000);
}

/* ── The mirror (S2) ────────────────────────────────────────── */

static volatile LONG g_swaps, g_clears_other_rt;
static volatile LONG g_nolock, g_d3dx_draws, g_interleaved;
static volatile LONG g_ev_d3dx, g_ev_clear, g_ev_clear_rt, g_ev_interleave;
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
    if (host_dump_prefix(prefix, sizeof(prefix)))
        snprintf(g_check_prefix, sizeof(g_check_prefix), "%s", prefix);
}

static int describe_target(d3dh_target_t *t, int *z_off);

/* (Count, pRects, Flags, Color, Z, Stencil). The census never saw rects.
 * S4: a render-to-texture target is cleared on its host render target; the
 * tint check applies to the back buffer only. */
static void mirror_clear(const d3db_args_t *a)
{
    d3dh_target_t t;
    int z_off;
    float z;

    if (describe_target(&t, &z_off) != 0) {
        InterlockedIncrement(&g_clears_other_rt);   /* a target the host can't make */
        return;
    }
    memcpy(&z, &a->arg[4], 4);
    event(t.data ? &g_ev_clear_rt : &g_ev_clear, 12, "Clear flags %X colour %08X z %g stencil %u, target %08X -> host",
          a->arg[2], a->arg[3], (double)z, a->arg[5], t.data);
    if (g_rtlog && t.data)
        fprintf(stderr, "[D3DB] rtclear target %08X flags %X colour %08X\n", t.data, a->arg[2], a->arg[3]);
    if (g_tint && !t.data) {        /* frame k (0-based): R = k & 0xFF, G = k >> 8 & 0xFF, B = 0x80 */
        uint32_t k = (uint32_t)g_swaps;
        d3dh_clear(&t, a->arg[2], 0xFF000080u | ((k & 0xFF) << 16) | (((k >> 8) & 0xFF) << 8), z, a->arg[5]);
        return;
    }
    d3dh_clear(&t, a->arg[2], a->arg[3], z, a->arg[5]);
}

/* ── Draws (S3): XDK state → xbox_d3d8 ─────────────────────── */

/* xbox_d3d8 takes PC D3D8 numbering and values; D3D keeps XDK ones in its
 * shadows (18.31). XDK render-state indices, in the D3D__RenderState order
 * (0-56 pixel shader, 57-81 simple, 82-116 deferred, 117-145 complex): */
enum {
    XRS_ZFUNC = 57, XRS_ALPHAFUNC, XRS_ALPHABLENDENABLE, XRS_ALPHATESTENABLE, XRS_ALPHAREF,
    XRS_SRCBLEND, XRS_DESTBLEND, XRS_ZWRITEENABLE, XRS_DITHERENABLE, XRS_SHADEMODE,
    XRS_COLORWRITEENABLE, XRS_STENCILZFAIL, XRS_STENCILPASS, XRS_STENCILFUNC, XRS_STENCILREF,
    XRS_STENCILMASK, XRS_STENCILWRITEMASK, XRS_BLENDOP,
    XRS_FOGENABLE = 82, XRS_FOGTABLEMODE, XRS_FOGSTART, XRS_FOGEND, XRS_FOGDENSITY, XRS_RANGEFOGENABLE,
    XRS_LIGHTING = 92, XRS_SPECULARENABLE, XRS_LOCALVIEWER, XRS_COLORVERTEX,
    XRS_SPECULARMATERIALSOURCE = 100, XRS_DIFFUSEMATERIALSOURCE, XRS_AMBIENTMATERIALSOURCE,
    XRS_EMISSIVEMATERIALSOURCE,
    XRS_AMBIENT = 105,
    XRS_PSTEXTUREMODES = 117, XRS_VERTEXBLEND, XRS_FOGCOLOR, XRS_FILLMODE, XRS_BACKFILLMODE,
    XRS_TWOSIDEDLIGHTING, XRS_NORMALIZENORMALS, XRS_ZENABLE, XRS_STENCILENABLE, XRS_STENCILFAIL,
    XRS_FRONTFACE, XRS_CULLMODE, XRS_TEXTUREFACTOR, XRS_ZBIAS,
};

/* XDK texture-stage indices (D3D__TextureState[stage][32]). */
enum {
    XTSS_ADDRESSU = 0, XTSS_ADDRESSV, XTSS_ADDRESSW, XTSS_MAGFILTER, XTSS_MINFILTER, XTSS_MIPFILTER,
    XTSS_MIPMAPLODBIAS, XTSS_MAXMIPLEVEL, XTSS_MAXANISOTROPY, XTSS_COLORKEYOP, XTSS_COLORSIGN,
    XTSS_ALPHAKILL, XTSS_COLOROP, XTSS_COLORARG0, XTSS_COLORARG1, XTSS_COLORARG2, XTSS_ALPHAOP,
    XTSS_ALPHAARG0, XTSS_ALPHAARG1, XTSS_ALPHAARG2, XTSS_RESULTARG, XTSS_TEXTURETRANSFORMFLAGS,
    XTSS_TEXCOORDINDEX = 28,
};

/* The PC D3D8 numbers xbox_d3d8 uses (its d3d8_xbox.h can't be included
 * next to the generated-code headers). */
enum {
    PRS_ZENABLE = 7, PRS_FILLMODE = 8, PRS_SHADEMODE = 9, PRS_ZWRITEENABLE = 14, PRS_ALPHATESTENABLE = 15,
    PRS_SRCBLEND = 19, PRS_DESTBLEND = 20, PRS_CULLMODE = 22, PRS_ZFUNC = 23, PRS_ALPHAREF = 24,
    PRS_ALPHAFUNC = 25, PRS_DITHERENABLE = 26, PRS_ALPHABLENDENABLE = 27, PRS_FOGENABLE = 28,
    PRS_SPECULARENABLE = 29, PRS_FOGCOLOR = 34, PRS_FOGTABLEMODE = 35, PRS_FOGSTART = 36, PRS_FOGEND = 37,
    PRS_FOGDENSITY = 38, PRS_RANGEFOGENABLE = 48, PRS_STENCILENABLE = 52, PRS_STENCILFAIL = 53,
    PRS_STENCILZFAIL = 54, PRS_STENCILPASS = 55, PRS_STENCILFUNC = 56, PRS_STENCILREF = 57,
    PRS_STENCILMASK = 58, PRS_STENCILWRITEMASK = 59, PRS_TEXTUREFACTOR = 60, PRS_LIGHTING = 137,
    PRS_AMBIENT = 139, PRS_COLORVERTEX = 141, PRS_LOCALVIEWER = 142, PRS_NORMALIZENORMALS = 143,
    PRS_DIFFUSEMATERIALSOURCE = 145, PRS_SPECULARMATERIALSOURCE = 146, PRS_AMBIENTMATERIALSOURCE = 147,
    PRS_EMISSIVEMATERIALSOURCE = 148, PRS_VERTEXBLEND = 151, PRS_COLORWRITEENABLE = 168, PRS_BLENDOP = 171,

    PTSS_COLOROP = 1, PTSS_COLORARG1 = 2, PTSS_COLORARG2 = 3, PTSS_ALPHAOP = 4, PTSS_ALPHAARG1 = 5,
    PTSS_ALPHAARG2 = 6, PTSS_TEXCOORDINDEX = 11, PTSS_ADDRESSU = 13, PTSS_ADDRESSV = 14,
    PTSS_MAGFILTER = 16, PTSS_MINFILTER = 17, PTSS_MIPFILTER = 18, PTSS_MAXANISOTROPY = 21,
};

/* What the translator met that xbox_d3d8 can't show, by kind: counted, and
 * the first few of each logged. */
enum {
    GAP_VALUE,          /* an XDK value with no PC equivalent (mapped to a default) */
    GAP_TEXOP,          /* a texture op outside the verified 1-11 range */
    GAP_DIFFUSE_ARG,    /* plain D3DTA_DIFFUSE on a stage > 0 (xbox_d3d8 reads 0 as "default") */
    GAP_FRONTFACE,      /* FRONTFACE not the default CW */
    GAP_COUNT
};
static volatile LONG g_gap[GAP_COUNT], g_ev_gap;
static const char *const g_gap_name[GAP_COUNT] = {
    "unmapped value", "texture op above 11", "D3DTA_DIFFUSE on stage > 0", "FRONTFACE not CW",
};

static void gap(int kind, const char *what, uint32_t v)
{
    InterlockedIncrement(&g_gap[kind]);
    event(&g_ev_gap, 20, "translate: %s (%s = %X)", g_gap_name[kind], what, v);
}

static uint32_t rs_x(int i)
{
    return MEM32(D3D_RENDERSTATE + 4 * i);
}

/* XDK comparisons are GL's: NEVER 0x200 .. ALWAYS 0x207. PC: 1 .. 8. */
static uint32_t xl_cmp(uint32_t v, const char *what)
{
    if (v >= 0x200 && v <= 0x207)
        return v - 0x200 + 1;
    gap(GAP_VALUE, what, v);
    return 8;
}

/* XDK blend factors: ZERO 0, ONE 1, SRCCOLOR 0x300 .. SRCALPHASAT 0x308 (the
 * GL order, which is the PC order from SRCCOLOR 3 on). */
static uint32_t xl_blend(uint32_t v, const char *what)
{
    if (v == 0)
        return 1;
    if (v == 1)
        return 2;
    if (v >= 0x300 && v <= 0x308)
        return v - 0x300 + 3;
    gap(GAP_VALUE, what, v);             /* the constant-colour factors */
    return 2;
}

static uint32_t xl_stencil_op(uint32_t v, const char *what)
{
    switch (v) {
    case 0x1E00: return 1;              /* KEEP */
    case 0:      return 2;              /* ZERO */
    case 0x1E01: return 3;              /* REPLACE */
    case 0x1E02: return 4;              /* INCRSAT */
    case 0x1E03: return 5;              /* DECRSAT */
    case 0x150A: return 6;              /* INVERT */
    case 0x8507: return 7;              /* INCR */
    case 0x8508: return 8;              /* DECR */
    }
    gap(GAP_VALUE, what, v);
    return 1;
}

static uint32_t xl_blend_op(uint32_t v)
{
    switch (v) {
    case 0x8006: return 1;              /* ADD */
    case 0x800A: return 2;              /* SUBTRACT */
    case 0x800B: return 3;              /* REVSUBTRACT */
    case 0x8007: return 4;              /* MIN */
    case 0x8008: return 5;              /* MAX */
    }
    gap(GAP_VALUE, "BLENDOP", v);
    return 1;
}

/* XDK colour-write bits: R 1 << 16, G 1 << 8, B 1, A 1 << 24. */
static uint32_t xl_color_write(uint32_t v)
{
    return ((v >> 16) & 1) | (((v >> 8) & 1) << 1) | ((v & 1) << 2) | (((v >> 24) & 1) << 3);
}

/* The render states xbox_d3d8 reads, from D3D__RenderState. */
static unsigned translate_rs(d3dh_state_t *o)
{
    unsigned n = 0;
    uint32_t v;

#define PUT(s, val) do { o[n].state = (s); o[n].value = (val); n++; } while (0)
    PUT(PRS_ZENABLE, rs_x(XRS_ZENABLE));                    /* D3DZB_*: the same */
    v = rs_x(XRS_FILLMODE);                                 /* GL POINT 0x1B00, LINE, FILL */
    PUT(PRS_FILLMODE, (v >= 0x1B00 && v <= 0x1B02) ? v - 0x1B00 + 1 : 3);
    PUT(PRS_SHADEMODE, rs_x(XRS_SHADEMODE) == 0x1D00 ? 1 : 2);  /* GL FLAT 0x1D00, SMOOTH 0x1D01 */
    PUT(PRS_ZWRITEENABLE, rs_x(XRS_ZWRITEENABLE));
    PUT(PRS_ALPHATESTENABLE, rs_x(XRS_ALPHATESTENABLE));
    PUT(PRS_SRCBLEND, xl_blend(rs_x(XRS_SRCBLEND), "SRCBLEND"));
    PUT(PRS_DESTBLEND, xl_blend(rs_x(XRS_DESTBLEND), "DESTBLEND"));
    v = rs_x(XRS_CULLMODE);                                 /* NONE 0, CW 0x900, CCW 0x901 */
    PUT(PRS_CULLMODE, v == 0x900 ? 2 : v == 0x901 ? 3 : 1);
    if (v && rs_x(XRS_FRONTFACE) != 0x900)
        gap(GAP_FRONTFACE, "FRONTFACE", rs_x(XRS_FRONTFACE));
    PUT(PRS_ZFUNC, xl_cmp(rs_x(XRS_ZFUNC), "ZFUNC"));
    PUT(PRS_ALPHAREF, rs_x(XRS_ALPHAREF));
    PUT(PRS_ALPHAFUNC, xl_cmp(rs_x(XRS_ALPHAFUNC), "ALPHAFUNC"));
    PUT(PRS_DITHERENABLE, rs_x(XRS_DITHERENABLE));
    PUT(PRS_ALPHABLENDENABLE, rs_x(XRS_ALPHABLENDENABLE));
    PUT(PRS_FOGENABLE, rs_x(XRS_FOGENABLE));
    PUT(PRS_FOGTABLEMODE, rs_x(XRS_FOGTABLEMODE));          /* D3DFOG_*: the same */
    PUT(PRS_FOGSTART, rs_x(XRS_FOGSTART));
    PUT(PRS_FOGEND, rs_x(XRS_FOGEND));
    PUT(PRS_FOGDENSITY, rs_x(XRS_FOGDENSITY));
    PUT(PRS_RANGEFOGENABLE, rs_x(XRS_RANGEFOGENABLE));
    PUT(PRS_FOGCOLOR, rs_x(XRS_FOGCOLOR));
    PUT(PRS_SPECULARENABLE, rs_x(XRS_SPECULARENABLE));
    PUT(PRS_STENCILENABLE, rs_x(XRS_STENCILENABLE));
    PUT(PRS_STENCILFAIL, xl_stencil_op(rs_x(XRS_STENCILFAIL), "STENCILFAIL"));
    PUT(PRS_STENCILZFAIL, xl_stencil_op(rs_x(XRS_STENCILZFAIL), "STENCILZFAIL"));
    PUT(PRS_STENCILPASS, xl_stencil_op(rs_x(XRS_STENCILPASS), "STENCILPASS"));
    PUT(PRS_STENCILFUNC, xl_cmp(rs_x(XRS_STENCILFUNC), "STENCILFUNC"));
    PUT(PRS_STENCILREF, rs_x(XRS_STENCILREF));
    PUT(PRS_STENCILMASK, rs_x(XRS_STENCILMASK));
    PUT(PRS_STENCILWRITEMASK, rs_x(XRS_STENCILWRITEMASK));
    PUT(PRS_TEXTUREFACTOR, rs_x(XRS_TEXTUREFACTOR));
    PUT(PRS_LIGHTING, rs_x(XRS_LIGHTING));
    PUT(PRS_AMBIENT, rs_x(XRS_AMBIENT));
    PUT(PRS_COLORVERTEX, rs_x(XRS_COLORVERTEX));
    PUT(PRS_LOCALVIEWER, rs_x(XRS_LOCALVIEWER));
    PUT(PRS_NORMALIZENORMALS, rs_x(XRS_NORMALIZENORMALS));
    PUT(PRS_DIFFUSEMATERIALSOURCE, rs_x(XRS_DIFFUSEMATERIALSOURCE));    /* D3DMCS_*: the same */
    PUT(PRS_SPECULARMATERIALSOURCE, rs_x(XRS_SPECULARMATERIALSOURCE));
    PUT(PRS_AMBIENTMATERIALSOURCE, rs_x(XRS_AMBIENTMATERIALSOURCE));
    PUT(PRS_EMISSIVEMATERIALSOURCE, rs_x(XRS_EMISSIVEMATERIALSOURCE));
    PUT(PRS_VERTEXBLEND, rs_x(XRS_VERTEXBLEND));
    PUT(PRS_COLORWRITEENABLE, xl_color_write(rs_x(XRS_COLORWRITEENABLE)));
    PUT(PRS_BLENDOP, xl_blend_op(rs_x(XRS_BLENDOP)));
#undef PUT
    return n;
}

/* XDK texture ops. 1-11 are the PC ones (DISABLE .. ADDSMOOTH). From 12 the
 * XDK order is BLENDDIFFUSEALPHA, BLENDCURRENTALPHA, BLENDTEXTUREALPHA,
 * BLENDFACTORALPHA, ..., DOTPRODUCT3 22 (from memory, not yet checked
 * against a draw that uses one), mapped to xbox_d3d8's own numbers
 * (BLENDTEXTUREALPHA 13, BLENDFACTORALPHA 14, BLENDCURRENTALPHA 15, DOT3 24). */
static uint32_t xl_texop(uint32_t v)
{
    if (v >= 1 && v <= 11)
        return v;
    gap(GAP_TEXOP, "texture op", v);
    switch (v) {
    case 12: return 12;
    case 13: return 15;
    case 14: return 13;
    case 15: return 14;
    case 22: return 24;
    }
    return 4;
}

/* D3DTA_* are the same. xbox_d3d8 reads an argument of 0 as its default
 * (TEXTURE for ARG1, CURRENT for ARG2), so plain DIFFUSE can't be passed.
 * On stage 0 CURRENT is the diffuse colour, so it stands in there. */
static uint32_t xl_arg(uint32_t v, unsigned stage, const char *what)
{
    if (v != 0)
        return v;
    if (stage == 0)
        return 1;
    gap(GAP_DIFFUSE_ARG, what, stage);
    return 1;
}

/* XDK filters: NONE 0, POINT 1, LINEAR 2, ANISOTROPIC 3, QUINCUNX 4,
 * GAUSSIANCUBIC 5; the last two become LINEAR. */
static uint32_t xl_filter(uint32_t v)
{
    return v > 3 ? 2 : v;
}

/* XDK addressing: WRAP 1, MIRROR 2, CLAMP 3, BORDER 4, CLAMPTOEDGE 5. PC 5 is
 * MIRRORONCE, so CLAMPTOEDGE becomes CLAMP. */
static uint32_t xl_address(uint32_t v)
{
    return v == 5 ? 3 : v;
}

/* ── Pixel shaders (S4): XDK combiner state → xbox_d3d8's ──── */

/* SetPixelShader (0x102BB0) copies the handle's D3DPIXELSHADERDEF into
 * D3D__RenderState[0..56] (unless device flag 0x10 is set) and its
 * PSTextureModes into RS[117]; RS[0..56] is the def's layout: */
enum {
    XPS_ALPHAINPUTS = 0, XPS_FINAL_ABCD = 8, XPS_FINAL_EFG = 9, XPS_CONST0 = 10, XPS_CONST1 = 18,
    XPS_ALPHAOUTPUTS = 26, XPS_RGBINPUTS = 34, XPS_COMPAREMODE = 42, XPS_FINALCONST0 = 43, XPS_FINALCONST1 = 44,
    XPS_RGBOUTPUTS = 45, XPS_COMBINERCOUNT = 53, XPS_DOTMAPPING = 55, XPS_INPUTTEXTURE = 56,
    XPS_DEF_DWORDS = 57,
};

/* xbox_d3d8's numbers for them (d3d8_xbox.h), read by d3d8_combiners.c. */
enum {
    PPS_ALPHAINPUTS = 200, PPS_FINAL_ABCD = 208, PPS_FINAL_EFG = 209, PPS_RGBINPUTS = 210, PPS_RGBOUTPUTS = 218,
    PPS_ALPHAOUTPUTS = 226, PPS_COMBINERCOUNT = 234, PPS_CONST0 = 235, PPS_CONST1 = 243, PPS_TEXTUREMODES = 251,
};

/* What xbox_d3d8's combiner translator can't express, by kind: counted once
 * per translation that meets it (a mirrored draw or a probe), the first few
 * logged. */
enum {
    PSGAP_BLUE,         /* an alpha-portion or final G input reads blue; xbox_d3d8 always reads alpha */
    PSGAP_REG,          /* a reserved register (6, 7), or V1R0_SUM/EF_PROD in a general stage */
    PSGAP_OUTMAP,       /* an output mapping with no xbox_d3d8 equivalent */
    PSGAP_BLUE_TO_ALPHA,/* AB/CD blue-to-alpha */
    PSGAP_MUX_LSB,      /* a mux on r0.a's LSB; xbox_d3d8 muxes on the MSB */
    PSGAP_TEXMODE,      /* a texture mode other than NONE, 2D, 3D, CUBEMAP */
    PSGAP_FINALCONST,   /* final-combiner constants with all 8 stages in use */
    PSGAP_V1R0,         /* COMPLEMENT_V1/R0 on the V1R0 sum */
    PSGAP_COUNT
};
static volatile LONG g_psgap[PSGAP_COUNT], g_ev_psgap;
static const char *const g_psgap_name[PSGAP_COUNT] = {
    "blue channel read", "register", "output mapping", "blue to alpha", "mux on LSB", "texture mode",
    "final constants", "V1R0 complement",
};

static void psgap(unsigned *mask, int kind, const char *what, uint32_t v)
{
    if (*mask & (1u << kind))
        return;
    *mask |= 1u << kind;
    InterlockedIncrement(&g_psgap[kind]);
    event(&g_ev_psgap, 20, "pixel shader: %s (%s = %X)", g_psgap_name[kind], what, v);
}

/* One XDK combiner input byte (register [3:0], alpha/blue [4], mapping
 * [7:5]) in xbox_d3d8's terms, which differ only in registers 14 and 15
 * (V1R0_SUM and EF_PROD are the other way round there). In the alpha
 * portion and for G, bit 4 clear means blue. */
static uint32_t xps_in(uint32_t b, int alpha_portion, int final, unsigned *gaps)
{
    uint32_t reg = b & 15;

    if (reg == 6 || reg == 7 || (!final && reg >= 14))
        psgap(gaps, PSGAP_REG, "input register", reg);
    if (reg >= 14)
        reg ^= 1;
    if (alpha_portion && !(b & 0x10) && reg != 0)
        psgap(gaps, PSGAP_BLUE, "input", b);
    return (b & 0xF0) | reg;
}

/* XDK PS_COMBINERINPUTS packs A in the top byte and D in the bottom one;
 * xbox_d3d8 the other way round. `swap`: exchange AB and CD (a mux, below). */
static uint32_t xps_inputs(uint32_t v, int alpha_portion, int swap, unsigned *gaps)
{
    uint32_t a = xps_in((v >> 24) & 0xFF, alpha_portion, 0, gaps), b = xps_in((v >> 16) & 0xFF, alpha_portion, 0, gaps);
    uint32_t c = xps_in((v >> 8) & 0xFF, alpha_portion, 0, gaps), d = xps_in(v & 0xFF, alpha_portion, 0, gaps), t;

    if (swap) {
        t = a; a = c; c = t;
        t = b; b = d; d = t;
    }
    return a | (b << 8) | (c << 16) | (d << 24);
}

/* XDK PS_COMBINEROUTPUTS: CD destination [3:0], AB [7:4], sum [11:8], CD dot
 * 12, AB dot 13, mux 14, output mapping [17:15] (SHIFTRIGHT_1 is 6), CD/AB
 * blue-to-alpha 18/19. xbox_d3d8 has AB in [3:0] and CD in [7:4], and
 * SHIFTRIGHT_1 as 5. Its mux picks AB when r0.a >= 0.5, where the hardware
 * picks CD, so a muxing stage has AB and CD exchanged, inputs included. */
static uint32_t xps_out(uint32_t v, int rgb, unsigned *gaps, int *swap)
{
    uint32_t cd = v & 15, ab = (v >> 4) & 15, sum = (v >> 8) & 15;
    uint32_t cd_dot = (v >> 12) & 1, ab_dot = (v >> 13) & 1, mux = (v >> 14) & 1, map = (v >> 15) & 7, t;

    if (rgb && (v & (3u << 18)))
        psgap(gaps, PSGAP_BLUE_TO_ALPHA, "output", v);
    if (map == 6) {
        map = 5;
    } else if (map > 4) {
        psgap(gaps, PSGAP_OUTMAP, "output", v);
        map = 0;
    }
    if (ab >= 14 || cd >= 14 || sum >= 14)
        psgap(gaps, PSGAP_REG, "output register", v);
    *swap = (int)mux;
    if (mux) {
        t = ab; ab = cd; cd = t;
        t = ab_dot; ab_dot = cd_dot; cd_dot = t;
    }
    return ab | (cd << 4) | (sum << 8) | (cd_dot << 12) | (ab_dot << 13) | (mux << 14) | (map << 15);
}

/* XDK texture modes, 5 bits per stage (NONE 0, PROJECT2D 1, PROJECT3D 2,
 * CUBEMAP 3, PASSTHRU 4, ...) → xbox_d3d8's nibble (2D 0, 3D 1, CUBE 2,
 * NONE 3). */
static uint32_t xps_texmode(uint32_t m, unsigned *gaps)
{
    switch (m) {
    case 0: return 3;
    case 1: return 0;
    case 2: return 1;
    case 3: return 2;
    }
    psgap(gaps, PSGAP_TEXMODE, "texture mode", m);
    return 3;
}

static int reads_const(uint32_t abcd, uint32_t efg)
{
    int i;
    for (i = 0; i < 4; i++) {
        uint32_t r = (abcd >> (8 * i)) & 15;
        if (r == 1 || r == 2)
            return 1;
    }
    for (i = 1; i < 4; i++) {
        uint32_t r = (efg >> (8 * i)) & 15;
        if (r == 1 || r == 2)
            return 1;
    }
    return 0;
}

/* The pixel shader in D3D__RenderState, for xbox_d3d8: its combiner states
 * and token (stage count in [3:0], texture-mode nibbles in [23:8]). Returns
 * the gap kinds met, as a mask. The final combiner's constants (RS 43/44)
 * have no slot of their own there: xbox_d3d8 gives the final combiner the
 * last stage's, so when it reads one they go into one extra stage that
 * writes nothing. No final combiner (ABCD and EFG both 0) outputs r0. */
static unsigned translate_ps(d3dh_ps_t *ps)
{
    uint32_t count = rs_x(XPS_COMBINERCOUNT), n = count & 0xFF, n_host, abcd, efg, tm, token, i, v;
    uint32_t c0[8], c1[8], out;
    unsigned gaps = 0;
    int swap;

    if (n < 1)
        n = 1;
    if (n > 8)
        n = 8;
    for (i = 0; i < 8; i++) {           /* SAME_C0/C1: every stage reads stage 0's */
        c0[i] = rs_x(XPS_CONST0 + ((count & 0x1000) ? i : 0));
        c1[i] = rs_x(XPS_CONST1 + ((count & 0x10000) ? i : 0));
    }
    ps->n_rs = 0;
#define PUT(s, val) do { ps->rs[ps->n_rs].state = (s); ps->rs[ps->n_rs].value = (val); ps->n_rs++; } while (0)
    for (i = 0; i < 8; i++) {
        if (i < n) {
            out = xps_out(rs_x(XPS_RGBOUTPUTS + i), 1, &gaps, &swap);
            if (swap && !(count & 0x100))
                psgap(&gaps, PSGAP_MUX_LSB, "combiner count", count);
            PUT(PPS_RGBOUTPUTS + i, out);
            PUT(PPS_RGBINPUTS + i, xps_inputs(rs_x(XPS_RGBINPUTS + i), 0, swap, &gaps));
            out = xps_out(rs_x(XPS_ALPHAOUTPUTS + i), 0, &gaps, &swap);
            if (swap && !(count & 0x100))
                psgap(&gaps, PSGAP_MUX_LSB, "combiner count", count);
            PUT(PPS_ALPHAOUTPUTS + i, out);
            PUT(PPS_ALPHAINPUTS + i, xps_inputs(rs_x(XPS_ALPHAINPUTS + i), 1, swap, &gaps));
        } else {                        /* unused, or the constants' carrier stage: writes nothing */
            PUT(PPS_RGBOUTPUTS + i, 0);
            PUT(PPS_RGBINPUTS + i, 0);
            PUT(PPS_ALPHAOUTPUTS + i, 0);
            PUT(PPS_ALPHAINPUTS + i, 0);
        }
    }

    abcd = rs_x(XPS_FINAL_ABCD);
    efg = rs_x(XPS_FINAL_EFG);
    n_host = n;
    if (!abcd && !efg) {                /* no final combiner: r0 */
        PUT(PPS_FINAL_ABCD, 12u << 24);             /* D = r0 */
        PUT(PPS_FINAL_EFG, (12u | 0x10) << 16);     /* G = r0.a */
    } else {
        if (reads_const(abcd, efg)) {
            if (n < 8) {
                c0[n] = rs_x(XPS_FINALCONST0);
                c1[n] = rs_x(XPS_FINALCONST1);
                n_host = n + 1;
            } else if (c0[7] != rs_x(XPS_FINALCONST0) || c1[7] != rs_x(XPS_FINALCONST1)) {
                psgap(&gaps, PSGAP_FINALCONST, "final constants", rs_x(XPS_FINALCONST0));
            }
        }
        if (efg & 0x60)                 /* COMPLEMENT_V1/R0: only matter if V1R0_SUM (14) is read */
            for (i = 0; i < 7; i++)
                if (((i < 4 ? abcd >> (8 * i) : efg >> (8 * (i - 3))) & 15) == 14)
                    psgap(&gaps, PSGAP_V1R0, "final EFG", efg);
        for (i = 0, v = 0; i < 4; i++)  /* A..D: top byte first */
            v |= xps_in((abcd >> (24 - 8 * i)) & 0xFF, 0, 1, &gaps) << (8 * i);
        PUT(PPS_FINAL_ABCD, v);
        v = xps_in((efg >> 24) & 0xFF, 0, 1, &gaps) | (xps_in((efg >> 16) & 0xFF, 0, 1, &gaps) << 8) |
            (xps_in((efg >> 8) & 0xFF, 1, 1, &gaps) << 16);
        PUT(PPS_FINAL_EFG, v);
    }
    for (i = 0; i < 8; i++) {
        PUT(PPS_CONST0 + i, c0[i]);
        PUT(PPS_CONST1 + i, c1[i]);
    }
    PUT(PPS_COMBINERCOUNT, n_host);
    tm = rs_x(XRS_PSTEXTUREMODES);
    for (i = 0, v = 0; i < 4; i++)
        v |= xps_texmode((tm >> (5 * i)) & 31, &gaps) << (4 * i);
    PUT(PPS_TEXTUREMODES, v);
#undef PUT
    token = n_host | (v << 8);
    ps->token = token;
    return gaps;
}

static unsigned translate_tss(unsigned stage, d3dh_state_t *o)
{
    uint32_t base = D3D_TEXTURESTATE + 4 * D3D_TSS_COUNT * stage;
    unsigned n = 0;

#define TS(i) MEM32(base + 4 * (i))
#define PUT(s, val) do { o[n].state = (s); o[n].value = (val); n++; } while (0)
    PUT(PTSS_COLOROP, xl_texop(TS(XTSS_COLOROP)));
    PUT(PTSS_COLORARG1, xl_arg(TS(XTSS_COLORARG1), stage, "COLORARG1"));
    PUT(PTSS_COLORARG2, xl_arg(TS(XTSS_COLORARG2), stage, "COLORARG2"));
    PUT(PTSS_ALPHAOP, xl_texop(TS(XTSS_ALPHAOP)));
    PUT(PTSS_ALPHAARG1, xl_arg(TS(XTSS_ALPHAARG1), stage, "ALPHAARG1"));
    PUT(PTSS_ALPHAARG2, xl_arg(TS(XTSS_ALPHAARG2), stage, "ALPHAARG2"));
    PUT(PTSS_TEXCOORDINDEX, TS(XTSS_TEXCOORDINDEX));
    PUT(PTSS_ADDRESSU, xl_address(TS(XTSS_ADDRESSU)));
    PUT(PTSS_ADDRESSV, xl_address(TS(XTSS_ADDRESSV)));
    /* RECOMP_D3D_BRIDGE_POINT: point sampling from level 0, the way the NV2A
     * executor's software rasteriser samples, to compare with its dumps. */
    PUT(PTSS_MAGFILTER, g_point ? 1 : xl_filter(TS(XTSS_MAGFILTER)));
    PUT(PTSS_MINFILTER, g_point ? 1 : xl_filter(TS(XTSS_MINFILTER)));
    PUT(PTSS_MIPFILTER, g_point ? 0 : xl_filter(TS(XTSS_MIPFILTER)));
    PUT(PTSS_MAXANISOTROPY, TS(XTSS_MAXANISOTROPY));
#undef PUT
#undef TS
    return n;
}

/* XDK primitive types are the NV2A's (POINTLIST 1, LINELIST 2, LINELOOP 3,
 * LINESTRIP 4, TRIANGLELIST 5, TRIANGLESTRIP 6, TRIANGLEFAN 7, QUADLIST 8,
 * QUADSTRIP 9, POLYGON 10) and draws count vertices. xbox_d3d8 takes PC types
 * (POINTLIST 1, LINELIST 2, LINESTRIP 3, TRIANGLELIST 4, TRIANGLESTRIP 5,
 * TRIANGLEFAN 6, and its own QUADLIST 8) and primitive counts. 0: not mapped. */
static uint32_t xl_prim(uint32_t xdk, uint32_t verts, uint32_t *count)
{
    switch (xdk) {
    case 1: *count = verts;                        return 1;
    case 2: *count = verts / 2;                    return 2;
    case 4: *count = verts >= 2 ? verts - 1 : 0;   return 3;
    case 5: *count = verts / 3;                    return 4;
    case 6: *count = verts >= 3 ? verts - 2 : 0;   return 5;
    case 7: *count = verts >= 3 ? verts - 2 : 0;   return 6;
    case 8: *count = verts / 4;                    return 8;
    }
    *count = 0;
    return 0;
}

/* Physical Data -> guest VA: the runtime's contiguous arena maps physical P
 * at 0x80000000 + P (xbox_memory_layout.c). An address beyond what the arena
 * handed out is taken as a VA, as the NV2A executor does. */
uint32_t xbox_ContiguousAllocatedBytes(void);

static uint32_t phys_to_va(uint32_t p)
{
    return p < xbox_ContiguousAllocatedBytes() ? 0x80000000u + p : p;
}

/* FNV-1a over the whole texture when it is up to 256 KB, else over 64
 * 64-byte samples spread across it plus the first and last 4 KB. */
static uint32_t content_sig(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u, i;

#define MIX(ptr, len) do { const uint8_t *q_ = (ptr); uint32_t k_; \
        for (k_ = 0; k_ < (len); k_++) h = (h ^ q_[k_]) * 16777619u; } while (0)
    if (n <= 256 * 1024) {
        MIX(p, n);
    } else {
        MIX(p, 4096);
        for (i = 0; i < 64; i++)
            MIX(p + (uint64_t)(n - 64) * i / 64, 64);
        MIX(p + n - 4096, 4096);
    }
#undef MIX
    return h ? h : 1;
}

/* Why a draw wasn't mirrored. Every draw path is counted, so the [D3DB] host
 * draws line accounts for all of a frame's draws (S4; before, only
 * DrawVerticesUP's skips were). */
enum {
    SKIP_TARGET,        /* a render target the host can't make (not a texture level, or its format) */
    SKIP_VS_PROGRAM,    /* a programmable vertex shader (S5) */
    SKIP_PRIM,          /* LINELOOP, QUADSTRIP, POLYGON */
    SKIP_TEXTURE,       /* a cube or volume texture, a format xbox_d3d8 lacks, or P8 with no palette */
    SKIP_INDEXED,       /* DrawIndexedVertices with a vertex format (fixed-function 3D: S5-S6) */
    SKIP_IMMEDIATE,     /* Begin/SetVertexData/End (S4 part 2) */
    SKIP_VERTICES,      /* DrawVertices (never seen) */
    SKIP_COUNT
};
static volatile LONG g_skip[SKIP_COUNT], g_mirrored_draws, g_mirrored_rt_draws, g_mirrored_ps_draws;
static volatile LONG g_ev_skip, g_ev_draw, g_ev_draw_tss, g_ev_diffuse, g_ev_pal, g_ev_rt;
static volatile LONG g_ev_draw_rt, g_ev_draw_tss_rt, g_ev_draw_rt_v;
static const char *const g_skip_name[SKIP_COUNT] = {
    "render target", "vertex program", "primitive", "texture", "indexed", "immediate", "DrawVertices",
};

static void skip(int kind, uint32_t v)
{
    InterlockedIncrement(&g_skip[kind]);
    event(&g_ev_skip, 20, "draw not mirrored: %s (%X)", g_skip_name[kind], v);
}

#define DEV_PALETTES        0xA88u      /* palettes[4], by stage (SetPalette 0xFFE10) */
#define XDK_FMT_P8          0x0B
#define RES_TYPE_PALETTE    0x30000u    /* Common & 0x70000 */

/* The guest texture bound to a stage, as a host descriptor. Returns 0 if
 * there is none, 1 if described, -1 if it can't be mirrored. */
static int describe_texture(unsigned stage, d3dh_tex_t *t)
{
    uint32_t hdr = MEM32(dev() + DEV_TEXTURES + 4 * stage), f, sz, fmt;

    if (!hdr)
        return 0;
    f = MEM32(hdr + RES_FORMAT);
    sz = MEM32(hdr + RES_SIZE);
    fmt = (f >> 8) & 0xFF;
    if ((f & 4) || ((f >> 4) & 15) != 2 || !d3dh_format_supported(fmt))
        return -1;                      /* cube, not 2D, or not uploadable */
    memset(t, 0, sizeof(*t));
    t->key = hdr;
    t->data = MEM32(hdr + RES_DATA);
    t->format = fmt;
    t->levels = (f >> 16) & 15;
    if (sz) {                           /* linear: Size has width, height, pitch */
        t->width = (sz & 0xFFF) + 1;
        t->height = ((sz >> 12) & 0xFFF) + 1;
        t->pitch = ((sz >> 24) + 1) * 64;
    } else {
        t->width = 1u << ((f >> 20) & 15);
        t->height = 1u << ((f >> 24) & 15);
    }
    if (!t->levels)
        t->levels = 1;
    t->bytes = d3dh_texture_bytes(fmt, t->width, t->height, t->levels, t->pitch);
    t->bits = (const uint8_t *)XBOX_PTR(phys_to_va(t->data));
    t->sig = content_sig(t->bits, t->bytes);
    if (fmt == XDK_FMT_P8) {
        /* S4: the stage's palette. Its Common carries the size in [31:30]
         * (256 >> n entries) and the resource type 3 in [18:16]. */
        uint32_t pal = MEM32(dev() + DEV_PALETTES + 4 * stage), common;
        if (!pal)
            return -1;
        common = MEM32(pal + RES_COMMON);
        t->pal_entries = 256u >> ((common >> 30) & 3);
        t->palette = (const uint32_t *)XBOX_PTR(phys_to_va(MEM32(pal + RES_DATA)));
        t->pal_sig = content_sig((const uint8_t *)t->palette, 4 * t->pal_entries);
        event(&g_ev_pal, 6, "P8 texture %08X (%ux%u, %u levels) on stage %u: palette %08X, Common %08X (type %u),"
              " %u entries, data %08X", hdr, t->width, t->height, t->levels, stage, pal, common,
              (common >> 16) & 7, t->pal_entries, MEM32(pal + RES_DATA));
    }
    return 1;
}

/* Where the guest draws: the back buffer, or a texture level the host can
 * make a render target for. Returns 0, or -1 (not mirrored). `z_off`: the
 * guest has no depth surface, so depth and stencil tests are off (the XDK
 * SetRenderTarget turns them off with a null Z). */
static int describe_target(d3dh_target_t *t, int *z_off)
{
    uint32_t d = dev(), rt = MEM32(d + DEV_RENDER_TARGET), f, sz;

    memset(t, 0, sizeof(*t));
    *z_off = MEM32(d + DEV_DEPTH_SURFACE) == 0;
    if (strcmp(surface_kind(rt), "back") == 0)
        return 0;
    if (!MEM32(rt + RES_PARENT))
        return -1;                      /* not a texture level */
    f = MEM32(rt + RES_FORMAT);
    sz = MEM32(rt + RES_SIZE);
    t->format = (f >> 8) & 0xFF;
    if (sz) {
        t->width = (sz & 0xFFF) + 1;
        t->height = ((sz >> 12) & 0xFFF) + 1;
    } else {
        t->width = 1u << ((f >> 20) & 15);
        t->height = 1u << ((f >> 24) & 15);
    }
    t->data = res_data(rt);
    if (!t->data || t->format == XDK_FMT_P8 || !d3dh_format_supported(t->format) ||
        d3dh_texture_bytes(t->format, 4, 4, 1, 0) == 0)
        return -1;
    event(&g_ev_rt, 8, "render target %08X: texture level, Data %08X, %ux%u format %02X, depth %s", rt, t->data,
          t->width, t->height, t->format, *z_off ? "none" : "set");
    return 0;
}

/* ── S4 checks: the pixel-shader probe and the texture check ── */

/* Probe inputs: four sets (v0, v1, t0-t3, ARGB) with distinct values in
 * every channel, so a wrong register or channel shows, and spread over the
 * range so that at least one set keeps each result off 0 and 255. */
#define PROBE_SETS 4
static const uint32_t g_probe_in[PROBE_SETS][6] = {
    { 0xB0906030u, 0x40C0A020u, 0xE0F08010u, 0x80304070u, 0x6050E0C0u, 0xF0209050u },
    { 0x80808080u, 0x7F7F7F7Fu, 0x90888078u, 0x70787F88u, 0x8C847C74u, 0x747C848Cu },
    { 0x20406080u, 0xA0806040u, 0x406080A0u, 0x6080A0C0u, 0x80A0C0E0u, 0x302010F0u },
    { 0xFF102030u, 0x10FFE0D0u, 0x08183848u, 0xF8E8C8A8u, 0x55AA55AAu, 0xAA55AA55u },
};

#define PS_SEEN_MAX 64
static uint32_t g_ps_seen[PS_SEEN_MAX];
static int g_ps_seen_n;

/* Each distinct pixel shader, the first time a draw (any path) uses one:
 * translated, drawn on the host once per probe set, and logged with its XDK
 * def (RS 0-56 and 117) so tools/diag/pscheck.py can evaluate the same def
 * on the CPU and compare. */
static void ps_probe_once(void)
{
    uint32_t h = 2166136261u, i, out = 0;
    unsigned gaps;
    d3dh_ps_t ps;
    char line[1024];
    int len = 0, rc, k;

    for (i = 0; i < XPS_DEF_DWORDS; i++)
        h = (h ^ rs_x(i)) * 16777619u;
    h = (h ^ rs_x(XRS_PSTEXTUREMODES)) * 16777619u;
    for (i = 0; i < (uint32_t)g_ps_seen_n; i++)
        if (g_ps_seen[i] == h)
            return;
    if (g_ps_seen_n >= PS_SEEN_MAX)
        return;
    g_ps_seen[g_ps_seen_n++] = h;
    gaps = translate_ps(&ps);
    for (i = 0; i < XPS_DEF_DWORDS; i++)
        len += snprintf(line + len, sizeof(line) - len, "%s%08X", i ? " " : "", rs_x(i));
    for (k = 0; k < PROBE_SETS; k++) {
        const uint32_t *in = g_probe_in[k];
        rc = d3dh_ps_probe(&ps, in[0], in[1], in + 2, &out);
        fprintf(stderr, "[D3DB] ps probe %d.%d: %s %08X; gaps %X; v0 %08X v1 %08X t %08X %08X %08X %08X;"
                " token %08X; def %s tm %08X\n", g_ps_seen_n - 1, k, rc ? "FAILED" : "result", out, gaps, in[0],
                in[1], in[2], in[3], in[4], in[5], ps.token, line, rs_x(XRS_PSTEXTUREMODES));
    }
}

#define TEX_SEEN_MAX 256
static uint32_t g_tex_seen[TEX_SEEN_MAX][2];
static int g_tex_seen_n;

/* Each distinct P8 texture (by header and palette), the first time a draw
 * (any path) binds it: uploaded through the palette and written out. */
static void tex_check_once(void)
{
    unsigned s;
    int i;

    for (s = 0; s < 4; s++) {
        d3dh_tex_t t;
        char path[700];
        uint32_t hdr = MEM32(dev() + DEV_TEXTURES + 4 * s);
        if (!hdr || ((MEM32(hdr + RES_FORMAT) >> 8) & 0xFF) != XDK_FMT_P8 || describe_texture(s, &t) != 1)
            continue;
        for (i = 0; i < g_tex_seen_n; i++)
            if (g_tex_seen[i][0] == hdr && g_tex_seen[i][1] == t.pal_sig)
                break;
        if (i < g_tex_seen_n || g_tex_seen_n >= TEX_SEEN_MAX)
            continue;
        g_tex_seen[g_tex_seen_n][0] = hdr;
        g_tex_seen[g_tex_seen_n][1] = t.pal_sig;
        g_tex_seen_n++;
        snprintf(path, sizeof(path), "%stex_%08X_%08X.bmp", g_check_prefix, hdr, t.pal_sig);
        fprintf(stderr, "[D3DB] texture check %s: %s (P8 %ux%u, %u levels, palette %u entries, stage %u)\n",
                d3dh_texture_check(&t, path) == 0 ? "wrote" : "FAILED", path, t.width, t.height, t.levels,
                t.pal_entries, s);
    }
}

/* Every draw, on any path, before the path is mirrored or skipped. */
static void draw_checks(void)
{
    if (g_psprobe && MEM32(dev() + DEV_PIXEL_SHADER))
        ps_probe_once();
    if (g_texcheck && g_check_prefix[0])
        tex_check_once();
}

/* RECOMP_D3D_BRIDGE_RTLOG: a render-target draw in full, for a software
 * re-render (tools/diag/rtcheck.py): target, primitive, the XDK states it
 * needs, texture 0, and every vertex's dwords. */
static void rt_log_draw(const d3db_args_t *a, const d3dh_draw_t *dr, const d3dh_tex_t *t0)
{
    uint32_t i, n = a->arg[1] * (a->arg[3] / 4);
    char *line = (char *)malloc(64 + 9 * (size_t)n);
    int len = 0;
    uint32_t tss = D3D_TEXTURESTATE;

    if (!line)
        return;
    for (i = 0; i < n; i++)
        len += sprintf(line + len, " %08X", MEM32(a->arg[2] + 4 * i));
    fprintf(stderr, "[D3DB] rtdraw target %08X prim %u verts %u stride %u fvf %X tex0 %08X %ux%u fmt %02X"
            " blend %u %X %X op %X atest %u %X ref %X colorop %u %u %u alphaop %u %u %u addr %u %u:%s\n",
            dr->target.data, a->arg[0], a->arg[1], a->arg[3], dr->fvf, dr->tex[0] ? t0->data : 0,
            dr->tex[0] ? t0->width : 0, dr->tex[0] ? t0->height : 0, dr->tex[0] ? t0->format : 0,
            rs_x(XRS_ALPHABLENDENABLE), rs_x(XRS_SRCBLEND), rs_x(XRS_DESTBLEND), rs_x(XRS_BLENDOP),
            rs_x(XRS_ALPHATESTENABLE), rs_x(XRS_ALPHAFUNC), rs_x(XRS_ALPHAREF),
            MEM32(tss + 4 * XTSS_COLOROP), MEM32(tss + 4 * XTSS_COLORARG1), MEM32(tss + 4 * XTSS_COLORARG2),
            MEM32(tss + 4 * XTSS_ALPHAOP), MEM32(tss + 4 * XTSS_ALPHAARG1), MEM32(tss + 4 * XTSS_ALPHAARG2),
            MEM32(tss + 4 * XTSS_ADDRESSU), MEM32(tss + 4 * XTSS_ADDRESSV), line);
    free(line);
}

/* DrawVerticesUP(PrimitiveType, VertexCount, pVertexStreamZeroData,
 * VertexStreamZeroStride), after the lifted call: the same draw through
 * xbox_d3d8, with the state read from D3D's shadows at this point. S4 adds
 * render targets and pixel shaders. */
static void mirror_draw_up(const d3db_args_t *a)
{
    uint32_t d = dev(), vs, count = 0, prim, s, i;
    d3dh_state_t rs[64], tss[4][16];
    d3dh_tex_t tex[4];
    d3dh_ps_t ps;
    d3dh_draw_t dr;
    int z_off;

    draw_checks();
    memset(tex, 0, sizeof(tex));
    memset(&dr, 0, sizeof(dr));
    if (describe_target(&dr.target, &z_off) != 0) {
        skip(SKIP_TARGET, MEM32(d + DEV_RENDER_TARGET));
        return;
    }
    vs = MEM32(d + DEV_VS_HANDLE);
    if (vs & 1) {
        skip(SKIP_VS_PROGRAM, vs);
        return;
    }
    prim = xl_prim(a->arg[0], a->arg[1], &count);
    if (!prim || !count) {
        skip(SKIP_PRIM, a->arg[0]);
        return;
    }

    for (s = 0; s < 4; s++) {
        int r = describe_texture(s, &tex[s]);
        if (r < 0) {
            uint32_t hdr = MEM32(d + DEV_TEXTURES + 4 * s);
            skip(SKIP_TEXTURE, hdr ? MEM32(hdr + RES_FORMAT) : 0);
            return;
        }
        dr.tex[s] = r ? &tex[s] : NULL;
        dr.tss[s] = tss[s];
        dr.n_tss[s] = translate_tss(s, tss[s]);
        /* S4: MIPFILTER NONE samples level 0 only in D3D, but xbox_d3d8's
         * sampler still walks the mip chain (MIP_POINT, MaxLOD unbounded).
         * So such a stage gets its texture's level 0 alone. */
        if (r)
            for (i = 0; i < dr.n_tss[s]; i++)
                if (tss[s][i].state == PTSS_MIPFILTER && tss[s][i].value == 0)
                    tex[s].levels = 1;
    }
    if (MEM32(d + DEV_PIXEL_SHADER)) {
        translate_ps(&ps);
        dr.ps = &ps;
    }
    dr.rs = rs;
    dr.n_rs = translate_rs(rs);
    if (z_off)                          /* no guest depth surface: no depth or stencil test */
        for (i = 0; i < dr.n_rs; i++)
            if (rs[i].state == PRS_ZENABLE || rs[i].state == PRS_ZWRITEENABLE || rs[i].state == PRS_STENCILENABLE)
                rs[i].value = 0;
    dr.prim = prim;
    dr.prim_count = count;
    dr.stride = a->arg[3];
    dr.fvf = vs;
    dr.verts = (const void *)XBOX_PTR(a->arg[2]);
    dr.vert_count = a->arg[1];
    dr.vp_x = MEM32(d + DEV_VIEWPORT);
    dr.vp_y = MEM32(d + DEV_VIEWPORT + 4);
    dr.vp_w = MEM32(d + DEV_VIEWPORT + 8);
    dr.vp_h = MEM32(d + DEV_VIEWPORT + 12);
    memcpy(&dr.vp_minz, (const void *)XBOX_PTR(d + DEV_VIEWPORT + 16), 4);
    memcpy(&dr.vp_maxz, (const void *)XBOX_PTR(d + DEV_VIEWPORT + 20), 4);

    /* The first few draws, raw XDK values; render-target draws have their own
     * allowance, as the splash uses up the first. */
    if (dr.target.data ? g_ev_draw_rt < 8 : g_ev_draw < 12) {
        char line[400];
        int len = 0, k;
        static const int shown[] = { XRS_ZFUNC, XRS_ALPHAFUNC, XRS_ALPHABLENDENABLE, XRS_ALPHATESTENABLE,
                                     XRS_ALPHAREF, XRS_SRCBLEND, XRS_DESTBLEND, XRS_ZWRITEENABLE,
                                     XRS_STENCILFUNC, XRS_STENCILREF, XRS_LIGHTING, XRS_ZENABLE,
                                     XRS_STENCILENABLE, XRS_CULLMODE, XRS_COLORWRITEENABLE, XRS_BLENDOP };
        for (k = 0; k < (int)(sizeof(shown) / sizeof(shown[0])); k++)
            len += snprintf(line + len, sizeof(line) - len, " %d=%X", shown[k], rs_x(shown[k]));
        event(dr.target.data ? &g_ev_draw_rt : &g_ev_draw, dr.target.data ? 8 : 12, "DrawVerticesUP prim %u verts %u stride %u fvf %X target %08X (%ux%u)%s tex0 %08X"
              " (fmt %02X %ux%u lv %u data %08X, %u bytes); RS%s", a->arg[0], a->arg[1], a->arg[3], vs,
              dr.target.data, dr.target.width, dr.target.height, z_off ? " no depth" : "", tex[0].key,
              dr.tex[0] ? tex[0].format : 0, dr.tex[0] ? tex[0].width : 0, dr.tex[0] ? tex[0].height : 0,
              dr.tex[0] ? tex[0].levels : 0, dr.tex[0] ? tex[0].data : 0, dr.tex[0] ? tex[0].bytes : 0, line);
        for (len = 0, k = 0; k < D3D_TSS_COUNT; k++)
            len += snprintf(line + len, sizeof(line) - len, " %X", MEM32(D3D_TEXTURESTATE + 4 * k));
        event(dr.target.data ? &g_ev_draw_tss_rt : &g_ev_draw_tss, dr.target.data ? 8 : 12, "  TSS stage 0:%s",
              line);
        if (dr.target.data && (vs & 0x40)) {
            uint32_t off = ((vs & 0xE) == 4 ? 16 : 12) + ((vs & 0x10) ? 12 : 0);
            event(&g_ev_draw_rt_v, 8, "  render-target draw: vertex 0 at (%g, %g), diffuse %08X, tex0 %s%08X",
                  (double)*(const float *)XBOX_PTR(a->arg[2]), (double)*(const float *)XBOX_PTR(a->arg[2] + 4),
                  MEM32(a->arg[2] + off), tex[0].key && dr.tex[0] ? "Data " : "none ", dr.tex[0] ? tex[0].data : 0);
        }
    }
    if (vs & 0x40) {                    /* D3DFVF_DIFFUSE: vertex 0's colour, for the first draws */
        uint32_t off = ((vs & 0xE) == 4 ? 16 : 12) + ((vs & 0x10) ? 12 : 0);
        event(&g_ev_diffuse, 120, "UP draw %ld at guest swap %ld: tex0 %s, vertex 0 diffuse %08X",
              (long)g_mirrored_draws, (long)g_swaps, dr.tex[0] ? "set" : "none", MEM32(a->arg[2] + off));
    }
    if (g_rtlog && dr.target.data)
        rt_log_draw(a, &dr, &tex[0]);
    if (d3dh_draw_up(&dr) == 0) {
        InterlockedIncrement(&g_mirrored_draws);
        if (dr.target.data)
            InterlockedIncrement(&g_mirrored_rt_draws);
        if (dr.ps)
            InterlockedIncrement(&g_mirrored_ps_draws);
    }
}

/* The draw paths that aren't mirrored yet: counted, and checked. */
static void mirror_draw_other(int id)
{
    uint32_t vs = MEM32(dev() + DEV_VS_HANDLE);

    draw_checks();
    if (id == D3DB_END)
        skip(SKIP_IMMEDIATE, vs);
    else if (vs & 1)
        skip(SKIP_VS_PROGRAM, vs);
    else
        skip(id == D3DB_DRAW ? SKIP_VERTICES : SKIP_INDEXED, vs);
}

/* Time spent in the mirror (translate + host call, in d3db_leave), in QPC
 * ticks: draws, swaps (dump, copy, present), everything else. */
static volatile LONG64 g_mirror_ticks[3];

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
            fprintf(stderr, "[D3DB] host draws: mirrored %ld (to render targets %ld, with pixel shaders %ld),"
                    " host draws %u (fails %u); not mirrored: target %ld, vertex program %ld, primitive %ld,"
                    " texture %ld, indexed %ld, immediate %ld, DrawVertices %ld; textures uploaded %u (P8 %u,"
                    " fails %u, evictions %u); translation gaps: value %ld, texop %ld, diffuse arg %ld,"
                    " frontface %ld\n",
                    (long)g_mirrored_draws, (long)g_mirrored_rt_draws, (long)g_mirrored_ps_draws, st.draws,
                    st.draw_fails, (long)g_skip[SKIP_TARGET], (long)g_skip[SKIP_VS_PROGRAM],
                    (long)g_skip[SKIP_PRIM], (long)g_skip[SKIP_TEXTURE], (long)g_skip[SKIP_INDEXED],
                    (long)g_skip[SKIP_IMMEDIATE], (long)g_skip[SKIP_VERTICES], st.tex_uploads, st.p8_uploads,
                    st.tex_upload_fails, st.tex_evictions, (long)g_gap[GAP_VALUE], (long)g_gap[GAP_TEXOP],
                    (long)g_gap[GAP_DIFFUSE_ARG], (long)g_gap[GAP_FRONTFACE]);
            fprintf(stderr, "[D3DB] host S4: render targets %u (fails %u), draws to them %u, clears %u, sampled %u"
                    " (while bound as the target %u); pixel-shader draws %u, probes %u; pixel-shader gaps:"
                    " blue %ld, register %ld, output map %ld, blue-to-alpha %ld, mux LSB %ld, texture mode %ld,"
                    " final constants %ld, V1R0 %ld\n",
                    st.rt_created, st.rt_fails, st.rt_draws, st.rt_clears, st.rt_sampled, st.rt_feedback,
                    st.ps_draws, st.ps_probes, (long)g_psgap[PSGAP_BLUE], (long)g_psgap[PSGAP_REG],
                    (long)g_psgap[PSGAP_OUTMAP], (long)g_psgap[PSGAP_BLUE_TO_ALPHA], (long)g_psgap[PSGAP_MUX_LSB],
                    (long)g_psgap[PSGAP_TEXMODE], (long)g_psgap[PSGAP_FINALCONST], (long)g_psgap[PSGAP_V1R0]);
            {
                static LONG64 last[3];
                LARGE_INTEGER f;
                LONG64 cur[3];
                int i;
                QueryPerformanceFrequency(&f);
                for (i = 0; i < 3; i++)
                    cur[i] = g_mirror_ticks[i];
                fprintf(stderr, "[D3DB] host mirror time this interval: draws %.1f ms, swaps %.1f ms,"
                        " other %.1f ms (%.1f%% of the interval)\n",
                        1000.0 * (cur[0] - last[0]) / f.QuadPart, 1000.0 * (cur[1] - last[1]) / f.QuadPart,
                        1000.0 * (cur[2] - last[2]) / f.QuadPart,
                        100.0 * ((cur[0] - last[0]) + (cur[1] - last[1]) + (cur[2] - last[2])) / f.QuadPart / dt);
                for (i = 0; i < 3; i++)
                    last[i] = cur[i];
            }
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
    case D3DB_TEX_LOCK: case D3DB_SURF_LOCK:
        if (d3db_host)                  /* the game is about to write it: re-upload at the next draw */
            d3dh_texture_touch(res_data(a->arg[0]));
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
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        switch (id) {
        case D3DB_CREATE_DEVICE: mirror_create_device(a); break;
        case D3DB_CLEAR:         mirror_clear(a);         break;
        case D3DB_SWAP:          mirror_swap();           break;
        case D3DB_DRAW_UP:       mirror_draw_up(a);       break;
        case D3DB_DRAW_INDEXED: case D3DB_END: case D3DB_DRAW:
            mirror_draw_other(id);
            break;
        }
        QueryPerformanceCounter(&t1);
        InterlockedAdd64(&g_mirror_ticks[id == D3DB_DRAW_UP || id == D3DB_DRAW_INDEXED || id == D3DB_END ||
                                         id == D3DB_DRAW ? 0 : id == D3DB_SWAP ? 1 : 2],
                         t1.QuadPart - t0.QuadPart);
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
