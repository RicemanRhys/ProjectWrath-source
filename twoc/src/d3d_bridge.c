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
 *   - Every draw path is counted on the [D3DB] host draws line.
 *
 * Step 34 (notes 18.36) adds fixed-function 3D: DrawIndexedVertices with a
 * vertex format (the title and menu's FVF 0x152, XYZ|NORMAL|DIFFUSE|TEX1).
 * Indices come from the caller, vertices from stream 0 plus the base vertex,
 * and every untransformed draw (UP too) takes world/view/projection, the
 * material and the enabled lights from the device (describe_ff). xbox_d3d8
 * lights a draw when its colours all come from the material; when one comes
 * from the vertex colour (the menu's ambient and emissive), which xbox_d3d8
 * ignores, the bridge lights a copy of the vertices on the CPU by D3D8's
 * rules and the host draws it unlit (cpu_light). Immediate-mode and
 * DrawVertices draws, and vertex programs, weren't mirrored yet.
 * Step 35 (notes 18.37) adds immediate mode (Begin, SetVertexData2f/4f,
 * End: the menu's planets and particles), buffered as the game sets each
 * attribute and drawn at End (mirror_draw_imm), and CopyRects from the back
 * buffer to a texture (the hub), copied on the host (mirror_copy_rects).
 * RECOMP_D3D_BRIDGE_DRAWLOG=A-B logs every draw of guest swaps A-B.
 * Step 36 (notes 18.38, S5 part 1) adds vertex programs on the UP and
 * indexed paths (mirror_draw_vp): the program is read from the current
 * vertex-shader object, the constants from D3D's shadow plus the viewport
 * scale and offset D3D gives the NV2A as c58/c59 (vp_constants), and every
 * register the program reads is fetched from its stream and expanded to a
 * float4 on the CPU (vp_vertices), so stream 1 interleaves into the one
 * host stream. xbox_d3d8's translator (d3d8_vsh.c, fixed locally) turns the
 * microcode into HLSL. Program draws that sample a CopyRects copy wait for
 * part 2 (counted). Checks: RECOMP_D3D_BRIDGE_VSDUMP (each program in full)
 * and _VSPROBE (its first vertices drawn as points by the host), compared by
 * tools/diag/vscheck.py.
 * S5 part 2 (notes 18.39) adds cube maps (describe_texture: six faces, each
 * a mip chain padded to 128 bytes; the hub's DXT5 0x07780F2D) and texel
 * addressing for linear textures in program draws (texel_patch: the hub's
 * water refraction samples the CopyRects copy at screen pixels).
 * Step 48 (notes 18.51) adds DrawVertices (mirror_draw_vertices: the
 * levels' snow and particles) and D3D's default final combiner for a pixel
 * shader without one (translate_ps: fog, and V1 + r0 with specular).
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
#include <math.h>

#include <windows.h>

#define RECOMP_GENERATED_CODE
#include "recomp/gen/recomp_funcs.h"

#include "d3d_bridge.h"
#include "d3d_host.h"
#include "log_block.h"

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
int d3db_census_on, d3db_host_on;
static int g_tint;                  /* RECOMP_D3D_BRIDGE_TINT: colour clears show the frame index */
static int g_point;                 /* RECOMP_D3D_BRIDGE_POINT: point sampling (a check, S3) */
static int g_psprobe;               /* RECOMP_D3D_BRIDGE_PSPROBE: probe each pixel shader (a check, S4) */
static int g_texcheck;              /* RECOMP_D3D_BRIDGE_TEXCHECK: write each P8 texture (a check, S4) */
static int g_rtlog;                 /* RECOMP_D3D_BRIDGE_RTLOG: log render-target draws in full (a check, S4) */
static int g_skiptex;               /* RECOMP_D3D_BRIDGE_SKIPTEX: textures of skipped draws (a check, step 34) */
static int g_vsdump;                /* RECOMP_D3D_BRIDGE_VSDUMP: each vertex program in full (a check, S5) */
static int g_vsprobe;               /* RECOMP_D3D_BRIDGE_VSPROBE: each vertex program drawn as points (a check, S5) */
static int g_vp_copy;               /* RECOMP_D3D_BRIDGE_VP_COPY: mirror copy-sampling program draws even
                                       when texel_patch can't patch them (a look, S5) */
static long g_drawlog_from, g_drawlog_to;   /* RECOMP_D3D_BRIDGE_DRAWLOG=A-B: every draw in swaps A-B (step 35) */
static char g_drawlog_trigger[MAX_PATH]; /* RECOMP_D3D_BRIDGE_DRAWLOG_TRIGGER: the next N swaps once this file appears */
static FILE *g_motion;                     /* RECOMP_D3D_BRIDGE_MOTIONLOG=PATH (step 42) */
static char g_motion_on_path[MAX_PATH];
static int g_motion_live;
static unsigned g_motion_n;
static uint32_t g_motion_vsc_ret[4], g_motion_draw_ret;  /* who last wrote c0 (a call chain), who drew */
static uint32_t g_draw_ret;         /* the draw call's guest return address, for the draw log (18.56) */
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

/* ── 16:9 (notes 18.54, 18.56) ──────────────────────────────────
 *
 * RECOMP_WIDESCREEN=1 (16:9) or =W:H, with the bridge on. The game builds
 * every projection in sub_000956B0 from the camera's aspect (height over
 * width, 0.75), which sets the projection's width and the frustum culling
 * together (18.54); its wrapper in recomp_manual.c scales that aspect by
 * d3db_camera_aspect_scale for the call. The host then renders a wider back
 * buffer (d3d_host.c). Off, or with the bridge off, d3db_wide is 0 and
 * nothing here runs. */
double d3db_wide;
static uint32_t g_guest_w, g_guest_h;      /* the guest back buffer (CreateDevice) */
static unsigned g_render_w, g_render_h;     /* RECOMP_RENDER_SIZE; 0 if not asked for */
static volatile LONG g_wide_cameras, g_wide_kept, g_ev_wide, g_wide_ff_pillar;

/* RECOMP_RENDER_SIZE=WxH or =native (the primary monitor's mode), with the
 * bridge on: the host renders at that size, or the largest of the picture's
 * aspect that fits in it (d3dh_create's host size, host_size). Independent
 * of RECOMP_WIDESCREEN: at 4:3 it is a sharper 4:3 picture. */
static int render_size_parse(const char *v, unsigned *pw, unsigned *ph);

static void render_size_init(void)
{
    const char *v = getenv("RECOMP_RENDER_SIZE");

    if (!v || !*v || strcmp(v, "0") == 0)
        return;
    if (!d3db_host_on) {
        fprintf(stderr, "[WIDE] RECOMP_RENDER_SIZE needs the bridge (RECOMP_D3D_BRIDGE=1): ignored\n");
        return;
    }
    render_size_parse(v, &g_render_w, &g_render_h);
}

/* "WxH" or "native" into *pw x *ph; "" or "0" is 0 x 0 (the guest's size).
 * Returns -1 (and 0 x 0) for anything else. Step 54: also the overlay's. */
static int render_size_parse(const char *v, unsigned *pw, unsigned *ph)
{
    unsigned w = 0, h = 0;

    *pw = *ph = 0;
    if (!v || !*v || strcmp(v, "0") == 0)
        return 0;
    if (_stricmp(v, "native") == 0) {
        DEVMODEA dm;
        memset(&dm, 0, sizeof(dm));
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm)) {
            w = dm.dmPelsWidth;
            h = dm.dmPelsHeight;
        }
    } else if (sscanf(v, "%ux%u", &w, &h) != 2 && sscanf(v, "%uX%u", &w, &h) != 2) {
        w = h = 0;
    }
    if (w < 64 || h < 64 || w > 8192 || h > 8192) {
        fprintf(stderr, "[WIDE] RECOMP_RENDER_SIZE=%s: not WxH (64-8192) or native; ignored\n", v);
        return -1;
    }
    *pw = w;
    *ph = h;
    return 0;
}

/* The host back buffer for the guest's gw x gh: the picture's aspect (the
 * guest's, or d3db_wide), at the guest's height, or the largest that fits in
 * RECOMP_RENDER_SIZE. Widths even. Neither set: the guest's own size. */
static void host_size(uint32_t gw, uint32_t gh, unsigned *w, unsigned *h)
{
    double r = d3db_wide > 0 ? d3db_wide : (double)gw / (double)gh;

    *w = gw;
    *h = gh;
    if (!g_render_w) {
        if (d3db_wide > 0)
            *w = 2 * (unsigned)(gh * r / 2.0 + 0.5);
        return;
    }
    if ((double)g_render_w / g_render_h > r) {
        *h = g_render_h;
        *w = 2 * (unsigned)(g_render_h * r / 2.0 + 0.5);
    } else {
        *w = g_render_w & ~1u;
        *h = (unsigned)(g_render_w / r + 0.5);
    }
}

static void wide_init(void)
{
    const char *v = getenv("RECOMP_WIDESCREEN");
    double w = 16.0, h = 9.0;

    render_size_init();
    if (!v || !*v || strcmp(v, "0") == 0)
        return;
    if (!d3db_host_on) {
        fprintf(stderr, "[WIDE] RECOMP_WIDESCREEN needs the bridge (RECOMP_D3D_BRIDGE=1, run_twoc.ps1 -Bridge or"
                " -HostOnly): off\n");
        return;
    }
    if (strchr(v, ':') && (sscanf(v, "%lf:%lf", &w, &h) != 2 || w <= 0 || h <= 0 || w / h <= 4.0 / 3.0 + 1e-6)) {
        fprintf(stderr, "[WIDE] RECOMP_WIDESCREEN=%s: not W:H wider than 4:3; off\n", v);
        return;
    }
    d3db_wide = w / h;
    fprintf(stderr, "[WIDE] widescreen on: %.4f (%g:%g); 4:3 camera aspects x %.4f\n", d3db_wide, w, h,
            (4.0 / 3.0) / d3db_wide);
}

/* Whether the current render target is the back buffer or a target of its
 * size (H2 Oh No's underwater pass renders the level into one). */
static int screen_target(void)
{
    uint32_t rt = MEM32(dev() + DEV_RENDER_TARGET), f, sz, w, h;

    if (!rt || strcmp(surface_kind(rt), "back") == 0)
        return 1;
    f = MEM32(rt + RES_FORMAT);
    sz = MEM32(rt + RES_SIZE);
    w = sz ? (sz & 0xFFF) + 1 : 1u << ((f >> 20) & 15);
    h = sz ? ((sz >> 12) & 0xFFF) + 1 : 1u << ((f >> 24) & 15);
    return w == g_guest_w && h == g_guest_h;
}

float d3db_camera_aspect_scale(uint32_t camera, float aspect)
{
    static uint32_t seen[32][2];
    static int n_seen;
    uint32_t rt = MEM32(dev() + DEV_RENDER_TARGET), a, i;
    int wide;

    if (d3db_wide <= 0)
        return 1.0f;
    wide = aspect > 0.74f && aspect < 0.76f && screen_target();
    InterlockedIncrement(wide ? &g_wide_cameras : &g_wide_kept);
    memcpy(&a, &aspect, 4);
    for (i = 0; i < (uint32_t)n_seen && (seen[i][0] != camera || seen[i][1] != (a ^ rt)); i++)
        ;
    if (i == (uint32_t)n_seen && n_seen < 32) {     /* each camera, aspect and target once */
        seen[n_seen][0] = camera;
        seen[n_seen++][1] = a ^ rt;
        event(&g_ev_wide, 32, "camera %08X aspect %.4f into %s %08X (Data %08X): %s", camera, (double)aspect,
              surface_kind(rt), rt, res_data(rt), wide ? "widened" : "kept");
    }
    return wide ? (float)((4.0 / 3.0) / d3db_wide) : 1.0f;
}

void d3db_init(void)
{
    d3db_host_on = env_on("RECOMP_D3D_BRIDGE");
    d3db_census_on = env_on("RECOMP_D3D_CENSUS");
    d3db_on = d3db_host_on || d3db_census_on;
    g_tint = d3db_host_on && env_on("RECOMP_D3D_BRIDGE_TINT");
    g_point = d3db_host_on && env_on("RECOMP_D3D_BRIDGE_POINT");
    g_psprobe = d3db_host_on && env_on("RECOMP_D3D_BRIDGE_PSPROBE");
    g_texcheck = d3db_host_on && env_on("RECOMP_D3D_BRIDGE_TEXCHECK");
    g_rtlog = d3db_host_on && env_on("RECOMP_D3D_BRIDGE_RTLOG");
    g_skiptex = d3db_host_on && env_on("RECOMP_D3D_BRIDGE_SKIPTEX");
    g_vsdump = d3db_host_on && env_on("RECOMP_D3D_BRIDGE_VSDUMP");
    g_vsprobe = d3db_host_on && env_on("RECOMP_D3D_BRIDGE_VSPROBE");
    g_vp_copy = d3db_host_on && env_on("RECOMP_D3D_BRIDGE_VP_COPY");
    if (d3db_host_on && getenv("RECOMP_D3D_BRIDGE_DRAWLOG")) {  /* "A-B": guest swaps */
        char *e;
        g_drawlog_from = strtol(getenv("RECOMP_D3D_BRIDGE_DRAWLOG"), &e, 0);
        g_drawlog_to = *e == '-' ? strtol(e + 1, NULL, 0) : g_drawlog_from;
    }
    if (d3db_host_on && getenv("RECOMP_D3D_BRIDGE_DRAWLOG_TRIGGER")) {
        snprintf(g_drawlog_trigger, sizeof(g_drawlog_trigger), "%s", getenv("RECOMP_D3D_BRIDGE_DRAWLOG_TRIGGER"));
        fprintf(stderr, "[D3DB] draw log on demand: the swaps after %s appears (it holds their count)\n",
                g_drawlog_trigger);
    }
    if (d3db_host_on && getenv("RECOMP_D3D_BRIDGE_MOTIONLOG")) {
        const char *m = getenv("RECOMP_D3D_BRIDGE_MOTIONLOG");
        g_motion = fopen(m, "w");
        if (g_motion) {
            setvbuf(g_motion, NULL, _IOFBF, 4 << 20);
            snprintf(g_motion_on_path, sizeof(g_motion_on_path), "%s.on", m);
            fprintf(stderr, "[D3DB] motion log to %s while %s exists\n", m, g_motion_on_path);
        }
    }
    wide_init();
    if (d3db_on)
        coop_watch_cs(GAME_D3D_LOCK);
    g_t0_ms = g_last_dump_ms = GetTickCount64();
    if (d3db_host_on)
        fprintf(stderr, "[D3DB] bridge on: host mirror (device, window, Clear, Swap, DrawVerticesUP,"
                " render targets, pixel shaders, P8, fixed-function DrawIndexedVertices, immediate mode, vertex programs)%s%s%s\n", g_tint ? "; colour clears tinted with the frame index" : "",
                g_psprobe ? "; pixel-shader probe" : "", g_texcheck ? "; P8 texture check" : "");
    if (d3db_census_on)
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

/* Pacing (S7). While the executor paces the game, the host adds no wait:
 * DXGI interval 0, no limiter. Without it (run_twoc.ps1 -HostOnly) nothing
 * else paces the game (18.32), and DXGI vsync would pace it to the host
 * monitor (240/s at 239 Hz, 18.33), so the host present runs a frame limiter
 * at the Xbox's 59.94 Hz divided by the game's own XDK
 * D3DPRESENT_INTERVAL_* (none for IMMEDIATE), with DXGI interval 0.
 * RECOMP_D3D_BRIDGE_INTERVAL overrides the DXGI interval and
 * RECOMP_D3D_BRIDGE_HZ the refresh rate the limiter divides (0: off). */
#define XBOX_REFRESH_HZ (60000.0 / 1001.0)

static unsigned present_interval(void)
{
    const char *v = getenv("RECOMP_D3D_BRIDGE_INTERVAL");

    return v && *v ? (unsigned)strtoul(v, NULL, 0) : 0;
}

static double present_hz(uint32_t xdk)
{
    const char *v = getenv("RECOMP_D3D_BRIDGE_HZ");
    double hz = v && *v ? strtod(v, NULL) : XBOX_REFRESH_HZ;

    if (env_on("RECOMP_PB_EXEC") && !(v && *v))
        return 0;
    switch (xdk) {
    case 0x80000000u: return 0;         /* IMMEDIATE */
    case 2:           return hz / 2;
    case 4:           return hz / 3;
    case 8:           return hz / 4;
    default:          return hz;        /* DEFAULT, ONE */
    }
}

/* A display whose refresh is a whole multiple of the limiter's rate (4 x
 * 59.94 = 239.76 Hz here) paces the presents itself (18.45): DXGI interval N
 * and no limiter, so each frame is shown for exactly N refreshes. The
 * limiter's clock isn't locked to the display, so its frames landed 3, 4 or 5
 * refreshes apart: step 41's micro-stutter, with the game's own frames steady.
 * Only within 0.5% of the rate, for N up to 4 (DXGI's most), and not when
 * RECOMP_D3D_BRIDGE_INTERVAL or _HZ is set. */
static void vsync_pacing(unsigned *interval, double *hz)
{
    const char *vi = getenv("RECOMP_D3D_BRIDGE_INTERVAL"), *vh = getenv("RECOMP_D3D_BRIDGE_HZ");
    double r;
    unsigned n;

    if (*hz <= 0 || *interval || (vi && *vi) || (vh && *vh))
        return;
    r = d3dh_display_hz();
    n = r > 0 ? (unsigned)(r / *hz + 0.5) : 0;
    if (n < 1 || n > 4 || fabs(r / n - *hz) > 0.005 * *hz) {
        fprintf(stderr, "[D3DB] display %.3f Hz isn't a multiple of %.3f Hz: the frame limiter paces\n", r, *hz);
        return;
    }
    fprintf(stderr, "[D3DB] display %.3f Hz = %u x %.3f Hz: presents at interval %u, paced by the display\n",
            r, n, r / n, n);
    *interval = n;
    *hz = 0;
}

/* Step 54 (notes 18.57): the player's pacing (RECOMP_PACING, the overlay).
 * auto is the above, as before. vsync: the display, at the whole interval
 * nearest its rate over the game's (the game then runs at the display's
 * rate over it, its own speed only on a multiple). limiter: the limiter, no
 * vsync. limiter+vsync: the limiter, and each present on a refresh. With the
 * executor pacing (no limiter rate) nothing changes. */
static uint32_t g_pp_interval;          /* the game's XDK presentation interval (CreateDevice) */
static int g_pacing_mode;               /* the TWOC_PACING_* in effect */

static void pacing_for(int mode, int live, unsigned *interval, double *hz)
{
    double r;
    unsigned n;

    *interval = present_interval();
    *hz = present_hz(g_pp_interval);
    if (mode == TWOC_PACING_AUTO && !live) {
        vsync_pacing(interval, hz);     /* at the start: as before */
        return;
    }
    if (*hz <= 0)
        return;
    /* During the run DWM's rate follows our presents on a variable-refresh
     * display, so the monitor's nominal rate decides (d3dh_monitor_hz). */
    r = d3dh_monitor_hz();
    if (r <= 0)
        r = d3dh_display_hz();
    if (mode == TWOC_PACING_AUTO) {
        const char *vi = getenv("RECOMP_D3D_BRIDGE_INTERVAL"), *vh = getenv("RECOMP_D3D_BRIDGE_HZ");
        n = r > 0 ? (unsigned)(r / *hz + 0.5) : 0;
        if ((vi && *vi) || (vh && *vh) || *interval || n < 1 || n > 4 || fabs(r / n - *hz) > 0.005 * *hz) {
            fprintf(stderr, "[D3DB] pacing auto: monitor %.3f Hz isn't a multiple of %.3f Hz: the frame limiter"
                    " paces\n", r, *hz);
            return;
        }
        fprintf(stderr, "[D3DB] pacing auto: monitor %.3f Hz = %u x %.3f Hz: presents at interval %u, paced by"
                " the display\n", r, n, r / n, n);
        *interval = n;
        *hz = 0;
        return;
    }
    switch (mode) {
    case TWOC_PACING_VSYNC:
        n = r > 0 ? (unsigned)(r / *hz + 0.5) : 1;
        n = n < 1 ? 1 : n > 4 ? 4 : n;
        fprintf(stderr, "[D3DB] pacing vsync: monitor %.3f Hz / %u = %.3f frames a second (the game's %.3f)\n", r, n,
                r > 0 ? r / n : 0.0, *hz);
        *interval = n;
        *hz = 0;
        break;
    case TWOC_PACING_LIMITER:
        *interval = 0;
        break;
    case TWOC_PACING_LIMITER_VSYNC:
        *interval = 1;
        break;
    }
}

/* Step 54: settings changed in the overlay, on the presenting thread with
 * the game held. The aspect takes effect at the game's next projection, the
 * host picture and the pacing at once. */
static void settings_apply(const twoc_settings_t *s)
{
    double wide = 0;
    unsigned rw, rh, w, h, interval;
    double hz;

    if (s->widescreen && s->wide_h > 0 && s->wide_w / s->wide_h > 4.0 / 3.0 + 1e-6)
        wide = s->wide_w / s->wide_h;
    if (wide != d3db_wide)
        fprintf(stderr, "[WIDE] widescreen %s (live)\n", wide > 0 ? "on" : "off");
    d3db_wide = wide;
    if (render_size_parse(s->render_size, &rw, &rh) == 0) {
        g_render_w = rw;
        g_render_h = rh;
    }
    host_size(g_guest_w ? g_guest_w : 640, g_guest_h ? g_guest_h : 480, &w, &h);
    d3dh_resize(w, h, d3db_wide);
    if (s->pacing != g_pacing_mode) {   /* only when the pacing itself changed */
        g_pacing_mode = s->pacing;
        pacing_for(s->pacing, 1, &interval, &hz);
        d3dh_set_pacing(interval, hz);
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
    unsigned interval, host_w, host_h;
    double hz;

    if (eax != 0 || !pp) {
        fprintf(stderr, "[D3DB] guest CreateDevice returned %08X: no host device\n", eax);
        return;
    }
    g_guest_w = MEM32(pp + 4 * PP_WIDTH);
    g_guest_h = MEM32(pp + 4 * PP_HEIGHT);
    g_pp_interval = MEM32(pp + 4 * PP_INTERVAL);
    g_pacing_mode = twoc_pacing_parse(getenv("RECOMP_PACING"));
    pacing_for(g_pacing_mode, 0, &interval, &hz);      /* auto: as before */
    host_size(g_guest_w ? g_guest_w : 640, g_guest_h ? g_guest_h : 480, &host_w, &host_h);
    d3dh_create(g_guest_w, g_guest_h, host_w, host_h, d3db_wide, g_render_w != 0, interval, hz,
                host_dump_prefix(prefix, sizeof(prefix)));
    d3dh_set_apply(settings_apply);
    if (host_dump_prefix(prefix, sizeof(prefix)))
        snprintf(g_check_prefix, sizeof(g_check_prefix), "%s", prefix);
}

static int describe_target(d3dh_target_t *t, int *z_off);

/* (Count, pRects, Flags, Color, Z, Stencil). The census never saw rects.
 * S4: a render-to-texture target is cleared on its host render target; the
 * tint check applies to the back buffer only. */
static int drawlog_on(void);
static unsigned g_drawlog_n;

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
    if (drawlog_on())                   /* step 49: where the clears fall among the draws */
        fprintf(stderr, "[D3DB] drawlog swap %ld clear before #%u: flags %X colour %08X z %g stencil %u, target %08X%s\n",
                (long)g_swaps + 1, g_drawlog_n, a->arg[2], a->arg[3], (double)z, a->arg[5], t.data,
                z_off ? " (no depth surface)" : "");
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
    XRS_POLYGONOFFSETZSLOPESCALE = 77, XRS_POLYGONOFFSETZOFFSET, XRS_POINTOFFSETENABLE, XRS_WIREFRAMEOFFSETENABLE,
    XRS_SOLIDOFFSETENABLE,
    XRS_FOGENABLE = 82, XRS_FOGTABLEMODE, XRS_FOGSTART, XRS_FOGEND, XRS_FOGDENSITY, XRS_RANGEFOGENABLE,
    XRS_LIGHTING = 92, XRS_SPECULARENABLE, XRS_LOCALVIEWER, XRS_COLORVERTEX,
    XRS_SPECULARMATERIALSOURCE = 100, XRS_DIFFUSEMATERIALSOURCE, XRS_AMBIENTMATERIALSOURCE,
    XRS_EMISSIVEMATERIALSOURCE,
    XRS_AMBIENT = 105, XRS_POINTSIZE, XRS_POINTSIZE_MIN, XRS_POINTSPRITEENABLE, XRS_POINTSCALEENABLE,
    XRS_POINTSCALE_A, XRS_POINTSCALE_B, XRS_POINTSCALE_C, XRS_POINTSIZE_MAX,
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
    GAP_LIGHTS,         /* more than 8 enabled lights (xbox_d3d8's limit): the rest dropped */
    GAP_TEXTRANSFORM,   /* TEXTURETRANSFORMFLAGS set on a textured stage (host matrices stay identity) */
    GAP_CPU_LIGHT,      /* lighting xbox_d3d8 gets wrong, with no diffuse colour to light on the CPU */
    GAP_SPECULAR,       /* SPECULARENABLE on a draw lit on the CPU (no specular produced) */
    GAP_TEXCOORD_SIZE,  /* texture-coordinate formats in the FVF (xbox_d3d8 sizes them its own way) */
    GAP_IMM_W,          /* an immediate XYZ vertex with w != 1 (the host takes x, y, z only) */
    GAP_COUNT
};
static volatile LONG g_gap[GAP_COUNT], g_ev_gap;
static const char *const g_gap_name[GAP_COUNT] = {
    "unmapped value", "texture op above 11", "D3DTA_DIFFUSE on stage > 0", "FRONTFACE not CW",
    "over 8 lights", "texture transform", "CPU lighting without a diffuse", "specular", "texcoord format",
    "immediate w not 1",
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
    PPS_DOTMAPPING = 252, PPS_INPUTTEXTURE = 253,
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
    PSGAP_TEXMODE,      /* a texture mode xbox_d3d8 has no route for (step 49: the dot-product ones added) */
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
    /* Step 49: the dot-product modes (xbox_d3d8 local, d3d8_combiners.h) */
    case 0x11: return 4;                /* DOTPRODUCT */
    case 0x0C: return 5;                /* DOT_RFLCT_SPEC (texm3x3vspec) */
    case 0x0B: return 6;                /* DOT_RFLCT_DIFF */
    case 0x0E: return 7;                /* DOT_STR_CUBE */
    case 0x09: return 8;                /* DOT_ST */
    case 0x0D: return 9;                /* DOT_STR_3D */
    case 0x0A: return 10;               /* DOT_ZW */
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
    if (!abcd && !efg) {
        /* No final combiner of its own: D3D's default (0x108140), which fogs
         * (A = FOG.a, B = r0, C = FOG.rgb) when FOGENABLE is on, and takes
         * V1 + r0 instead of r0 when SPECULARENABLE is. G = r0.a. */
        uint32_t col = rs_x(XRS_SPECULARENABLE) ? 14u : 12u;
        abcd = rs_x(XRS_FOGENABLE) ? 0x13000300u | (col << 16) : col;
        efg = 0x1C80u;
    }
    {
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
    PUT(PPS_DOTMAPPING, rs_x(XPS_DOTMAPPING));         /* the XDK's layout, which xbox_d3d8 reads (step 49) */
    PUT(PPS_INPUTTEXTURE, rs_x(XPS_INPUTTEXTURE));
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
    SKIP_TEXTURE,       /* a volume texture, a format xbox_d3d8 lacks, or P8 with no palette */
    SKIP_STREAM,        /* DrawIndexedVertices: no stream 0, or a stride under the FVF's size */
    SKIP_FVF,           /* an FVF with blend weights (XYZB*) */
    SKIP_IMMEDIATE,     /* Begin/SetVertexData/End (S4 part 2) */
    SKIP_VERTICES,      /* DrawVertices with no stream-0 vertices for its FVF (step 48) */
    SKIP_VP_COPY,       /* a vertex program sampling a CopyRects copy (S5 part 2) */
    SKIP_COUNT
};
static volatile LONG g_skip[SKIP_COUNT], g_mirrored_draws, g_mirrored_rt_draws, g_mirrored_ps_draws;
static volatile LONG g_ev_skip, g_ev_draw, g_ev_draw_tss, g_ev_diffuse, g_ev_pal, g_ev_rt;
static volatile LONG g_ev_draw_rt, g_ev_draw_tss_rt, g_ev_draw_rt_v;
static const char *const g_skip_name[SKIP_COUNT] = {
    "render target", "vertex program", "primitive", "texture", "stream", "vertex format", "immediate",
    "DrawVertices", "vertex program sampling a copy",
};

static int copy_dst_bound(void);
static volatile LONG g_copy_sampled_vp;

static void skip(int kind, uint32_t v)
{
    InterlockedIncrement(&g_skip[kind]);
    if (kind == SKIP_VS_PROGRAM && copy_dst_bound())
        InterlockedIncrement(&g_copy_sampled_vp);
    event(&g_ev_skip, 20, "draw not mirrored: %s (%X)", g_skip_name[kind], v);
}

#define DEV_PALETTES        0xA88u      /* palettes[4], by stage (SetPalette 0xFFE10) */
#define XDK_FMT_P8          0x0B
#define RES_TYPE_PALETTE    0x30000u    /* Common & 0x70000 */

/* Content signatures, remembered for the rest of a guest frame (step 35):
 * the menu's particle atlas is 256 KB and hashing it at each of ~100
 * immediate draws a frame cost ~130 us a draw. An entry is keyed by header,
 * Data and size and holds for the swap it was made in; a guest lock of the
 * texture drops it (sig_forget), as it does the host's cached copy. */
#define SIG_SLOTS 512
static struct {
    uint32_t hdr, data, bytes, sig;
    LONG swap;
} g_sig[SIG_SLOTS];

static uint32_t texture_sig(uint32_t hdr, uint32_t data, const uint8_t *bits, uint32_t bytes)
{
    uint32_t i = ((hdr >> 4) * 2654435761u) >> 23;      /* 9 bits: SIG_SLOTS */

    if (g_sig[i].hdr == hdr && g_sig[i].data == data && g_sig[i].bytes == bytes && g_sig[i].swap == g_swaps)
        return g_sig[i].sig;
    g_sig[i].hdr = hdr;
    g_sig[i].data = data;
    g_sig[i].bytes = bytes;
    g_sig[i].swap = g_swaps;
    g_sig[i].sig = content_sig(bits, bytes);
    return g_sig[i].sig;
}

static void sig_forget(uint32_t data)
{
    uint32_t i;

    for (i = 0; i < SIG_SLOTS; i++)
        if (g_sig[i].hdr && data >= g_sig[i].data && data - g_sig[i].data < g_sig[i].bytes)
            g_sig[i].hdr = 0;
}

static volatile LONG g_cube_binds, g_ev_cube;  /* draws' stages that bound a cube map (S5 part 2) */
static volatile LONG g_null_stages;             /* stages in use with no texture: opaque black (step 49) */
#define NULL_TEXTURE_KEY 0xFFFFFFF0u            /* the host texture for them (no guest header or Data) */

/* Whether the texture stage combiner reaches stage `s`: D3D stops at the
 * first stage whose COLOROP is DISABLE (XDK 1), and so does a stage's own. */
static int stage_reached(unsigned s)
{
    unsigned k;

    for (k = 0; k <= s; k++)
        if (MEM32(D3D_TEXTURESTATE + 4 * (k * D3D_TSS_COUNT + XTSS_COLOROP)) == 1)
            return 0;
    return 1;
}
#define CUBE_SEEN_MAX 16
static uint32_t g_cube_seen[CUBE_SEEN_MAX];     /* shaders, stage and texture modes: each kind logged once */

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
    if (((f >> 4) & 15) != 2 || !d3dh_format_supported(fmt) || ((f & 4) && (sz || fmt == XDK_FMT_P8)))
        return -1;                      /* not 2D, not uploadable, or a linear or P8 cube */
    memset(t, 0, sizeof(*t));
    t->cube = (f & 4) != 0;
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
    if (t->cube) {                      /* S5 part 2: six faces, each padded to 128 bytes */
        t->face_bytes = (t->bytes + 127) & ~127u;
        t->bytes = 6 * t->face_bytes;
    }
    t->bits = (const uint8_t *)XBOX_PTR(phys_to_va(t->data));
    t->sig = texture_sig(hdr, t->data, t->bits, t->bytes);
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

/* FNV-1a over the current def (RS 0-56) and texture modes (RS 117): a
 * pixel shader's identity, as the probe and the draw log name it. */
static uint32_t ps_def_hash(void)
{
    uint32_t h = 2166136261u, i;

    for (i = 0; i < XPS_DEF_DWORDS; i++)
        h = (h ^ rs_x(i)) * 16777619u;
    return (h ^ rs_x(XRS_PSTEXTUREMODES)) * 16777619u;
}

/* Each distinct pixel shader, the first time a draw (any path) uses one:
 * translated, drawn on the host once per probe set, and logged with its XDK
 * def (RS 0-56 and 117) so tools/diag/pscheck.py can evaluate the same def
 * on the CPU and compare. */
static void ps_probe_once(void)
{
    uint32_t h = ps_def_hash(), i, out = 0;
    unsigned gaps;
    d3dh_ps_t ps;
    char line[1024];
    int len = 0, rc, k;

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
                " token %08X; def %s tm %08X; fog %u specular %u; hash %08X\n", g_ps_seen_n - 1, k,
                rc ? "FAILED" : "result", out, gaps, in[0], in[1], in[2], in[3], in[4], in[5], ps.token, line,
                rs_x(XRS_PSTEXTUREMODES), rs_x(XRS_FOGENABLE) ? 1u : 0u, rs_x(XRS_SPECULARENABLE) ? 1u : 0u, h);
    }
}

#define TEX_SEEN_MAX 256
static uint32_t g_tex_seen[TEX_SEEN_MAX][2];
static int g_tex_seen_n;

/* Each distinct P8 texture (by header and palette), the first time a draw
 * (any path) binds it: uploaded through the palette and written out. S5
 * part 2: each DXT cube map too, as its six faces' level 0 raw from guest
 * memory at the face stride (skiptex_cube_*.raw; tools/diag/skiptex.py
 * decodes them), which shows whether the stride is right. */
static void tex_check_once(void)
{
    unsigned s;
    int i;

    for (s = 0; s < 4; s++) {
        d3dh_tex_t t;
        char path[700];
        uint32_t hdr = MEM32(dev() + DEV_TEXTURES + 4 * s), fmt = hdr ? (MEM32(hdr + RES_FORMAT) >> 8) & 0xFF : 0;
        if (hdr && (MEM32(hdr + RES_FORMAT) & 4) && (fmt == 0x0C || fmt == 0x0E || fmt == 0x0F) &&
            describe_texture(s, &t) == 1) {
            unsigned face;
            for (i = 0; i < g_tex_seen_n; i++)
                if (g_tex_seen[i][0] == hdr && g_tex_seen[i][1] == t.sig)
                    break;
            if (i < g_tex_seen_n || g_tex_seen_n >= TEX_SEEN_MAX)
                continue;
            g_tex_seen[g_tex_seen_n][0] = hdr;
            g_tex_seen[g_tex_seen_n][1] = t.sig;
            g_tex_seen_n++;
            for (face = 0; face < 6; face++) {
                FILE *o;
                snprintf(path, sizeof(path), "%sskiptex_cube_%08X_f%u_fmt%02X_%ux%u.raw", g_check_prefix, hdr, face,
                         fmt, t.width, t.height);
                o = fopen(path, "wb");
                if (o) {
                    fwrite(t.bits + (size_t)face * t.face_bytes, 1, d3dh_texture_bytes(fmt, t.width, t.height, 1, 0), o);
                    fclose(o);
                }
            }
            fprintf(stderr, "[D3DB] texture check: cube %08X (fmt %02X, edge %u, %u levels, face stride %u, Data %08X,"
                    " stage %u): six faces written\n", hdr, fmt, t.width, t.levels, t.face_bytes, t.data, s);
            continue;
        }
        if (!hdr || fmt != XDK_FMT_P8 || describe_texture(s, &t) != 1)
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

/* RECOMP_D3D_BRIDGE_SKIPTEX (step 34, a check): what the draws the mirror
 * skips would have shown. Each distinct texture (header, palette) a skipped
 * draw binds is written once per kind, as skiptex_<kind>_<header>_s<stage>
 * with its format and size: a BMP through the host upload (P8 through its
 * palette), or the raw level 0 for DXT, which tools/diag/skiptex.py decodes.
 * A skipped immediate draw also logs its position box the first time a
 * texture is seen. */
#define SKIPTEX_MAX 512
static uint32_t g_skiptex_seen[SKIPTEX_MAX][3];
static int g_skiptex_n;

static int skip_tex_once(const char *kind)
{
    unsigned s;
    int i, any_new = 0;

    for (s = 0; s < 4; s++) {
        d3dh_tex_t t;
        char path[700];
        uint32_t hdr = MEM32(dev() + DEV_TEXTURES + 4 * s), f, fmt, k = (uint32_t)kind[0] | ((uint32_t)kind[1] << 8);
        if (!hdr || describe_texture(s, &t) != 1)
            continue;
        for (i = 0; i < g_skiptex_n; i++)
            if (g_skiptex_seen[i][0] == hdr && g_skiptex_seen[i][1] == t.pal_sig && g_skiptex_seen[i][2] == k)
                break;
        if (i < g_skiptex_n || g_skiptex_n >= SKIPTEX_MAX)
            continue;
        g_skiptex_seen[g_skiptex_n][0] = hdr;
        g_skiptex_seen[g_skiptex_n][1] = t.pal_sig;
        g_skiptex_seen[g_skiptex_n][2] = k;
        g_skiptex_n++;
        any_new = 1;
        f = MEM32(hdr + RES_FORMAT);
        fmt = (f >> 8) & 0xFF;
        if (fmt == 0x0C || fmt == 0x0E || fmt == 0x0F) {    /* DXT1, DXT3, DXT5: raw level 0 */
            FILE *o;
            snprintf(path, sizeof(path), "%sskiptex_%s_%08X_s%u_fmt%02X_%ux%u.raw", g_check_prefix, kind, hdr, s,
                     fmt, t.width, t.height);
            o = fopen(path, "wb");
            if (o) {
                fwrite(t.bits, 1, d3dh_texture_bytes(fmt, t.width, t.height, 1, 0), o);
                fclose(o);
            }
        } else {
            snprintf(path, sizeof(path), "%sskiptex_%s_%08X_s%u_fmt%02X_%ux%u.bmp", g_check_prefix, kind, hdr, s,
                     fmt, t.width, t.height);
            if (d3dh_texture_check(&t, path) != 0)
                continue;
        }
        fprintf(stderr, "[D3DB] skipped-draw texture (%s, stage %u, Data %08X): %s\n", kind, s, t.data, path);
    }
    return any_new;
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

/* ── Fixed-function 3D (step 34): transforms, lights, material ── */

#define DEV_BASE_VERTEX     0x01Cu      /* SetIndices' BaseVertexIndex (0xFFEA0) */
#define DEV_LIGHTS_ON       0x398u      /* first enabled light; LightEnable (0xFFBA0) links them */
#define DEV_TRANSFORMS      0x750u      /* 64 bytes each, by XDK index (SetTransform 0xFEA20) */
#define DEV_MATERIAL        0x9F0u      /* D3DMATERIAL8 (SetMaterial 0xFEB50) */
#define LIGHT_NEXT          0x8Cu       /* in a 0x90-byte light (array at device +0x390): the next enabled one */
#define XTS_VIEW            0           /* XDK transform indices: VIEW 0, PROJECTION 1, TEXTURE0-3 2-5, WORLD 6 */
#define XTS_PROJECTION      1
#define XTS_WORLD           6

#define FVF_POSITION_MASK   0x00Eu      /* the XDK's FVF bits are the PC ones */
#define FVF_XYZ             0x002u
#define FVF_XYZRHW          0x004u
#define FVF_NORMAL          0x010u
#define FVF_PSIZE           0x020u
#define FVF_DIFFUSE         0x040u
#define FVF_SPECULAR        0x080u

/* Bytes per vertex of an FVF. Texture-coordinate formats: 0 two floats, 1
 * three, 2 four, 3 one. */
static uint32_t fvf_size(uint32_t fvf)
{
    static const uint8_t pos[8] = { 0, 12, 16, 16, 20, 24, 28, 32 };
    static const uint8_t tc[4] = { 8, 12, 16, 4 };
    uint32_t n = pos[(fvf & FVF_POSITION_MASK) >> 1], t, i;

    n += (fvf & FVF_NORMAL) ? 12 : 0;
    n += (fvf & FVF_PSIZE) ? 4 : 0;
    n += (fvf & FVF_DIFFUSE) ? 4 : 0;
    n += (fvf & FVF_SPECULAR) ? 4 : 0;
    t = (fvf >> 8) & 15;
    for (i = 0; i < t && i < 8; i++)
        n += tc[(fvf >> (16 + 2 * i)) & 3];
    return n;
}

/* What an untransformed draw needs besides the render states: world, view
 * and projection (device +0x750, XDK order), the material (+0x9F0), and the
 * enabled lights, walked from +0x398 (the first 26 dwords of each are its
 * D3DLIGHT8). */
static void describe_ff(d3dh_ff_t *ff)
{
    uint32_t d = dev(), l, n = 0, guard, s;

    memcpy(ff->world, (const void *)XBOX_PTR(d + DEV_TRANSFORMS + 64 * XTS_WORLD), 64);
    memcpy(ff->view, (const void *)XBOX_PTR(d + DEV_TRANSFORMS + 64 * XTS_VIEW), 64);
    memcpy(ff->proj, (const void *)XBOX_PTR(d + DEV_TRANSFORMS + 64 * XTS_PROJECTION), 64);
    memcpy(ff->material, (const void *)XBOX_PTR(d + DEV_MATERIAL), sizeof(ff->material));
    for (l = MEM32(d + DEV_LIGHTS_ON), guard = 0; l && guard < 256; l = MEM32(l + LIGHT_NEXT), guard++) {
        if (n == D3DH_MAX_LIGHTS) {
            gap(GAP_LIGHTS, "enabled lights", n + 1);
            break;
        }
        memcpy(ff->light[n++], (const void *)XBOX_PTR(l), sizeof(ff->light[0]));
    }
    ff->n_lights = n;
    for (s = 0; s < 4; s++)             /* the host's texture matrices stay identity */
        if (MEM32(D3D_TEXTURESTATE + 4 * (s * D3D_TSS_COUNT + XTSS_TEXTURETRANSFORMFLAGS)) &&
            MEM32(dev() + DEV_TEXTURES + 4 * s))
            gap(GAP_TEXTRANSFORM, "TEXTURETRANSFORMFLAGS on stage", s);
}

/* A material colour's source (RS 100-103, D3DMCS_*: MATERIAL 0, COLOR1 1,
 * COLOR2 2) as D3D resolves it: a vertex colour only with COLORVERTEX on and
 * that colour in the FVF. 0 material, 1 the vertex's diffuse, 2 its specular. */
static int color_source(int xrs, uint32_t fvf)
{
    uint32_t v = rs_x(xrs);

    if (!rs_x(XRS_COLORVERTEX))
        return 0;
    if (v == 1 && (fvf & FVF_DIFFUSE))
        return 1;
    if (v == 2 && (fvf & FVF_SPECULAR))
        return 2;
    return 0;
}

/* Whether a lit draw has to be lit here rather than by xbox_d3d8, whose
 * lighting takes every colour from the material (COLORVERTEX and the
 * material sources are ignored) and skips lighting without normals. The
 * title and menu light with the ambient and emissive colours read from the
 * vertex colour (RS 102/103 = COLOR1). The result goes in the diffuse
 * colour, so the FVF needs one. */
static int needs_cpu_lighting(uint32_t fvf)
{
    if (!rs_x(XRS_LIGHTING) || (fvf & FVF_POSITION_MASK) != FVF_XYZ)
        return 0;
    if (!color_source(XRS_DIFFUSEMATERIALSOURCE, fvf) && !color_source(XRS_AMBIENTMATERIALSOURCE, fvf) &&
        !color_source(XRS_EMISSIVEMATERIALSOURCE, fvf) && (fvf & FVF_NORMAL))
        return 0;
    if (!(fvf & FVF_DIFFUSE)) {
        gap(GAP_CPU_LIGHT, "lit with a vertex colour or no normal, but no diffuse to write: FVF", fvf);
        return 0;
    }
    return 1;
}

typedef struct { float r, g, b, a; } col_t;

static col_t argb_col(uint32_t c)
{
    col_t o;
    o.r = ((c >> 16) & 255) / 255.0f;
    o.g = ((c >> 8) & 255) / 255.0f;
    o.b = (c & 255) / 255.0f;
    o.a = (c >> 24) / 255.0f;
    return o;
}

static uint32_t col_argb(col_t c)
{
#define Q(x) ((uint32_t)((x) <= 0.0f ? 0 : (x) >= 1.0f ? 255 : (int)((x) * 255.0f + 0.5f)))
    return (Q(c.a) << 24) | (Q(c.r) << 16) | (Q(c.g) << 8) | Q(c.b);
#undef Q
}

/* r = a * b, row-major, row vectors (D3D's convention). */
static void m4_mul(float *r, const float *a, const float *b)
{
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            r[4 * i + j] = a[4 * i] * b[j] + a[4 * i + 1] * b[4 + j] + a[4 * i + 2] * b[8 + j] +
                           a[4 * i + 3] * b[12 + j];
}

static void v3_norm(float *v)
{
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 0.0f) {
        v[0] /= l;
        v[1] /= l;
        v[2] /= l;
    }
}

/* D3D8 fixed-function vertex lighting (the "Mathematics of Lighting" rules),
 * in camera space as D3D does it, for `count` vertices at `v` (a copy the
 * bridge owns). Each vertex's diffuse becomes
 *   Ce + Ca * Ga + sum over lights of atten * spot * (Ca * La + Cd * Ld * max(N.L, 0)),
 * clamped, with Cd's alpha; C* per RS 101-103 (material or vertex colour).
 * Specular isn't produced (SPECULARENABLE is off from splash to hub; counted
 * if not). Normals are normalized only with NORMALIZENORMALS, as the NV2A. */
static void cpu_light(uint8_t *v, uint32_t count, uint32_t stride, uint32_t fvf, const d3dh_ff_t *ff)
{
    const float *mat = (const float *)ff->material;     /* Diffuse 0, Ambient 4, Specular 8, Emissive 12 */
    uint32_t off_n = 12, off_d = 12 + ((fvf & FVF_NORMAL) ? 12 : 0) + ((fvf & FVF_PSIZE) ? 4 : 0);
    uint32_t off_s = off_d + 4, i, k;
    int src_d = color_source(XRS_DIFFUSEMATERIALSOURCE, fvf), src_a = color_source(XRS_AMBIENTMATERIALSOURCE, fvf);
    int src_e = color_source(XRS_EMISSIVEMATERIALSOURCE, fvf), norm = rs_x(XRS_NORMALIZENORMALS) != 0;
    float wv[16], nm[9], det, lp[D3DH_MAX_LIGHTS][3], ld[D3DH_MAX_LIGHTS][3];
    col_t ga = argb_col(rs_x(XRS_AMBIENT)), mc[4];

    if (rs_x(XRS_SPECULARENABLE))
        gap(GAP_SPECULAR, "SPECULARENABLE on a draw lit on the CPU", 1);
    for (k = 0; k < 4; k++) {
        mc[k].r = mat[4 * k];
        mc[k].g = mat[4 * k + 1];
        mc[k].b = mat[4 * k + 2];
        mc[k].a = mat[4 * k + 3];
    }
    m4_mul(wv, ff->world, ff->view);
    /* Normals: the inverse transpose of world*view's 3x3 (row vectors), nm. */
    det = wv[0] * (wv[5] * wv[10] - wv[6] * wv[9]) - wv[1] * (wv[4] * wv[10] - wv[6] * wv[8]) +
          wv[2] * (wv[4] * wv[9] - wv[5] * wv[8]);
    if (fabsf(det) < 1e-20f)
        det = 1.0f;
    nm[0] = (wv[5] * wv[10] - wv[6] * wv[9]) / det;     /* cofactors / det = inverse transposed */
    nm[1] = -(wv[4] * wv[10] - wv[6] * wv[8]) / det;
    nm[2] = (wv[4] * wv[9] - wv[5] * wv[8]) / det;
    nm[3] = -(wv[1] * wv[10] - wv[2] * wv[9]) / det;
    nm[4] = (wv[0] * wv[10] - wv[2] * wv[8]) / det;
    nm[5] = -(wv[0] * wv[9] - wv[1] * wv[8]) / det;
    nm[6] = (wv[1] * wv[6] - wv[2] * wv[5]) / det;
    nm[7] = -(wv[0] * wv[6] - wv[2] * wv[4]) / det;
    nm[8] = (wv[0] * wv[5] - wv[1] * wv[4]) / det;
    for (k = 0; k < ff->n_lights; k++) {                 /* position 13-15, direction 16-18 (dwords) */
        const float *L = (const float *)ff->light[k], *vw = ff->view;
        for (i = 0; i < 3; i++) {
            lp[k][i] = L[13] * vw[i] + L[14] * vw[4 + i] + L[15] * vw[8 + i] + vw[12 + i];
            ld[k][i] = L[16] * vw[i] + L[17] * vw[4 + i] + L[18] * vw[8 + i];
        }
        v3_norm(ld[k]);
    }

    for (i = 0; i < count; i++, v += stride) {
        const float *p = (const float *)v;
        float P[3], N[3] = { 0, 0, 0 };
        col_t vd = argb_col(*(const uint32_t *)(v + off_d)), vs = { 0, 0, 0, 0 }, cd, ca, ce, o;
        uint32_t j;

        if (fvf & FVF_SPECULAR)
            vs = argb_col(*(const uint32_t *)(v + off_s));
        cd = src_d == 1 ? vd : src_d == 2 ? vs : mc[0];
        ca = src_a == 1 ? vd : src_a == 2 ? vs : mc[1];
        ce = src_e == 1 ? vd : src_e == 2 ? vs : mc[3];
        for (j = 0; j < 3; j++)
            P[j] = p[0] * wv[j] + p[1] * wv[4 + j] + p[2] * wv[8 + j] + wv[12 + j];
        if (fvf & FVF_NORMAL) {
            const float *n = (const float *)(v + off_n);
            for (j = 0; j < 3; j++)
                N[j] = n[0] * nm[3 * j] + n[1] * nm[3 * j + 1] + n[2] * nm[3 * j + 2];
            if (norm)
                v3_norm(N);
        }
        o.r = ce.r + ca.r * ga.r;
        o.g = ce.g + ca.g * ga.g;
        o.b = ce.b + ca.b * ga.b;
        for (k = 0; k < ff->n_lights; k++) {
            const uint32_t *L = ff->light[k];
            const float *Lf = (const float *)L;          /* Diffuse 1-4, Ambient 9-12, Range 19, Falloff 20,
                                                          * Attenuation 21-23, Theta 24, Phi 25 */
            float Ldir[3], atten = 1.0f, spot = 1.0f, ndl;
            if (L[0] == 3) {                            /* directional */
                Ldir[0] = -ld[k][0];
                Ldir[1] = -ld[k][1];
                Ldir[2] = -ld[k][2];
            } else {                                    /* point 1, spot 2 */
                float dist, den;
                for (j = 0; j < 3; j++)
                    Ldir[j] = lp[k][j] - P[j];
                dist = sqrtf(Ldir[0] * Ldir[0] + Ldir[1] * Ldir[1] + Ldir[2] * Ldir[2]);
                if (dist > Lf[19])
                    continue;
                if (dist > 0.0f)
                    for (j = 0; j < 3; j++)
                        Ldir[j] /= dist;
                den = Lf[21] + Lf[22] * dist + Lf[23] * dist * dist;
                atten = den > 0.0f ? 1.0f / den : 1.0f;
                if (L[0] == 2) {
                    float rho = -(ld[k][0] * Ldir[0] + ld[k][1] * Ldir[1] + ld[k][2] * Ldir[2]);
                    float ct = cosf(Lf[24] * 0.5f), cp = cosf(Lf[25] * 0.5f);
                    if (rho <= cp)
                        continue;
                    if (rho < ct)
                        spot = powf((rho - cp) / (ct - cp), Lf[20]);
                }
            }
            ndl = N[0] * Ldir[0] + N[1] * Ldir[1] + N[2] * Ldir[2];
            if (ndl < 0.0f)
                ndl = 0.0f;
            atten *= spot;
            o.r += atten * (ca.r * Lf[9] + cd.r * Lf[1] * ndl);
            o.g += atten * (ca.g * Lf[10] + cd.g * Lf[2] * ndl);
            o.b += atten * (ca.b * Lf[11] + cd.b * Lf[3] * ndl);
        }
        o.a = cd.a;
        *(uint32_t *)(v + off_d) = col_argb(o);
    }
}

/* The census key for fixed-function draws: lighting, COLORVERTEX, the four
 * material sources (specular, diffuse, ambient, emissive) and the enabled
 * lights by type. */
static void scrape_ff(const char *path)
{
    uint32_t d = dev(), vs = MEM32(d + DEV_VS_HANDLE), l, guard, types[4] = { 0, 0, 0, 0 };

    if ((vs & 1) || (vs & FVF_POSITION_MASK) == FVF_XYZRHW)
        return;
    for (l = MEM32(d + DEV_LIGHTS_ON), guard = 0; l && guard < 256; l = MEM32(l + LIGHT_NEXT), guard++)
        types[MEM32(l) & 3]++;
    cen_addf(1, "ff %s fvf=%X lit=%u cv=%u src=%u%u%u%u lights=p%u.s%u.d%u normalize=%u spec=%u fog=%u",
             path, vs, rs_x(XRS_LIGHTING), rs_x(XRS_COLORVERTEX), rs_x(XRS_SPECULARMATERIALSOURCE),
             rs_x(XRS_DIFFUSEMATERIALSOURCE), rs_x(XRS_AMBIENTMATERIALSOURCE), rs_x(XRS_EMISSIVEMATERIALSOURCE),
             types[1], types[2], types[3], rs_x(XRS_NORMALIZENORMALS), rs_x(XRS_SPECULARENABLE),
             rs_x(XRS_FOGENABLE));
}

static volatile LONG g_cpu_lit_draws, g_host_lit_draws, g_ev_idx, g_ev_idx_ff;

/* CopyRects destinations (step 35, mirror_copy_rects), by Data, so the draws
 * that sample a copy can be counted and the first few logged. */
#define COPY_DST_MAX 8
static uint32_t g_copy_dst[COPY_DST_MAX];
static volatile LONG g_copies, g_copies_skipped, g_copy_sampled, g_ev_copy_host,
    g_ev_copy_draw;

static int copy_dst_bound(void)
{
    uint32_t s, i, data;

    if (!g_copy_dst[0])
        return 0;
    for (s = 0; s < 4; s++) {
        data = res_data(MEM32(dev() + DEV_TEXTURES + 4 * s));
        for (i = 0; data && i < COPY_DST_MAX && g_copy_dst[i]; i++)
            if (g_copy_dst[i] == data)
                return 1;
    }
    return 0;
}

static int g_draw_samples_copy;        /* set by setup_draw for the draw being mirrored */

/* The device's viewport (+0x9D0: X, Y, W, H, MinZ, MaxZ) into a draw. */
static void draw_viewport(d3dh_draw_t *dr)
{
    uint32_t d = dev();

    dr->vp_x = MEM32(d + DEV_VIEWPORT);
    dr->vp_y = MEM32(d + DEV_VIEWPORT + 4);
    dr->vp_w = MEM32(d + DEV_VIEWPORT + 8);
    dr->vp_h = MEM32(d + DEV_VIEWPORT + 12);
    memcpy(&dr->vp_minz, (const void *)XBOX_PTR(d + DEV_VIEWPORT + 16), 4);
    memcpy(&dr->vp_maxz, (const void *)XBOX_PTR(d + DEV_VIEWPORT + 20), 4);
}

/* Everything a mirrored draw needs besides its vertices, shared by the UP and
 * indexed paths: the target, textures and stage states, the pixel shader, the
 * render states, the viewport and, for untransformed vertices, the
 * fixed-function state. Returns 0 (lit by xbox_d3d8 or unlit), 1 (to be lit
 * on the CPU: LIGHTING is already off in `rs`), or -1 (skipped and counted). */
static int setup_draw(d3dh_draw_t *dr, uint32_t fvf, d3dh_tex_t tex[4], d3dh_state_t rs[64],
                      d3dh_state_t tss[4][16], d3dh_ps_t *ps, d3dh_ff_t *ff)
{
    uint32_t d = dev(), s, i;
    int z_off, cpu = 0;

    if (describe_target(&dr->target, &z_off) != 0) {
        skip(SKIP_TARGET, MEM32(d + DEV_RENDER_TARGET));
        return -1;
    }
    if (fvf && (fvf & FVF_POSITION_MASK) != FVF_XYZ && (fvf & FVF_POSITION_MASK) != FVF_XYZRHW) {
        skip(SKIP_FVF, fvf);                /* blend weights (XYZB*); 0 is a vertex program (S5) */
        return -1;
    }
    if (fvf & 0xFFFF0000u)
        gap(GAP_TEXCOORD_SIZE, "texture-coordinate formats in the FVF", fvf);
    for (s = 0; s < 4; s++) {
        int r = describe_texture(s, &tex[s]);
        if (r < 0) {
            uint32_t hdr = MEM32(d + DEV_TEXTURES + 4 * s);
            skip(SKIP_TEXTURE, hdr ? MEM32(hdr + RES_FORMAT) : 0);
            return -1;
        }
        dr->tex[s] = r ? &tex[s] : NULL;
        if (!r && !MEM32(d + DEV_PIXEL_SHADER) && stage_reached(s)) {
            /* Step 49: a stage in use with no texture. The NV2A reads such a
             * stage (texture mode NONE) as opaque black, 0,0,0,1, as xemu
             * models it; an unbound D3D11 view reads 0,0,0,0, so a
             * MODULATE(TEXTURE, DIFFUSE) stage lost the diffuse alpha (Crash's
             * shadow: two of its three darkening passes added nothing). */
            static const uint32_t black = 0xFF000000u;
            memset(&tex[s], 0, sizeof(tex[s]));
            tex[s].key = tex[s].data = NULL_TEXTURE_KEY;
            tex[s].format = 0x06;       /* A8R8G8B8 */
            tex[s].width = tex[s].height = tex[s].levels = 1;
            tex[s].bits = (const uint8_t *)&black;
            tex[s].bytes = 4;
            tex[s].sig = 1;
            dr->tex[s] = &tex[s];
            InterlockedIncrement(&g_null_stages);
        }
        if (r > 0 && tex[s].cube) {
            uint32_t key = MEM32(d + DEV_VS_HANDLE) ^ (MEM32(d + DEV_PIXEL_SHADER) * 31u) ^ (s << 28) ^
                           (rs_x(XRS_PSTEXTUREMODES) * 17u), j;
            InterlockedIncrement(&g_cube_binds);
            for (j = 0; j < CUBE_SEEN_MAX && g_cube_seen[j] && g_cube_seen[j] != key; j++)
                ;
            if (j < CUBE_SEEN_MAX && !g_cube_seen[j]) {         /* each kind of cube draw once */
                g_cube_seen[j] = key;
                event(&g_ev_cube, CUBE_SEEN_MAX, "cube map %08X on stage %u: vertex shader %08X, pixel shader %08X"
                      " (texture modes %08X, dot mapping %X, input texture %X), target %08X, textures %08X %08X"
                      " %08X %08X", tex[s].key, s,
                      MEM32(d + DEV_VS_HANDLE), MEM32(d + DEV_PIXEL_SHADER), rs_x(XRS_PSTEXTUREMODES),
                      rs_x(XPS_DOTMAPPING), rs_x(XPS_INPUTTEXTURE),
                      MEM32(d + DEV_RENDER_TARGET), MEM32(d + DEV_TEXTURES), MEM32(d + DEV_TEXTURES + 4),
                      MEM32(d + DEV_TEXTURES + 8), MEM32(d + DEV_TEXTURES + 12));
            }
        }
        dr->tss[s] = tss[s];
        dr->n_tss[s] = translate_tss(s, tss[s]);
        /* S4: MIPFILTER NONE samples level 0 only in D3D, but xbox_d3d8's
         * sampler still walks the mip chain (MIP_POINT, MaxLOD unbounded).
         * So such a stage gets its texture's level 0 alone. */
        if (r)
            for (i = 0; i < dr->n_tss[s]; i++)
                if (tss[s][i].state == PTSS_MIPFILTER && tss[s][i].value == 0)
                    tex[s].levels = 1;
    }
    if (MEM32(d + DEV_PIXEL_SHADER)) {
        translate_ps(ps);
        dr->ps = ps;
    }
    g_draw_samples_copy = copy_dst_bound();
    if (g_draw_samples_copy)
        InterlockedIncrement(&g_copy_sampled);
    dr->rs = rs;
    dr->n_rs = translate_rs(rs);
    if ((fvf & FVF_POSITION_MASK) == FVF_XYZ) {
        describe_ff(ff);
        dr->ff = ff;
        /* 18.56: a projection the game didn't widen (the menus' and the
         * text's own, perspective or 640x480 ortho: x scale / y scale 0.75)
         * keeps its 4:3 shape, pillarboxed; a widened one (0.5625) fills. */
        if (d3db_wide > 0 && ff->proj[5] != 0.0f && fabs(fabs(ff->proj[0] / ff->proj[5]) - 0.75) < 0.005) {
            dr->pillar = 1;
            InterlockedIncrement(&g_wide_ff_pillar);
        }
        cpu = needs_cpu_lighting(fvf);
    }
    for (i = 0; i < dr->n_rs; i++) {
        if (z_off && (rs[i].state == PRS_ZENABLE || rs[i].state == PRS_ZWRITEENABLE ||
                      rs[i].state == PRS_STENCILENABLE))
            rs[i].value = 0;                /* no guest depth surface: no depth or stencil test */
        if (rs[i].state == PRS_LIGHTING) {
            if (cpu)
                rs[i].value = 0;            /* lit on the CPU: the host draws the result unlit */
            else if (rs[i].value && dr->ff)
                InterlockedIncrement(&g_host_lit_draws);
        }
    }
    dr->fvf = fvf;
    draw_viewport(dr);
    return cpu;
}

/* A copy of `count` vertices lit on the CPU (freed by the caller). */
static uint8_t *cpu_lit_copy(const void *verts, uint32_t count, uint32_t stride, uint32_t fvf, const d3dh_ff_t *ff)
{
    uint8_t *c = (uint8_t *)malloc((size_t)count * stride);

    if (!c)
        return NULL;
    memcpy(c, verts, (size_t)count * stride);
    cpu_light(c, count, stride, fvf, ff);
    InterlockedIncrement(&g_cpu_lit_draws);
    return c;
}

/* Step 50 (notes 18.53): texel_patch's fixed-function side. The NV2A
 * addresses a linear texture in texels and xbox_d3d8 samples over 0..1, so
 * a fixed-function draw (UP, indexed, immediate) with a linear texture on a
 * stage gets its vertices copied (`*owned`, freed by the caller; a CPU-lit
 * copy is reused) with the coordinate set that stage reads (TEXCOORDINDEX)
 * divided by the texture's width and height. H2 Oh No's underwater pass
 * draws its 640x480 linear target over the screen with coordinates 0..639 x
 * 0..479; read as 0..1 and clamped, the screen was one texel's colour. A
 * stage with texture generation, or a set two linear stages read at different
 * sizes, is left as it was and counted. */
static volatile LONG g_ff_texel_draws, g_ff_texel_gaps, g_ev_ff_texel, g_wide_crops;

static void ff_texel_scale(d3dh_draw_t *dr, uint32_t fvf, uint8_t **owned)
{
    static const uint8_t pos[8] = { 0, 12, 16, 16, 20, 24, 28, 32 };
    static const uint8_t tc[4] = { 8, 12, 16, 4 };
    uint32_t sets = (fvf >> 8) & 15, off, set_off[8], done = 0, crop = 0, s, i;
    float sw[8], sh[8], k = d3db_wide > 0 ? (float)((4.0 / 3.0) / d3db_wide) : 1.0f;
    uint8_t *c;

    for (s = 0; s < 4; s++) {
        const d3dh_tex_t *t = dr->tex[s];
        uint32_t tci, set;
        if (!t || !t->pitch)
            continue;                   /* none, or not linear */
        tci = MEM32(D3D_TEXTURESTATE + 4 * (s * D3D_TSS_COUNT + XTSS_TEXCOORDINDEX));
        set = tci & 0xFFFF;
        if ((tci >> 16) || set >= sets || set >= 8) {
            InterlockedIncrement(&g_ff_texel_gaps);
            continue;
        }
        if (done & (1u << set)) {
            if (sw[set] != 1.0f / t->width || sh[set] != 1.0f / t->height)
                InterlockedIncrement(&g_ff_texel_gaps);
            continue;
        }
        done |= 1u << set;
        sw[set] = 1.0f / t->width;
        sh[set] = 1.0f / t->height;
        /* 18.56: a screen-sized texture (the CopyRects copy of a 16:9 frame)
         * drawn into a smaller target (the pause still) keeps its middle 4:3,
         * what the game captured at 4:3, instead of the whole width squeezed. */
        if (d3db_wide > 0 && dr->target.data && t->width == g_guest_w && t->height == g_guest_h &&
            (dr->target.width != g_guest_w || dr->target.height != g_guest_h))
            crop |= 1u << set;
        event(&g_ev_ff_texel, 8, "fixed-function draw (FVF %X): linear texture %08X (%ux%u) on stage %u, coordinate"
              " set %u scaled to 0..1", fvf, t->key, t->width, t->height, s, set);
    }
    if (!done || !dr->vert_count)
        return;
    off = pos[(fvf & FVF_POSITION_MASK) >> 1] + ((fvf & FVF_NORMAL) ? 12 : 0) + ((fvf & FVF_PSIZE) ? 4 : 0) +
          ((fvf & FVF_DIFFUSE) ? 4 : 0) + ((fvf & FVF_SPECULAR) ? 4 : 0);
    for (i = 0; i < sets && i < 8; i++) {
        set_off[i] = off;
        off += tc[(fvf >> (16 + 2 * i)) & 3];
    }
    c = *owned;
    if (!c) {
        c = (uint8_t *)malloc((size_t)dr->vert_count * dr->stride);
        if (!c)
            return;
        memcpy(c, dr->verts, (size_t)dr->vert_count * dr->stride);
        *owned = c;
    }
    for (i = 0; i < 8; i++) {
        uint32_t v, two = ((fvf >> (16 + 2 * i)) & 3) != 3;    /* TEXCOORDSIZE1 has u alone */
        if (!(done & (1u << i)))
            continue;
        for (v = 0; v < dr->vert_count; v++) {
            float *p = (float *)(c + (size_t)v * dr->stride + set_off[i]);
            p[0] *= sw[i];
            if (crop & (1u << i))
                p[0] = 0.5f + (p[0] - 0.5f) * k;
            if (two)
                p[1] *= sh[i];
        }
    }
    dr->verts = c;
    InterlockedIncrement(&g_ff_texel_draws);
    if (crop)
        InterlockedIncrement(&g_wide_crops);
}

static void count_mirrored(const d3dh_draw_t *dr)
{
    InterlockedIncrement(&g_mirrored_draws);
    if (dr->target.data)
        InterlockedIncrement(&g_mirrored_rt_draws);
    if (dr->ps)
        InterlockedIncrement(&g_mirrored_ps_draws);
}

/* ── Vertex programs (S5 part 1) ───────────────────────────────
 *
 * A programmable vertex shader is a D3D object (CreateVertexShader 0x102440;
 * handle = object | 1, current one at device +0x380):
 *   +0x04 flags: the Usage argument, 0x10 has a program, 0x1 not a plain
 *         0x2078 program, 0x8 a read/write (0x7378) one; 0x400/0x800/0x1000/
 *         0x2000 the declaration sets register 3/4/7/8 (SetVertexShader
 *         0x102940 pushes a default for each one it doesn't);
 *   +0x08 instruction count; +0x0C the dwords of its upload at +0x114;
 *   +0x14 + 16 r, register r's declaration (0x1021A0): stream, byte offset in
 *         the stream's vertex, NV2A format (type in bits 0-3, size in 4-7;
 *         0x02 disabled); the byte at +0x20 + 16 r is 1 or 2 when the
 *         tessellator fills the register instead;
 *   +0x114 the upload, as push-buffer: NV097_SET_TRANSFORM_PROGRAM (0xB00) in
 *         runs of up to 32 dwords (0x102130), then the declaration's
 *         constants (CONSTANT_LOAD 0x1EA4 + SET_TRANSFORM_CONSTANT, 0x102340).
 * The microcode is 4 dwords an instruction, as the NV2A takes it.
 * SetVertexShader loads it at program address 0 and starts there
 * (0x102610/0x102670); the load also copies it into the device's program
 * shadow at +0x1798 and the declaration constants into the constant shadow
 * at +0xB98, which SetVertexShaderConstant (0x102AA0) writes at slot
 * reg + 96. The shadow is never skipped from splash to hub (census: device
 * flag 0x10 always clear), so +0xB98 is the NV2A's whole constant memory as
 * the game set it, indexed as the microcode indexes it.
 *
 * Vertex fetch, as the draw's array setup (0x108F40) programs the NV2A:
 * hardware attribute i reads declaration register map[i], the byte table at
 * 0x1B11C8 + (flags & 0x10), from its stream: format (stride << 8) | type,
 * address = the vertex buffer's Data + the stream's offset + the register's
 * offset (+ base vertex * stride for indexed draws). A disabled register
 * (type 0x02) keeps its current value. */
#define DEV_VS_OBJECT       0x380u
#define DEV_VS_CONSTANTS    0xB98u      /* float4[192], by NV2A constant slot */
#define VS_ATTR_MAP         0x001B11C8u
#define VSO_FLAGS           0x04u
#define VSO_COUNT           0x08u
#define VSO_WORDS           0x0Cu
#define VSO_DECL            0x14u       /* 16 bytes a register: stream, offset, format */
#define VSO_TESS            0x20u       /* a byte, 16 bytes a register */
#define VSO_UPLOAD          0x114u
#define VS_MAX_INSNS        136
#define VS_CONSTS           192
#define VS_FMT_DISABLED     0x02u

typedef struct {
    uint32_t obj, flags, n_insns, hash;
    uint32_t code[VS_MAX_INSNS * 4];
    struct {
        uint32_t reg;                   /* the declaration register (map[i]) */
        uint32_t stream, offset, format, tess;
    } attr[16];                         /* by vertex register (hardware attribute) */
} vprog_t;

/* The current vertex shader's program and declaration. Returns 0, or -1 if
 * it has no program or its upload can't be read. */
static int describe_vprog(vprog_t *p)
{
    uint32_t obj = MEM32(dev() + DEV_VS_OBJECT), words, i, n = 0, h = 2166136261u;

    memset(p, 0, sizeof(*p));
    if (!obj)
        return -1;
    p->obj = obj;
    p->flags = MEM32(obj + VSO_FLAGS);
    if (!(p->flags & 0x10))
        return -1;
    words = MEM32(obj + VSO_WORDS);
    for (i = 0; i < words && n < VS_MAX_INSNS * 4;) {
        uint32_t hd = MEM32(obj + VSO_UPLOAD + 4 * i), m = hd & 0x1FFC, c = (hd >> 18) & 0x7FF, k;
        if (!hd || !c)
            break;
        if (m >= 0xB00 && m < 0xB80)
            for (k = 0; k < c && n < VS_MAX_INSNS * 4; k++)
                p->code[n++] = MEM32(obj + VSO_UPLOAD + 4 * (i + 1 + k));
        i += 1 + c;
    }
    p->n_insns = n / 4;
    if (p->n_insns > MEM32(obj + VSO_COUNT))
        p->n_insns = MEM32(obj + VSO_COUNT);
    if (!p->n_insns)
        return -1;
    for (i = 0; i < p->n_insns * 4; i++)
        h = (h ^ p->code[i]) * 16777619u;
    p->hash = h ? h : 1;
    for (i = 0; i < 16; i++) {
        uint32_t r = MEM8(VS_ATTR_MAP + (p->flags & 0x10) + i) & 15, d = obj + VSO_DECL + 16 * r;
        p->attr[i].reg = r;
        p->attr[i].stream = MEM32(d);
        p->attr[i].offset = MEM32(d + 4);
        p->attr[i].format = MEM32(d + 8) & 0xFF;
        p->attr[i].tess = MEM8(obj + VSO_TESS + 16 * r);
    }
    return 0;
}

/* One vertex attribute in an NV2A array format, as the NV2A expands it:
 * missing components are (0, 0, 0, 1). Types: 0 UB_D3D (a D3DCOLOR: B, G,
 * R, A bytes read as x = R), 1 S1 (normalized signed short), 2 F (float;
 * size 7 is FLOAT2H, three floats read as x, y, 0, w), 4 UB_OGL (bytes in
 * order), 5 S32K (signed short, not normalized), 6 CMP (11:11:10 signed
 * normalized). Returns 0, or -1 for a type it doesn't know. */
static int vs_fetch(uint32_t va, uint32_t format, float out[4])
{
    uint32_t type = format & 15, size = format >> 4, k;

    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    switch (type) {
    case 0:                             /* UB_D3D */
        if (size == 4) {
            uint32_t c = MEM32(va);
            out[0] = ((c >> 16) & 255) / 255.0f;
            out[1] = ((c >> 8) & 255) / 255.0f;
            out[2] = (c & 255) / 255.0f;
            out[3] = (c >> 24) / 255.0f;
            return 0;
        }
        for (k = 0; k < size && k < 4; k++)
            out[k] = MEM8(va + k) / 255.0f;
        return 0;
    case 4:                             /* UB_OGL */
        for (k = 0; k < size && k < 4; k++)
            out[k] = MEM8(va + k) / 255.0f;
        return 0;
    case 1:                             /* S1 */
        for (k = 0; k < size && k < 4; k++)
            out[k] = (2.0f * (float)(int16_t)MEM16(va + 2 * k) + 1.0f) / 65535.0f;
        return 0;
    case 5:                             /* S32K */
        for (k = 0; k < size && k < 4; k++)
            out[k] = (float)(int16_t)MEM16(va + 2 * k);
        return 0;
    case 2:                             /* F */
        if (size == 7) {
            memcpy(&out[0], (const void *)XBOX_PTR(va), 8);
            memcpy(&out[3], (const void *)XBOX_PTR(va + 8), 4);
            return 0;
        }
        for (k = 0; k < size && k < 4; k++)
            memcpy(&out[k], (const void *)XBOX_PTR(va + 4 * k), 4);
        return 0;
    case 6: {                           /* CMP: x 11, y 11, z 10 bits, signed */
        uint32_t c = MEM32(va);
        out[0] = (float)((int32_t)(c << 21) >> 21) / 1023.0f;
        out[1] = (float)((int32_t)(c << 10) >> 21) / 1023.0f;
        out[2] = (float)((int32_t)c >> 22) / 511.0f;
        return 0;
    }
    }
    return -1;
}

/* RECOMP_D3D_BRIDGE_VSDUMP (S5, a check): each distinct program, the first
 * time a draw uses it (and, with RECOMP_D3D_BRIDGE_DRAWLOG, every program
 * draw in the draw-log range, named HASH.N after its draw-log number): its microcode, declaration, streams, the whole
 * constant shadow, the viewport, and its first vertices' attributes as the
 * NV2A would fetch them, for tools/diag/vscheck.py to decode and evaluate.
 * `base[s]`/`stride[s]` locate vertex 0 of stream s (UP: stream 0 only);
 * `index` holds the first vertices' indices (NULL: 0, 1, 2...). */
#define VSDUMP_MAX 64
static uint32_t g_vsdump_seen[VSDUMP_MAX];
static int g_vsdump_n;

static void vs_dump_once(const char *path, const vprog_t *p, const float *consts, const uint32_t base[16],
                         const uint32_t stride[16], const uint16_t *index, uint32_t nv, int draw_no)
{
    char *line, id[24];
    int len, k;
    uint32_t i, v, d = dev();

    if (draw_no < 0) {                  /* once per program */
        for (k = 0; k < g_vsdump_n; k++)
            if (g_vsdump_seen[k] == p->hash)
                return;
        if (g_vsdump_n >= VSDUMP_MAX)
            return;
        g_vsdump_seen[g_vsdump_n++] = p->hash;
        snprintf(id, sizeof(id), "%08X", p->hash);
    } else {                            /* a draw in the draw-log range: named by its draw-log number */
        snprintf(id, sizeof(id), "%08X.%d", p->hash, draw_no);
    }
    line = (char *)malloc(16384);
    if (!line)
        return;
    fprintf(stderr, "[D3DB] vsdump %s path %s swap %ld obj %08X flags %08X insns %u words %u start %u\n", id,
            path, (long)g_swaps + 1, p->obj, p->flags, p->n_insns, MEM32(p->obj + VSO_WORDS), MEM32(d + 0x388));
    for (len = 0, i = 0; i < p->n_insns * 4; i++)
        len += snprintf(line + len, 16384 - len, " %08X", p->code[i]);
    fprintf(stderr, "[D3DB] vsdump %s code%s\n", id, line);
    for (i = 0; i < 16; i++)
        if (p->attr[i].format != VS_FMT_DISABLED || p->attr[i].tess)
            fprintf(stderr, "[D3DB] vsdump %s attr v%u reg %u stream %u offset %u format %02X tess %u stride %u\n",
                    id, i, p->attr[i].reg, p->attr[i].stream, p->attr[i].offset, p->attr[i].format,
                    p->attr[i].tess, stride[p->attr[i].stream & 15]);
    for (len = 0, i = 0; i < VS_CONSTS * 4; i++) {
        uint32_t u;
        memcpy(&u, &consts[i], 4);
        len += snprintf(line + len, 16384 - len, " %08X", u);
    }
    fprintf(stderr, "[D3DB] vsdump %s const%s\n", id, line);
    fprintf(stderr, "[D3DB] vsdump %s viewport %u %u %u %u %08X %08X\n", id, MEM32(d + DEV_VIEWPORT),
            MEM32(d + DEV_VIEWPORT + 4), MEM32(d + DEV_VIEWPORT + 8), MEM32(d + DEV_VIEWPORT + 12),
            MEM32(d + DEV_VIEWPORT + 16), MEM32(d + DEV_VIEWPORT + 20));
    for (v = 0; v < nv && v < 8; v++) {
        uint32_t ix = index ? index[v] : v;
        for (len = 0, i = 0; i < 16; i++) {
            float f[4];
            uint32_t s = p->attr[i].stream & 15;
            if (p->attr[i].format == VS_FMT_DISABLED || !base[s])
                continue;
            if (vs_fetch(base[s] + ix * stride[s] + p->attr[i].offset, p->attr[i].format, f) != 0)
                len += snprintf(line + len, 16384 - len, " v%u=?", i);
            else
                len += snprintf(line + len, 16384 - len, " v%u=%.9g,%.9g,%.9g,%.9g", i, (double)f[0], (double)f[1],
                                (double)f[2], (double)f[3]);
        }
        fprintf(stderr, "[D3DB] vsdump %s vertex %u index %u:%s\n", id, v, ix, line);
    }
    free(line);
}

/* Vertex 0 of each stream for an indexed draw: the vertex buffer's Data,
 * the stream's offset and the base vertex (device +0x1C), as 0x108F40. */
/* RECOMP_D3D_BRIDGE_MOTIONLOG=PATH (step 42, notes 18.44): while PATH.on exists (checked at each swap),
 * one compact line per mirrored program or fixed-function indexed draw goes to PATH, and one per swap with
 * the game's frame counter, so draw-time data can be followed from frame to frame without the drawlog's
 * cost. Program draws: hash, vertex range, a hash of the constants (c58/c59 left out: the bridge computes
 * them), a hash of the fetched vertices, vertex lo's first input register, constant slots 96-99 (the
 * XDK's c0-c3: every program seen reads its transform there) and 124-126 (the skinned programs' first
 * bone, c28-c30), so a draw's screen anchor can be projected. Fixed-function draws: FVF, vertex range,
 * texture 0, the world matrix's translation, a hash of the vertices and vertex lo's position. A test
 * switch: off, nothing is checked. */

static uint32_t fnv32(const void *p, size_t n, uint32_t h)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}

static void motion_swap(LONG n)
{
    if (!g_motion)
        return;
    if (g_motion_live)
        fprintf(g_motion, "S %ld %u %.4f\n", (long)n, MEM32(GAME_FRAME_COUNTER), now_s());
    g_motion_live = GetFileAttributesA(g_motion_on_path) != INVALID_FILE_ATTRIBUTES;
    g_motion_n = 0;
    fflush(g_motion);
}

/* RECOMP_D3D_BRIDGE_DRAWLOG_TRIGGER (step 49, a check): when the file
 * appears, the draw log (and the host's dumps) cover the next N swaps, N
 * read from the file (default 3); the file is then deleted. For a scene
 * someone reaches by hand, where the swap numbers aren't known in advance. */
static void drawlog_trigger(LONG n)
{
    static unsigned polls;
    FILE *f;
    long k = 3;

    if (!g_drawlog_trigger[0] || ++polls % 15)
        return;
    f = fopen(g_drawlog_trigger, "r");
    if (!f)
        return;
    if (fscanf(f, "%ld", &k) != 1 || k < 1 || k > 600)
        k = 3;
    fclose(f);
    DeleteFileA(g_drawlog_trigger);
    g_drawlog_from = n + 1;
    g_drawlog_to = n + k;
    d3dh_dump_range((unsigned)(n + 1), (unsigned)(n + k));
    fprintf(stderr, "[D3DB] draw log triggered: swaps %ld-%ld\n", (long)(n + 1), (long)(n + k));
}

static void stream_bases(uint32_t base[16], uint32_t stride[16])
{
    uint32_t s, bv = MEM32(dev() + 0x01Cu);

    for (s = 0; s < 16; s++) {
        uint32_t vb = MEM32(D3D_STREAMS + 12 * s + 8);
        stride[s] = MEM32(D3D_STREAMS + 12 * s);
        base[s] = vb ? phys_to_va(MEM32(vb + RES_DATA)) + MEM32(D3D_STREAMS + 12 * s + 4) + bv * stride[s] : 0;
    }
}

static int drawlog_on(void);
static unsigned g_drawlog_n;
static void drawlog(const char *path, uint32_t xdk_prim, uint32_t n, uint32_t fvf, const void *verts, uint32_t nv,
                    uint32_t stride, const char *outcome);
static uint32_t index_list(uint32_t xdk, const uint16_t *in, uint32_t n, uint32_t lo, uint16_t *out,
                           uint32_t *prim, uint32_t *count);

static float memf(uint32_t va)
{
    float f;
    memcpy(&f, (const void *)XBOX_PTR(va), 4);
    return f;
}

/* What the program mirror couldn't do exactly, counted: a register the
 * program reads that its declaration leaves out (the NV2A's current value
 * is assumed), one on a stream with no vertex buffer, an array type
 * vs_fetch doesn't know, a tessellator register, and a device in
 * D3DSCM_NORESERVEDCONSTANTS mode (c58/c59 unknown). */
enum { VPGAP_UNDECLARED, VPGAP_STREAM, VPGAP_FORMAT, VPGAP_TESS, VPGAP_NORESERVED, VPGAP_COUNT };
static volatile LONG g_vpgap[VPGAP_COUNT], g_ev_vpgap, g_mirrored_vp, g_ev_vp;
static const char *const g_vpgap_name[VPGAP_COUNT] = {
    "undeclared register", "stream without a buffer", "array type", "tessellator register", "NORESERVEDCONSTANTS",
};

static void vpgap(int kind, uint32_t hash, uint32_t v)
{
    InterlockedIncrement(&g_vpgap[kind]);
    event(&g_ev_vpgap, 20, "vertex program %08X: %s (%X)", hash, g_vpgap_name[kind], v);
}

/* The constants a program draw sees: the shadow, with c58 and c59, the
 * viewport scale and offset, as D3D pushes them for a program (0xFE2B0:
 * NV097_SET_VIEWPORT_SCALE and _OFFSET, which the NV2A's programs read as
 * those two constants; the XDK's reserved c-38 and c-37). From the device's
 * viewport (+0x9D0: X, Y, W, H, MinZ, MaxZ), its super-sample scale
 * (+0x458/+0x45C), its z scale (+0x450) and screen offset (+0x9E8/+0x9EC),
 * and D3D's constants 0.5 (0x15AF60) and the y factor (0x15F278):
 *   ox = X * ssx + offx, oy = Y * ssy + offy (both - 0.5 with device flag
 *   0x8000 and [0x10F02C] set), sx = W * ssx * 0.5, sy = H * ssy * ky;
 *   scale = (sx, sy, (MaxZ - MinZ) * zs, 0), offset = (ox + sx, oy - sy, MinZ * zs, 0). */
static void vp_constants(float *c)
{
    uint32_t d = dev(), flags = MEM32(d + DEV_FLAGS);
    float half = memf(0x0015AF60u), ky = memf(0x0015F278u), zs = memf(d + 0x450);
    float ssx = memf(d + 0x458), ssy = memf(d + 0x45C);
    float x = (float)MEM32(d + DEV_VIEWPORT), y = (float)MEM32(d + DEV_VIEWPORT + 4);
    float w = (float)MEM32(d + DEV_VIEWPORT + 8), h = (float)MEM32(d + DEV_VIEWPORT + 12);
    float minz = memf(d + DEV_VIEWPORT + 16), maxz = memf(d + DEV_VIEWPORT + 20);
    float ox = x * ssx + memf(d + 0x9E8), oy = y * ssy + memf(d + 0x9EC), sx, sy;

    memcpy(c, (const void *)XBOX_PTR(d + DEV_VS_CONSTANTS), VS_CONSTS * 16);
    if (flags & 0x200) {                /* the game keeps c58/c59 itself: never seen */
        vpgap(VPGAP_NORESERVED, 0, flags);
        return;
    }
    if ((flags & 0x8000) && MEM32(0x0010F02Cu)) {
        ox -= half;
        oy -= half;
    }
    sx = w * ssx * half;
    sy = h * ssy * ky;
    c[58 * 4 + 0] = sx;
    c[58 * 4 + 1] = sy;
    c[58 * 4 + 2] = (maxz - minz) * zs;
    c[58 * 4 + 3] = 0.0f;
    c[59 * 4 + 0] = ox + sx;
    c[59 * 4 + 1] = oy - sy;
    c[59 * 4 + 2] = minz * zs;
    c[59 * 4 + 3] = 0.0f;
}

/* Vertices [lo, lo + n) of a program draw in the host's layout: a float4
 * for each register in `inputs`, ascending, each fetched from its stream
 * and expanded as the NV2A does (vs_fetch). A register the declaration
 * leaves out reads what SetVertexShader leaves in it: white for the
 * diffuse registers 3 and 7, 0 for the speculars 4 and 8 (0x102940), and
 * (0, 0, 0, 1) for the rest (counted). Freed by the caller. */
static float *vp_vertices(const vprog_t *p, uint16_t inputs, const uint32_t base[16], const uint32_t stride[16],
                          uint32_t lo, uint32_t n, uint32_t *vstride)
{
    uint32_t regs[16], nr = 0, r, i, k;
    float *out;

    for (r = 0; r < 16; r++)
        if (inputs & (1u << r))
            regs[nr++] = r;
    *vstride = 16 * nr;
    out = (float *)malloc((size_t)n * nr * 16 + 16);
    if (!out)
        return NULL;
    for (k = 0; k < nr; k++) {
        uint32_t fmt = p->attr[regs[k]].format, s = p->attr[regs[k]].stream & 15, va;
        r = regs[k];
        if (fmt == VS_FMT_DISABLED || p->attr[r].tess || !base[s]) {
            float def[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            if (r == 3 || r == 7)
                def[0] = def[1] = def[2] = 1.0f;
            else if (r == 4 || r == 8)
                def[3] = 0.0f;
            else
                vpgap(VPGAP_UNDECLARED, p->hash, r);
            if (p->attr[r].tess)
                vpgap(VPGAP_TESS, p->hash, r);
            else if (fmt != VS_FMT_DISABLED)
                vpgap(VPGAP_STREAM, p->hash, s);
            for (i = 0; i < n; i++)
                memcpy(out + ((size_t)i * nr + k) * 4, def, 16);
            continue;
        }
        va = base[s] + p->attr[r].offset + lo * stride[s];
        for (i = 0; i < n; i++, va += stride[s])
            if (vs_fetch(va, fmt, out + ((size_t)i * nr + k) * 4) != 0) {
                vpgap(VPGAP_FORMAT, p->hash, fmt);
                break;
            }
    }
    return out;
}

/* RECOMP_D3D_BRIDGE_VSPROBE (S5, a check): each program's first draw
 * (the one RECOMP_D3D_BRIDGE_VSDUMP dumps), the same first vertices drawn
 * alone as points by the host with the translated shader, and where each
 * one landed; tools/diag/vscheck.py --probe compares that with its own CPU
 * evaluation of the dumped program. */
static uint32_t g_vsprobe_seen[VSDUMP_MAX];
static int g_vsprobe_n;

static void vs_probe_once(const vprog_t *p, const d3dh_vprog_t *hv, const float *verts, uint32_t vstride,
                          const d3dh_draw_t *dr, const uint16_t *idx, uint32_t lo, uint32_t n)
{
    uint32_t v;
    int k;

    for (k = 0; k < g_vsprobe_n; k++)
        if (g_vsprobe_seen[k] == p->hash)
            return;
    if (g_vsprobe_n >= VSDUMP_MAX)
        return;
    g_vsprobe_seen[g_vsprobe_n++] = p->hash;
    for (v = 0; v < n && v < 8; v++) {
        uint32_t at = idx ? idx[v] - lo : v, argb = 0;
        int px = -1, py = -1, lit = d3dh_vs_probe(hv, (const uint8_t *)verts + (size_t)at * vstride, vstride, dr,
                                                  &px, &py, &argb);
        fprintf(stderr, "[D3DB] vsprobe %08X vertex %u: lit %d pixel %d %d colour %08X; viewport %u %u %u %u\n",
                p->hash, v, lit, px, py, argb, dr->vp_x, dr->vp_y, dr->vp_w, dr->vp_h);
    }
}

/* S5 part 2 (notes 18.39): the NV2A addresses a linear texture in texels,
 * while xbox_d3d8 samples every texture over 0..1. A program draw with a
 * linear texture on stage n (the hub's water refraction C6C8F337 reads the
 * CopyRects copy at oPos.xy plus a ripple) gets one more instruction,
 * MUL oTn.xy, Rk, c[K] with c[K] = (1/w, 1/h, 1, 1), and each instruction
 * that writes oTn.xy also writes the free temp Rk. Rk is a temp the program
 * never touches and K a constant it doesn't read (nor within 64 above a
 * relative read). Returns the number of stages patched, or -1 if a writer
 * can't also write a temp or nothing is free (the caller then skips a draw
 * that samples a copy, as before). Microcode fields as in vscheck.py. */
#define VF(w, sh, n) (((w) >> (sh)) & ((1u << (n)) - 1))
static volatile LONG g_texel_draws, g_texel_fails, g_ev_texel;

static int texel_patch(vprog_t *p, float *consts)
{
    uint32_t used = 1u << 12, cread[VS_CONSTS / 32 + 1] = { 0 }, i, s, patched = 0, h = 2166136261u;
    uint32_t touch[VS_MAX_INSNS];       /* the temps each instruction reads or writes (step 49) */
    int rel_lo = VS_CONSTS;

    for (i = 0; i < p->n_insns; i++) {
        const uint32_t *w = p->code + 4 * i;
        uint32_t mac = VF(w[1], 21, 4), ilu = VF(w[1], 25, 3), cm = VF(w[3], 28, 2);
        uint32_t am = VF(w[2], 26, 2), bm = VF(w[2], 11, 2), c = VF(w[1], 13, 8), t = 0;
        if (am == 1) t |= 1u << VF(w[2], 28, 4);
        if (bm == 1) t |= 1u << VF(w[2], 13, 4);
        if (cm == 1) t |= 1u << ((VF(w[2], 0, 2) << 2) | VF(w[3], 30, 2));
        if (mac && VF(w[3], 24, 4)) t |= 1u << VF(w[3], 20, 4);
        if (ilu && VF(w[3], 16, 4)) t |= 1u << (mac ? 1 : VF(w[3], 20, 4));
        touch[i] = t;
        used |= t;
        if (am == 3 || bm == 3 || cm == 3) {
            if (VF(w[3], 1, 1)) {
                if ((int)c < rel_lo)
                    rel_lo = (int)c;
            } else if (c < VS_CONSTS) {
                cread[c / 32] |= 1u << (c % 32);
            }
        }
    }
    for (s = 0; s < 4; s++) {
        uint32_t hdr = MEM32(dev() + DEV_TEXTURES + 4 * s), sz = hdr ? MEM32(hdr + RES_SIZE) : 0, k, K, n = 0;
        uint32_t first = 0, late = 0;
        if (!sz)
            continue;                   /* none, or swizzled / compressed: 0..1 as on the host */
        for (i = 0; i < p->n_insns; i++) {
            uint32_t w3 = p->code[4 * i + 3], w1 = p->code[4 * i + 1];
            if (!VF(w3, 11, 1) || VF(w3, 3, 8) != 9 + s || !(VF(w3, 12, 4) & 0xC))
                continue;
            if (!n++)
                first = i;
            if (VF(w3, 2, 1) ? (VF(w1, 21, 4) || VF(w3, 16, 4)) : VF(w3, 24, 4))
                return -1;              /* its unit already writes a temp (or the ILU's is R1) */
        }
        if (!n)
            continue;                   /* the program doesn't write oTn.xy */
        for (i = first + 1; i < p->n_insns; i++)
            late |= touch[i];
        /* A temp the program never touches; else (step 49: Wa-Wa's programs
         * use R0-R11) one nothing reads or writes after the first writer. */
        for (k = 11; k >= 2 && (used & (1u << k)); k--)
            ;
        if (k < 2)
            for (k = 11; k >= 2 && (late & (1u << k)); k--)
                ;
        for (K = VS_CONSTS - 1; K > 59; K--)
            if (!(cread[K / 32] & (1u << (K % 32))) && !((int)K >= rel_lo && (int)K < rel_lo + 64))
                break;
        if (k < 2 || K <= 59 || p->n_insns >= VS_MAX_INSNS)
            return -1;
        used |= 1u << k;
        for (i = first; i < p->n_insns; i++)
            touch[i] |= 1u << k;        /* taken from the first writer on, for the next stage */
        touch[p->n_insns] = 1u << k;    /* the MUL appended below */
        cread[K / 32] |= 1u << (K % 32);
        for (i = 0; i < p->n_insns; i++) {
            uint32_t *w = p->code + 4 * i, om = VF(w[3], 12, 4);
            if (!VF(w[3], 11, 1) || VF(w[3], 3, 8) != 9 + s || !(om & 0xC))
                continue;
            if (VF(w[3], 2, 1))         /* the ILU alone: its temp write goes to R[r] */
                w[3] = (w[3] & ~(0xFu << 16) & ~(0xFu << 20)) | (om << 16) | (k << 20);
            else
                w[3] = (w[3] & ~(0xFu << 24) & ~(0xFu << 20)) | (om << 24) | (k << 20);
        }
        p->code[4 * (p->n_insns - 1) + 3] &= ~1u;          /* no longer the final one */
        {
            uint32_t *w = p->code + 4 * p->n_insns;
            w[0] = 0;
            w[1] = (2u << 21) | (K << 13) | 0x1Bu;          /* MUL; A swizzle xyzw */
            w[2] = (k << 28) | (1u << 26) | (0x1Bu << 17) | (3u << 11) | (0x1Bu << 2);  /* A = Rk, B = c[K] */
            w[3] = (1u << 28) | (0xCu << 12) | (1u << 11) | ((9 + s) << 3) | 1u;       /* oTn.xy, final */
        }
        p->n_insns++;
        consts[4 * K + 0] = 1.0f / (float)((sz & 0xFFF) + 1);
        consts[4 * K + 1] = 1.0f / (float)(((sz >> 12) & 0xFFF) + 1);
        consts[4 * K + 2] = 1.0f;
        consts[4 * K + 3] = 1.0f;
        event(&g_ev_texel, 8, "vertex program %08X: linear texture %08X (%ux%u) on stage %u, oT%u.xy scaled to 0..1"
              " (%u writers, temp R%u, constant %u)", p->hash, hdr, (sz & 0xFFF) + 1, ((sz >> 12) & 0xFFF) + 1, s, s,
              n, k, K);
        patched++;
    }
    if (patched) {
        for (i = 0; i < p->n_insns * 4; i++)
            h = (h ^ p->code[i]) * 16777619u;
        p->hash = h ? h : 1;            /* its own host shader */
    }
    return (int)patched;
}
/* Step 48 (notes 18.51): point sprites. The NV2A draws a point as a square
 * oPts.x pixels wide (the program's size, else RS POINTSIZE), and with
 * POINTSPRITEENABLE its texture coordinates run 0..1 across it; D3D11 draws
 * a point as one pixel. So a program point list becomes a quad per point:
 * each point's vertex four times with one more input register vC = (corner x,
 * corner y, u, v), corners +-0.5, and the program gets
 *   MAD oPos.xy, vC, Rk.xxxx, R12      (Rk: the size; oPos is in screen pixels)
 *   MOV oT0.xy, vC.zwzw                (with POINTSPRITEENABLE)
 * appended, each instruction that writes oPts.x also writing the free temp
 * Rk (or, with no writer, Rk read as c[K] = POINTSIZE). Returns the input
 * register used for vC, or -1 when nothing is free (the draw then goes out
 * as points, counted). Microcode fields as in texel_patch. */
static volatile LONG g_sprite_draws, g_sprite_fails, g_ev_sprite;

static int sprite_patch(vprog_t *p, float *consts, int sprite)
{
    uint32_t used = 1u << 12, vin = 1, cread[VS_CONSTS / 32 + 1] = { 0 }, i, k, v, K = 0, writers = 0;
    uint32_t h = 2166136261u, *w;
    int rel_lo = VS_CONSTS;

    for (i = 0; i < p->n_insns; i++) {
        const uint32_t *x = p->code + 4 * i;
        uint32_t mac = VF(x[1], 21, 4), ilu = VF(x[1], 25, 3), cm = VF(x[3], 28, 2);
        uint32_t am = VF(x[2], 26, 2), bm = VF(x[2], 11, 2), c = VF(x[1], 13, 8);
        if (am == 1) used |= 1u << VF(x[2], 28, 4);
        if (bm == 1) used |= 1u << VF(x[2], 13, 4);
        if (cm == 1) used |= 1u << ((VF(x[2], 0, 2) << 2) | VF(x[3], 30, 2));
        if (am == 2 || bm == 2 || cm == 2) vin |= 1u << VF(x[1], 9, 4);
        if (mac && VF(x[3], 24, 4)) used |= 1u << VF(x[3], 20, 4);
        if (ilu && VF(x[3], 16, 4)) used |= 1u << (mac ? 1 : VF(x[3], 20, 4));
        if (am == 3 || bm == 3 || cm == 3) {
            if (VF(x[3], 1, 1)) {
                if ((int)c < rel_lo)
                    rel_lo = (int)c;
            } else if (c < VS_CONSTS) {
                cread[c / 32] |= 1u << (c % 32);
            }
        }
        if (VF(x[3], 11, 1) && VF(x[3], 3, 8) == 6 && (VF(x[3], 12, 4) & 8)) {
            if (VF(x[3], 2, 1) ? (mac || VF(x[3], 16, 4)) : VF(x[3], 24, 4))
                return -1;              /* its unit already writes a temp (or the ILU's is R1) */
            writers++;
        }
    }
    for (k = 12; k-- > 0 && (used & (1u << k));)
        ;
    for (v = 15; v > 0 && (vin & (1u << v)); v--)
        ;
    if (k > 11 || !v || p->n_insns + 2 > VS_MAX_INSNS)
        return -1;
    if (!writers) {
        for (K = VS_CONSTS - 1; K > 59; K--)
            if (!(cread[K / 32] & (1u << (K % 32))) && !((int)K >= rel_lo && (int)K < rel_lo + 64))
                break;
        if (K <= 59)
            return -1;
        memcpy(&consts[4 * K], (const void *)XBOX_PTR(D3D_RENDERSTATE + 4 * XRS_POINTSIZE), 4);
    }
    for (i = 0; i < p->n_insns; i++) {
        uint32_t om;
        w = p->code + 4 * i;
        om = VF(w[3], 12, 4);
        if (!VF(w[3], 11, 1) || VF(w[3], 3, 8) != 6 || !(om & 8))
            continue;
        if (VF(w[3], 2, 1))             /* the ILU alone: its temp write goes to R[r] */
            w[3] = (w[3] & ~(0xFu << 16) & ~(0xFu << 20)) | (8u << 16) | (k << 20);
        else
            w[3] = (w[3] & ~(0xFu << 24) & ~(0xFu << 20)) | (8u << 24) | (k << 20);
    }
    p->code[4 * (p->n_insns - 1) + 3] &= ~1u;          /* no longer the final one */
    w = p->code + 4 * p->n_insns++;
    w[0] = 0;
    w[1] = (4u << 21) | (K << 13) | (v << 9) | 0x1Bu;                 /* MAD; A = vC.xyzw */
    w[2] = (2u << 26) | (writers ? (k << 13) | (1u << 11) : (3u << 11)) | (0x1Bu << 2) | 3u;  /* B = Rk.xxxx or
                                                                          c[K].xxxx, C = R12 (high bits) */
    w[3] = (1u << 28) | (0xCu << 12) | (1u << 11) | (sprite ? 0u : 1u);  /* C = R12 (low bits 0), oPos.xy */
    if (sprite) {
        w = p->code + 4 * p->n_insns++;
        w[0] = 0;
        w[1] = (1u << 21) | (v << 9) | 0xBBu;                         /* MOV; A = vC.zwzw */
        w[2] = (2u << 26) | (0x1Bu << 17) | (1u << 11) | (0x1Bu << 2);
        w[3] = (1u << 28) | (0xCu << 12) | (1u << 11) | (9u << 3) | 1u;    /* oT0.xy, final */
    }
    for (i = 0; i < p->n_insns * 4; i++)
        h = (h ^ p->code[i]) * 16777619u;
    p->hash = h ? h : 1;
    return (int)v;
}

/* With POINTSPRITEENABLE and no pixel shader, D3D's texture-stage combiner
 * starts at stage 3 (sub_0010A9B0: the NV2A generates sprite coordinates
 * for texture 3), so the sprite takes stage 3's state and texture and
 * stages 0-2 aren't combined. The host gets stage 3 as its stage 0, on the
 * coordinates sprite_patch writes to oT0, and nothing after it. */
static void sprite_stages(d3dh_draw_t *dr, d3dh_state_t tss[4][16])
{
    unsigned i, s;

    dr->tex[0] = dr->tex[3];
    memcpy(tss[0], tss[3], sizeof(tss[0]));
    dr->n_tss[0] = dr->n_tss[3];
    for (i = 0; i < dr->n_tss[0]; i++)
        if (tss[0][i].state == PTSS_TEXCOORDINDEX)
            tss[0][i].value = 0;
    for (s = 1; s < 4; s++) {
        dr->tex[s] = NULL;
        for (i = 0; i < dr->n_tss[s]; i++)
            if (tss[s][i].state == PTSS_COLOROP || tss[s][i].state == PTSS_ALPHAOP)
                tss[s][i].value = 1;    /* DISABLE */
    }
}

/* The quads for sprite_patch: each of `n` point records (`nr` float4
 * registers) four times, with vC's float4 inserted at column `col`, and six
 * indices a quad. Freed by the caller. */
static float *sprite_vertices(const float *pts, uint32_t n, uint32_t nr, uint32_t col, uint16_t **list)
{
    static const float corner[4][4] = {
        { -0.5f, -0.5f, 0.0f, 0.0f }, { 0.5f, -0.5f, 1.0f, 0.0f }, { 0.5f, 0.5f, 1.0f, 1.0f }, { -0.5f, 0.5f, 0.0f, 1.0f },
    };
    float *out = (float *)malloc((size_t)n * 4 * (nr + 1) * 16);
    uint32_t i, c;

    *list = (uint16_t *)malloc((size_t)n * 6 * sizeof(uint16_t));
    if (!out || !*list) {
        free(out);
        free(*list);
        *list = NULL;
        return NULL;
    }
    for (i = 0; i < n; i++) {
        const float *src = pts + (size_t)i * nr * 4;
        for (c = 0; c < 4; c++) {
            float *dst = out + ((size_t)i * 4 + c) * (nr + 1) * 4;
            memcpy(dst, src, (size_t)col * 16);
            memcpy(dst + 4 * col, corner[c], 16);
            memcpy(dst + 4 * (col + 1), src + 4 * col, (size_t)(nr - col) * 16);
        }
        (*list)[6 * i + 0] = (uint16_t)(4 * i);
        (*list)[6 * i + 1] = (uint16_t)(4 * i + 1);
        (*list)[6 * i + 2] = (uint16_t)(4 * i + 2);
        (*list)[6 * i + 3] = (uint16_t)(4 * i);
        (*list)[6 * i + 4] = (uint16_t)(4 * i + 2);
        (*list)[6 * i + 5] = (uint16_t)(4 * i + 3);
    }
    return out;
}
#undef VF

/* A draw with a vertex program (S5 part 1), UP (`idx` NULL: vertices 0 to
 * n - 1 of stream 0 at base[0]) or indexed (`n` 16-bit indices, streams at
 * base[] with the base vertex applied). The program, its declaration and
 * the constants are read from D3D's own state; the vertex range the draw
 * uses is fetched and expanded on the CPU, so any number of streams
 * interleave into the one host stream. Drawn with xbox_d3d8's translated
 * shader. A linear texture on a stage (the CopyRects copy) is addressed in
 * texels, so the program is patched to scale those coordinates (S5 part 2,
 * texel_patch); a draw that samples a copy and can't be patched is skipped
 * and counted. */
static void mirror_draw_vp(const char *path, uint32_t xdk_prim, uint32_t n, const uint16_t *idx,
                           const uint32_t base[16], const uint32_t stride[16])
{
    float consts[VS_CONSTS * 4], *verts;
    vprog_t p;
    d3dh_vprog_t hv;
    d3dh_state_t rs[64], tss[4][16];
    d3dh_tex_t tex[4];
    d3dh_ps_t ps;
    d3dh_draw_t dr;
    uint16_t inputs, *list = NULL;
    uint32_t lo = 0, hi = 0, nv, prim = 0, count = 0, k = 0, i, vstride;
    int texel, vc = -1;

    if (describe_vprog(&p) != 0) {
        skip(SKIP_VS_PROGRAM, MEM32(dev() + DEV_VS_HANDLE));
        return;
    }
    vp_constants(consts);
    if (g_vsdump) {
        vs_dump_once(path, &p, consts, base, stride, idx, n, -1);
        if (drawlog_on())
            vs_dump_once(path, &p, consts, base, stride, idx, n, (int)g_drawlog_n);
    }
    texel = texel_patch(&p, consts);    /* after the dump: vscheck.py checks the game's own program */
    if (texel > 0)
        InterlockedIncrement(&g_texel_draws);
    else if (texel < 0)
        InterlockedIncrement(&g_texel_fails);
    if (xdk_prim == 1 && !idx && n <= 16383) {          /* a point list: quads (step 48) */
        vc = sprite_patch(&p, consts, rs_x(XRS_POINTSPRITEENABLE) != 0);
        if (vc < 0)
            InterlockedIncrement(&g_sprite_fails);
    }
    hv.key = p.hash;
    hv.n_insns = p.n_insns;
    hv.code = p.code;
    hv.consts = consts;
    inputs = d3dh_vs_inputs(&hv);
    if (!inputs || !n) {
        skip(SKIP_VS_PROGRAM, p.hash);
        return;
    }
    if (idx) {
        lo = 0xFFFF;
        for (i = 0; i < n; i++) {
            if (idx[i] < lo)
                lo = idx[i];
            if (idx[i] > hi)
                hi = idx[i];
        }
        list = (uint16_t *)malloc((3 * (size_t)n + 6) * sizeof(uint16_t));
        if (!list)
            return;
        k = index_list(xdk_prim, idx, n, lo, list, &prim, &count);
        nv = hi - lo + 1;
    } else {
        prim = xl_prim(xdk_prim, n, &count);
        nv = n;
    }
    if (!prim || !count || (idx && !k)) {
        skip(SKIP_PRIM, xdk_prim);
        free(list);
        return;
    }
    verts = vp_vertices(&p, vc >= 0 ? inputs & ~(1u << vc) : inputs, base, stride, lo, nv, &vstride);
    if (!verts) {
        free(list);
        return;
    }
    if (vc >= 0) {                      /* each point a quad, vC in its register's column */
        uint32_t nr = vstride / 16, col = 0, r;
        float *quads;
        for (r = 0; r < (uint32_t)vc; r++)
            col += (inputs >> r) & 1;
        quads = sprite_vertices(verts, nv, nr, col, &list);
        free(verts);
        if (!quads)
            return;
        verts = quads;
        vstride += 16;
        prim = 4;                       /* TRIANGLELIST */
        count = 2 * nv;
        k = 6 * nv;
        nv *= 4;
        InterlockedIncrement(&g_sprite_draws);
        event(&g_ev_sprite, 8, "point sprites: program %08X, %u points as quads (vC = v%d, sprite %u), %s", p.hash,
              nv / 4, vc, rs_x(XRS_POINTSPRITEENABLE), path);
    }
    if (g_vsprobe) {                    /* before anything can skip the draw: the program is what's checked */
        d3dh_draw_t view;
        memset(&view, 0, sizeof(view));
        draw_viewport(&view);
        vs_probe_once(&p, &hv, verts, vstride, &view, idx, lo, n);
    }
    if (texel < 0 && copy_dst_bound() && !g_vp_copy) {
        skip(SKIP_VP_COPY, p.hash);
        if (drawlog_on())
            drawlog(path, xdk_prim, n, MEM32(dev() + DEV_VS_HANDLE), NULL, 0, 0,
                    "skipped: vertex program sampling a CopyRects copy");
        free(verts);
        free(list);
        return;
    }
    memset(tex, 0, sizeof(tex));
    memset(&dr, 0, sizeof(dr));
    if (setup_draw(&dr, 0, tex, rs, tss, &ps, NULL) < 0) {
        free(verts);
        free(list);
        return;
    }
    dr.prim = prim;
    dr.prim_count = count;
    dr.stride = vstride;
    dr.verts = verts;
    dr.vert_count = nv;
    dr.indices = list;
    dr.index_count = k;
    dr.vprog = &hv;
    if (vc >= 0) {                      /* a sprite faces the viewer: never culled */
        for (i = 0; i < dr.n_rs; i++)
            if (rs[i].state == PRS_CULLMODE)
                rs[i].value = 1;
        if (rs_x(XRS_POINTSPRITEENABLE) && !dr.ps)
            sprite_stages(&dr, tss);
    }
    event(&g_ev_vp, 12, "vertex program %08X (%u instructions, reads %04X): %s prim %u, %u vertices from %u, %u regs,"
          " target %08X, pixel shader %s, tex0 %08X", p.hash, p.n_insns, inputs, path, xdk_prim, nv, lo, vstride / 16,
          dr.target.data, dr.ps ? "set" : "none", tex[0].key);
    if (drawlog_on()) {
        uint32_t r, j, c, col = 0;
        drawlog(path, xdk_prim, n, MEM32(dev() + DEV_VS_HANDLE), NULL, 0, 0, "mirrored: vertex program");
        fprintf(stderr, "[D3DB] drawlog   inputs over %u vertices:", nv);
        for (r = 0; r < 16; r++) {
            float lo4[4] = { 1e30f, 1e30f, 1e30f, 1e30f }, hi4[4] = { -1e30f, -1e30f, -1e30f, -1e30f };
            if (!(inputs & (1u << r)))
                continue;
            for (j = 0; j < nv; j++)
                for (c = 0; c < 4; c++) {
                    float x = verts[(size_t)j * (vstride / 4) + 4 * col + c];
                    lo4[c] = x < lo4[c] ? x : lo4[c];
                    hi4[c] = x > hi4[c] ? x : hi4[c];
                }
            fprintf(stderr, " v%u (%.4g..%.4g, %.4g..%.4g, %.4g..%.4g, %.4g..%.4g)", r, (double)lo4[0], (double)hi4[0],
                    (double)lo4[1], (double)hi4[1], (double)lo4[2], (double)hi4[2], (double)lo4[3], (double)hi4[3]);
            col++;
        }
        fputc('\n', stderr);
    }
    if (g_motion_live) {
        uint32_t ch = fnv32(consts, 58 * 16, 2166136261u);
        ch = fnv32(consts + 60 * 4, (VS_CONSTS - 60) * 16, ch);
        fprintf(g_motion, "V %u %08X %u %u %08X %08X %.5g %.5g %.5g %.5g", g_motion_n++, p.hash, lo, nv, ch,
                fnv32(verts, (size_t)nv * vstride, 2166136261u), (double)verts[0], (double)verts[1],
                (double)verts[2], (double)verts[3]);
        for (i = 96 * 4; i < 100 * 4; i++)          /* the transform (the XDK's c0-c3) */
            fprintf(g_motion, " %.6g", (double)consts[i]);
        for (i = 124 * 4; i < 127 * 4; i++)         /* the first bone (c28-c30) */
            fprintf(g_motion, " %.6g", (double)consts[i]);
        fprintf(g_motion, " @%08X %08X<%08X<%08X<%08X", g_motion_draw_ret, g_motion_vsc_ret[0],
                g_motion_vsc_ret[1], g_motion_vsc_ret[2], g_motion_vsc_ret[3]);  /* drawn by, c0 set by */
        fputc('\n', g_motion);
    }
    if (d3dh_draw_up(&dr) == 0) {
        count_mirrored(&dr);
        InterlockedIncrement(&g_mirrored_vp);
    }
    free(verts);
    free(list);
}

/* DrawVerticesUP(PrimitiveType, VertexCount, pVertexStreamZeroData,
 * VertexStreamZeroStride), after the lifted call: the same draw through
 * xbox_d3d8, with the state read from D3D's shadows at this point. S4 adds
 * render targets and pixel shaders; step 34 the fixed-function state for
 * untransformed vertices. */
static void mirror_draw_up_path(const d3db_args_t *a, const char *path)
{
    uint32_t d = dev(), vs, count = 0, prim;
    d3dh_state_t rs[64], tss[4][16];
    d3dh_tex_t tex[4];
    d3dh_ps_t ps;
    d3dh_ff_t ff;
    d3dh_draw_t dr;
    uint8_t *lit = NULL;
    int z_off, cpu;

    draw_checks();
    memset(tex, 0, sizeof(tex));
    memset(&dr, 0, sizeof(dr));
    vs = MEM32(d + DEV_VS_HANDLE);
    if (vs & 1) {                       /* S5: a vertex program */
        uint32_t base[16] = { a->arg[2] }, stride[16] = { a->arg[3] };
        mirror_draw_vp(path, a->arg[0], a->arg[1], NULL, base, stride);
        return;
    }
    prim = xl_prim(a->arg[0], a->arg[1], &count);
    if (!prim || !count) {
        skip(SKIP_PRIM, a->arg[0]);
        return;
    }
    cpu = setup_draw(&dr, vs, tex, rs, tss, &ps, &ff);
    if (cpu < 0)
        return;
    z_off = MEM32(d + DEV_DEPTH_SURFACE) == 0;
    dr.prim = prim;
    dr.prim_count = count;
    dr.stride = a->arg[3];
    dr.verts = (const void *)XBOX_PTR(a->arg[2]);
    dr.vert_count = a->arg[1];
    if (cpu) {
        lit = cpu_lit_copy(dr.verts, dr.vert_count, dr.stride, vs, &ff);
        if (lit)
            dr.verts = lit;
    }
    ff_texel_scale(&dr, vs, &lit);

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
    if (drawlog_on() || (g_draw_samples_copy && InterlockedIncrement(&g_ev_copy_draw) <= 10))
        drawlog(path, a->arg[0], a->arg[1], vs, dr.verts, dr.vert_count, dr.stride,
                g_draw_samples_copy ? "mirrored, samples a CopyRects copy" : "mirrored");
    if (d3dh_draw_up(&dr) == 0)
        count_mirrored(&dr);
    free(lit);
}

static void mirror_draw_up(const d3db_args_t *a)
{
    mirror_draw_up_path(a, "UP");
}

/* An XDK primitive over `n` indices as xbox_d3d8 can draw it indexed, with
 * each index minus `lo`: lists and strips as they are, and what it can't draw
 * indexed (fans, quads, quad strips, polygons, line loops) as triangle lists
 * or a line strip. Returns the index count written to `out` (room for 3n + 6),
 * 0 if nothing is drawn; `prim` and `count` are the host's. */
static uint32_t index_list(uint32_t xdk, const uint16_t *in, uint32_t n, uint32_t lo, uint16_t *out,
                           uint32_t *prim, uint32_t *count)
{
    uint32_t i, k = 0;

#define I(j) ((uint16_t)(in[j] - lo))
    switch (xdk) {
    case 1: case 2: case 4: case 5: case 6:     /* POINTLIST, LINELIST, LINESTRIP, TRIANGLELIST, STRIP */
        *prim = xl_prim(xdk, n, count);
        for (i = 0; i < n; i++)
            out[k++] = I(i);
        return *count ? k : 0;
    case 3:                                     /* LINELOOP: a strip back to the start */
        if (n < 2)
            return 0;
        for (i = 0; i < n; i++)
            out[k++] = I(i);
        out[k++] = I(0);
        *prim = 3;
        *count = n;
        return k;
    case 7: case 10:                            /* TRIANGLEFAN, POLYGON */
        for (i = 1; i + 1 < n; i++) {
            out[k++] = I(0);
            out[k++] = I(i);
            out[k++] = I(i + 1);
        }
        break;
    case 8:                                     /* QUADLIST */
        for (i = 0; i + 3 < n; i += 4) {
            out[k++] = I(i);
            out[k++] = I(i + 1);
            out[k++] = I(i + 2);
            out[k++] = I(i);
            out[k++] = I(i + 2);
            out[k++] = I(i + 3);
        }
        break;
    case 9:                                     /* QUADSTRIP: quad j is 2j, 2j+1, 2j+3, 2j+2 */
        for (i = 0; i + 3 < n; i += 2) {
            out[k++] = I(i);
            out[k++] = I(i + 1);
            out[k++] = I(i + 3);
            out[k++] = I(i);
            out[k++] = I(i + 3);
            out[k++] = I(i + 2);
        }
        break;
    default:
        return 0;
    }
#undef I
    *prim = 4;
    *count = k / 3;
    return k;
}

/* DrawIndexedVertices(PrimitiveType, VertexCount, pIndexData) with a vertex
 * format (step 34): 16-bit indices from the caller's memory, vertices from
 * stream 0 (0x10F280: stride +0, offset +4, vertex buffer +8) starting at the
 * base vertex (device +0x1C), as the lifted body's vertex-array setup
 * (0x108F40) computes them. Drawn with xbox_d3d8's DrawIndexedPrimitiveUP
 * over the vertex range the indices use. */
static void mirror_draw_indexed(const d3db_args_t *a)
{
    uint32_t d = dev(), vs = MEM32(d + DEV_VS_HANDLE), n = a->arg[1], stride, vb, lo = 0xFFFF, hi = 0, i;
    uint32_t prim = 0, count = 0, k, va;
    const uint16_t *idx;
    uint16_t *list;
    d3dh_state_t rs[64], tss[4][16];
    d3dh_tex_t tex[4];
    d3dh_ps_t ps;
    d3dh_ff_t ff;
    d3dh_draw_t dr;
    uint8_t *lit = NULL;
    int cpu;

    draw_checks();
    if (vs & 1) {                       /* S5: a vertex program */
        uint32_t base[16], stride[16];
        stream_bases(base, stride);
        mirror_draw_vp("Indexed", a->arg[0], n, a->arg[2] ? (const uint16_t *)XBOX_PTR(a->arg[2]) : NULL, base,
                       stride);
        return;
    }
    stride = MEM32(D3D_STREAMS);
    vb = MEM32(D3D_STREAMS + 8);
    if (!n || !a->arg[2] || !vb || stride < fvf_size(vs)) {
        skip(SKIP_STREAM, stride);
        return;
    }
    idx = (const uint16_t *)XBOX_PTR(a->arg[2]);
    for (i = 0; i < n; i++) {
        if (idx[i] < lo)
            lo = idx[i];
        if (idx[i] > hi)
            hi = idx[i];
    }
    list = (uint16_t *)malloc((3 * (size_t)n + 6) * sizeof(uint16_t));
    if (!list)
        return;
    k = index_list(a->arg[0], idx, n, lo, list, &prim, &count);
    if (!k || !prim || !count) {
        skip(SKIP_PRIM, a->arg[0]);
        free(list);
        return;
    }
    memset(tex, 0, sizeof(tex));
    memset(&dr, 0, sizeof(dr));
    cpu = setup_draw(&dr, vs, tex, rs, tss, &ps, &ff);
    if (cpu < 0) {
        free(list);
        return;
    }
    va = phys_to_va(MEM32(vb + RES_DATA)) + MEM32(D3D_STREAMS + 4) + (MEM32(d + DEV_BASE_VERTEX) + lo) * stride;
    dr.prim = prim;
    dr.prim_count = count;
    dr.stride = stride;
    dr.verts = (const void *)XBOX_PTR(va);
    dr.vert_count = hi - lo + 1;
    dr.indices = list;
    dr.index_count = k;
    if (cpu) {
        lit = cpu_lit_copy(dr.verts, dr.vert_count, stride, vs, &ff);
        if (lit)
            dr.verts = lit;
    }
    ff_texel_scale(&dr, vs, &lit);

    /* The first draws in full, to check the scrape: vertex 0 of the range
     * before and after lighting, and the fixed-function state. */
    if (g_ev_idx < 10) {
        const float *p = (const float *)XBOX_PTR(va);
        uint32_t off_d = 12 + ((vs & FVF_NORMAL) ? 12 : 0);
        event(&g_ev_idx, 10, "DrawIndexedVertices prim %u indices %u (%u..%u) base %u stride %u fvf %X vb %08X"
              " data %08X -> host prim %u count %u, %u vertices; vertex %u: pos (%g, %g, %g) normal (%g, %g, %g)"
              " diffuse %08X%s%08X; LIGHTING %u COLORVERTEX %u sources %u%u%u%u, %u lights, %s", a->arg[0], n,
              lo, hi, MEM32(d + DEV_BASE_VERTEX), stride, vs, vb, MEM32(vb + RES_DATA), prim, count,
              dr.vert_count, lo, (double)p[0], (double)p[1], (double)p[2],
              (double)((vs & FVF_NORMAL) ? p[3] : 0), (double)((vs & FVF_NORMAL) ? p[4] : 0),
              (double)((vs & FVF_NORMAL) ? p[5] : 0), (vs & FVF_DIFFUSE) ? MEM32(va + off_d) : 0,
              lit ? " lit on the CPU to " : " unchanged ", lit ? *(const uint32_t *)(lit + off_d) : 0,
              rs_x(XRS_LIGHTING), rs_x(XRS_COLORVERTEX), rs_x(XRS_SPECULARMATERIALSOURCE),
              rs_x(XRS_DIFFUSEMATERIALSOURCE), rs_x(XRS_AMBIENTMATERIALSOURCE),
              rs_x(XRS_EMISSIVEMATERIALSOURCE), ff.n_lights, dr.tex[0] ? "textured" : "untextured");
        event(&g_ev_idx_ff, 30, "  world row 3 (%g, %g, %g) diag (%g, %g, %g); view row 3 (%g, %g, %g);"
              " projection %g %g %g %g %g; material diffuse (%g, %g, %g, %g) ambient (%g, %g, %g) emissive"
              " (%g, %g, %g); ambient RS %08X", (double)ff.world[12], (double)ff.world[13], (double)ff.world[14],
              (double)ff.world[0], (double)ff.world[5], (double)ff.world[10], (double)ff.view[12],
              (double)ff.view[13], (double)ff.view[14], (double)ff.proj[0], (double)ff.proj[5],
              (double)ff.proj[10], (double)ff.proj[11], (double)ff.proj[14],
              (double)((const float *)ff.material)[0], (double)((const float *)ff.material)[1],
              (double)((const float *)ff.material)[2], (double)((const float *)ff.material)[3],
              (double)((const float *)ff.material)[4], (double)((const float *)ff.material)[5],
              (double)((const float *)ff.material)[6], (double)((const float *)ff.material)[12],
              (double)((const float *)ff.material)[13], (double)((const float *)ff.material)[14],
              rs_x(XRS_AMBIENT));
        for (i = 0; i < ff.n_lights && i < 3; i++) {
            const float *L = (const float *)ff.light[i];
            event(&g_ev_idx_ff, 30, "  light %u: type %u diffuse (%g, %g, %g) ambient (%g, %g, %g) position (%g, %g, %g)"
                  " direction (%g, %g, %g) range %g attenuation %g %g %g", i, ff.light[i][0], (double)L[1],
                  (double)L[2], (double)L[3], (double)L[9], (double)L[10], (double)L[11], (double)L[13],
                  (double)L[14], (double)L[15], (double)L[16], (double)L[17], (double)L[18], (double)L[19],
                  (double)L[21], (double)L[22], (double)L[23]);
        }
    }
    if (drawlog_on() || (g_draw_samples_copy && InterlockedIncrement(&g_ev_copy_draw) <= 10))
        drawlog("Indexed", a->arg[0], n, vs, dr.verts, dr.vert_count, stride,
                g_draw_samples_copy ? "mirrored, samples a CopyRects copy" : "mirrored");
    if (g_motion_live) {
        const float *p0 = (const float *)XBOX_PTR(va);
        fprintf(g_motion, "F %u %X %u %u %08X %.5g %.5g %.5g %08X %.5g %.5g %.5g\n", g_motion_n++, vs, lo,
                dr.vert_count, tex[0].key, (double)ff.world[12], (double)ff.world[13], (double)ff.world[14],
                fnv32(p0, (size_t)dr.vert_count * stride, 2166136261u), (double)p0[0], (double)p0[1],
                (double)p0[2]);
    }
    if (d3dh_draw_up(&dr) == 0)
        count_mirrored(&dr);
    free(lit);
    free(list);
}

/* ── Immediate mode (S4 part 2): Begin, SetVertexData2f/4f, End ── */

/* The XDK's D3DVSDE_* registers are the NV2A's vertex attribute slots:
 * POSITION 0, BLENDWEIGHT 1, NORMAL 2, DIFFUSE 3, SPECULAR 4, FOG 5,
 * POINTSIZE 6, BACKDIFFUSE 7, BACKSPECULAR 8, TEXCOORD0-3 9-12. VERTEX (-1)
 * sets the position and launches a vertex: SetVertexData4f (0x101E60) pushes
 * NV097_SET_VERTEX4F for it and SET_VERTEX_DATA4F_M + 16 * reg otherwise;
 * SetVertexData2f (0x101E20) pushes SET_VERTEX_DATA2F_M + 8 * reg, which the
 * NV2A takes as (a, b, 0, 1). An attribute keeps its value until it is set
 * again, as the NV2A's do. The game's only caller (0x38300, through its
 * wrappers 0x3AE20-0x3AE60) sets DIFFUSE as (R, G, B, A) floats unpacked from
 * an ARGB colour and TEXCOORD0 as (u, v), then launches each vertex.
 *
 * So each SetVertexData call only stores: an attribute into g_imm_cur, or a
 * launch's position plus the attributes the FVF (read at Begin) uses into
 * g_imm.buf. End builds FVF vertices from those and draws them as a UP draw.
 * All immediate draws come from one thread (the census's tid keys). */
#define IMM_REGS        16
#define IMM_REC_MAX     36              /* floats per buffered vertex: position + 8 attributes */
#define IMM_MAX_VERTS   65535           /* index_list takes 16-bit indices */
#define VSDE_NORMAL     2
#define VSDE_DIFFUSE    3
#define VSDE_SPECULAR   4
#define VSDE_POINTSIZE  6
#define VSDE_TEXCOORD0  9

static float g_imm_cur[IMM_REGS][4];
static struct {
    int      open;                      /* between Begin and End */
    uint32_t prim, fvf;
    uint32_t reg[8], nreg;              /* the attributes each vertex keeps, in FVF order */
    uint32_t rec;                       /* floats per buffered vertex: the position, then those */
    uint32_t n, cap, dropped;
    float   *buf;
} g_imm;
static volatile LONG g_mirrored_imm, g_mirrored_imm_rhw, g_ev_imm, g_ev_imm_skip, g_ev_imm_fvf;
static uint32_t g_imm_fvf_seen[16];     /* each FVF's first immediate draw is logged */

static void imm_begin(uint32_t prim)
{
    uint32_t vs = MEM32(dev() + DEV_VS_HANDLE), i, t;

    g_imm.open = 1;
    g_imm.prim = prim;
    g_imm.fvf = vs;
    g_imm.n = g_imm.dropped = 0;
    g_imm.nreg = 0;
    if (!(vs & 1)) {
        if (vs & FVF_NORMAL)
            g_imm.reg[g_imm.nreg++] = VSDE_NORMAL;
        if (vs & FVF_PSIZE)
            g_imm.reg[g_imm.nreg++] = VSDE_POINTSIZE;
        if (vs & FVF_DIFFUSE)
            g_imm.reg[g_imm.nreg++] = VSDE_DIFFUSE;
        if (vs & FVF_SPECULAR)
            g_imm.reg[g_imm.nreg++] = VSDE_SPECULAR;
        t = (vs >> 8) & 15;
        for (i = 0; i < t && i < 4; i++)
            g_imm.reg[g_imm.nreg++] = VSDE_TEXCOORD0 + i;
    }
    g_imm.rec = 4 + 4 * g_imm.nreg;
}

static int imm_grow(void)
{
    uint32_t cap = g_imm.cap ? 2 * g_imm.cap : 1024;
    float *b;

    if (cap > IMM_MAX_VERTS)
        cap = IMM_MAX_VERTS;
    if (cap <= g_imm.cap)
        return 0;
    b = (float *)realloc(g_imm.buf, (size_t)cap * IMM_REC_MAX * sizeof(float));
    if (!b)
        return 0;
    g_imm.buf = b;
    g_imm.cap = cap;
    return 1;
}

/* SetVertexData2f/4f(Register, ...), from D3DB_DATA_CALL before the lifted
 * call: the arguments are still on the guest stack. */
void d3db_vertex_data(int id)
{
    uint32_t reg = MEM32(esp + 4), i;
    const uint32_t *v = (const uint32_t *)XBOX_PTR(esp + 8);
    float *o;

    if (reg < IMM_REGS) {
        uint32_t *c = (uint32_t *)g_imm_cur[reg];
        c[0] = v[0];
        c[1] = v[1];
        c[2] = id == D3DB_DATA4F ? v[2] : 0;
        c[3] = id == D3DB_DATA4F ? v[3] : 0x3F800000u;
        return;
    }
    if (reg != 0xFFFFFFFFu || id != D3DB_DATA4F || !g_imm.open)
        return;
    if (g_imm.n == g_imm.cap && !imm_grow()) {
        g_imm.dropped++;
        return;
    }
    o = g_imm.buf + (size_t)g_imm.n++ * g_imm.rec;
    memcpy(o, v, 16);
    for (i = 0; i < g_imm.nreg; i++)
        memcpy(o + 4 + 4 * i, g_imm_cur[g_imm.reg[i]], 16);
}

static uint32_t rgba_argb(const float *c)
{
    col_t o;
    o.r = c[0];
    o.g = c[1];
    o.b = c[2];
    o.a = c[3];
    return col_argb(o);
}

/* The buffered vertices in the FVF's layout (position, normal, point size,
 * diffuse, specular, texture coordinates), into a buffer the bridge owns. */
static uint8_t *imm_vertices(uint32_t fvf, uint32_t stride)
{
    static uint8_t *out;
    static size_t cap;
    static const uint8_t tc[4] = { 2, 3, 4, 1 };        /* floats per texture-coordinate format */
    size_t need = (size_t)g_imm.n * stride;
    uint32_t i, j, rhw = (fvf & FVF_POSITION_MASK) == FVF_XYZRHW, w_off = 0;

    if (need > cap) {
        uint8_t *b = (uint8_t *)realloc(out, need);
        if (!b)
            return NULL;
        out = b;
        cap = need;
    }
    for (i = 0; i < g_imm.n; i++) {
        const float *s = g_imm.buf + (size_t)i * g_imm.rec;
        uint8_t *o = out + (size_t)i * stride;
        uint32_t off = rhw ? 16 : 12, c;

        memcpy(o, s, off);
        if (!rhw && s[3] != 1.0f)
            w_off++;
        for (j = 0; j < g_imm.nreg; j++) {
            const float *a = s + 4 + 4 * j;
            switch (g_imm.reg[j]) {
            case VSDE_NORMAL:    memcpy(o + off, a, 12); off += 12; break;
            case VSDE_POINTSIZE: memcpy(o + off, a, 4);  off += 4;  break;
            case VSDE_DIFFUSE: case VSDE_SPECULAR:
                c = rgba_argb(a);
                memcpy(o + off, &c, 4);
                off += 4;
                break;
            default:                                    /* TEXCOORD0-3 */
                c = tc[(fvf >> (16 + 2 * (g_imm.reg[j] - VSDE_TEXCOORD0))) & 3];
                memcpy(o + off, a, 4 * c);
                off += 4 * c;
                break;
            }
        }
    }
    if (w_off)
        gap(GAP_IMM_W, "immediate vertices with w != 1", w_off);
    return out;
}

/* RECOMP_D3D_BRIDGE_DRAWLOG=A-B (a check, step 35): every draw on every path
 * between guest swaps A and B, one line each, with where it lands on the
 * screen (untransformed vertices through world * view * projection and the
 * viewport) and its texture-coordinate range. d3d_host.c dumps every one of
 * those swaps, so a thing on screen can be matched with its draws. */

static int drawlog_on(void)
{
    long s = (long)g_swaps + 1;         /* the swap that will show this draw */
    return g_drawlog_to && s >= g_drawlog_from && s <= g_drawlog_to;
}

static void drawlog(const char *path, uint32_t xdk_prim, uint32_t n, uint32_t fvf, const void *verts, uint32_t nv,
                    uint32_t stride, const char *outcome)
{
    const uint8_t *v = (const uint8_t *)verts;
    uint32_t d = dev(), i, t0 = MEM32(d + DEV_TEXTURES), t1 = MEM32(d + DEV_TEXTURES + 4), f0 = t0 ? MEM32(t0 + RES_FORMAT) : 0;
    float box[4] = { 1e30f, 1e30f, -1e30f, -1e30f }, uv[4] = { 1e30f, 1e30f, -1e30f, -1e30f }, m[16], wv[16];
    float vx = (float)MEM32(d + DEV_VIEWPORT), vy = (float)MEM32(d + DEV_VIEWPORT + 4);
    float vw = (float)MEM32(d + DEV_VIEWPORT + 8), vh = (float)MEM32(d + DEV_VIEWPORT + 12);
    uint32_t pos = fvf & FVF_POSITION_MASK, uv_off = 0, behind = 0;
    char w[256] = "";

    if (v && !(fvf & 1) && (pos == FVF_XYZ || pos == FVF_XYZRHW)) {
        if (pos == FVF_XYZ) {
            d3dh_ff_t ff;
            describe_ff(&ff);
            m4_mul(wv, ff.world, ff.view);
            m4_mul(m, wv, ff.proj);
            snprintf(w, sizeof(w), " world diag (%.3g %.3g %.3g) at (%.4g %.4g %.4g); view %08X; proj %.4g %.4g"
                     " %.4g %.4g %.4g %.4g", (double)ff.world[0], (double)ff.world[5], (double)ff.world[10],
                     (double)ff.world[12], (double)ff.world[13], (double)ff.world[14],
                     content_sig((const uint8_t *)ff.view, 64), (double)ff.proj[0], (double)ff.proj[5],
                     (double)ff.proj[10], (double)ff.proj[11], (double)ff.proj[14], (double)ff.proj[15]);
        }
        uv_off = (pos == FVF_XYZRHW ? 16 : 12) + ((fvf & FVF_NORMAL) ? 12 : 0) + ((fvf & FVF_PSIZE) ? 4 : 0) +
                 ((fvf & FVF_DIFFUSE) ? 4 : 0) + ((fvf & FVF_SPECULAR) ? 4 : 0);
        for (i = 0; i < nv; i++) {
            const float *p = (const float *)(v + (size_t)i * stride);
            float x = p[0], y = p[1];
            if (pos == FVF_XYZ) {
                float cx = p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12];
                float cy = p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13];
                float cw = p[0] * m[3] + p[1] * m[7] + p[2] * m[11] + m[15];
                if (cw <= 1e-6f) {
                    behind++;
                    continue;
                }
                x = vx + (cx / cw + 1.0f) * 0.5f * vw;
                y = vy + (1.0f - cy / cw) * 0.5f * vh;
            }
            box[0] = x < box[0] ? x : box[0];
            box[1] = y < box[1] ? y : box[1];
            box[2] = x > box[2] ? x : box[2];
            box[3] = y > box[3] ? y : box[3];
            if ((fvf >> 8) & 15) {
                const float *t = (const float *)(v + (size_t)i * stride + uv_off);
                uv[0] = t[0] < uv[0] ? t[0] : uv[0];
                uv[1] = t[1] < uv[1] ? t[1] : uv[1];
                uv[2] = t[0] > uv[2] ? t[0] : uv[2];
                uv[3] = t[1] > uv[3] ? t[1] : uv[3];
            }
        }
    }
    fprintf(stderr, "[D3DB] drawlog swap %ld #%u %s prim %u n %u fvf %X %s; tex0 %08X (fmt %02X, %ux%u, data %08X)"
            " tex1 %08X; target %s; from %08X; box (%.1f, %.1f)-(%.1f, %.1f)%s uv (%.4g, %.4g)-(%.4g, %.4g)%s\n",
            (long)g_swaps + 1, g_drawlog_n++, path, xdk_prim, n, fvf, outcome, t0, (f0 >> 8) & 0xFF,
            t0 ? 1u << ((f0 >> 20) & 15) : 0, t0 ? 1u << ((f0 >> 24) & 15) : 0, res_data(t0), t1,
            surface_kind(MEM32(d + DEV_RENDER_TARGET)), g_draw_ret, (double)box[0], (double)box[1], (double)box[2],
            (double)box[3], behind ? " (some behind the eye)" : "", (double)uv[0], (double)uv[1], (double)uv[2],
            (double)uv[3], w);
    /* S5: the blending and stage-0/1 ops too (XDK values), and the pixel shader. */
    fprintf(stderr, "[D3DB] drawlog   blend %u %X %X op %X atest %u %X ref %X z %u/%u; stage 0 colour %u %X %X alpha"
            " %u %X %X address %u %u; stage 1 colour %u alpha %u; pixel shader %s (texture modes %08X, dot mapping %X, input texture %X; stages %08X"
            " %08X %08X %08X); fog %u\n", rs_x(XRS_ALPHABLENDENABLE),
            rs_x(XRS_SRCBLEND), rs_x(XRS_DESTBLEND), rs_x(XRS_BLENDOP), rs_x(XRS_ALPHATESTENABLE), rs_x(XRS_ALPHAFUNC),
            rs_x(XRS_ALPHAREF), rs_x(XRS_ZENABLE), rs_x(XRS_ZWRITEENABLE),
            MEM32(D3D_TEXTURESTATE + 4 * XTSS_COLOROP), MEM32(D3D_TEXTURESTATE + 4 * XTSS_COLORARG1),
            MEM32(D3D_TEXTURESTATE + 4 * XTSS_COLORARG2), MEM32(D3D_TEXTURESTATE + 4 * XTSS_ALPHAOP),
            MEM32(D3D_TEXTURESTATE + 4 * XTSS_ALPHAARG1), MEM32(D3D_TEXTURESTATE + 4 * XTSS_ALPHAARG2),
            MEM32(D3D_TEXTURESTATE + 4 * XTSS_ADDRESSU), MEM32(D3D_TEXTURESTATE + 4 * XTSS_ADDRESSV),
            MEM32(D3D_TEXTURESTATE + 4 * (D3D_TSS_COUNT + XTSS_COLOROP)),
            MEM32(D3D_TEXTURESTATE + 4 * (D3D_TSS_COUNT + XTSS_ALPHAOP)), MEM32(d + DEV_PIXEL_SHADER) ? "set" : "none",
            rs_x(XRS_PSTEXTUREMODES), rs_x(XPS_DOTMAPPING), rs_x(XPS_INPUTTEXTURE), MEM32(d + DEV_TEXTURES), MEM32(d + DEV_TEXTURES + 4), MEM32(d + DEV_TEXTURES + 8),
            MEM32(d + DEV_TEXTURES + 12), rs_x(XRS_FOGENABLE));
    /* Step 48: the fog and the final combiner (XDK values; the floats as they are). */
    {
        uint32_t fs = rs_x(XRS_FOGSTART), fe = rs_x(XRS_FOGEND), fd = rs_x(XRS_FOGDENSITY);
        float f_s, f_e, f_d;
        memcpy(&f_s, &fs, 4);
        memcpy(&f_e, &fe, 4);
        memcpy(&f_d, &fd, 4);
        fprintf(stderr, "[D3DB] drawlog   fog table %u start %g end %g density %g colour %08X range %u; specular %u"
                " lighting %u; final %08X %08X count %X; vs %08X; pixel shader def %08X\n", rs_x(XRS_FOGTABLEMODE),
                (double)f_s, (double)f_e, (double)f_d, rs_x(XRS_FOGCOLOR), rs_x(XRS_RANGEFOGENABLE),
                rs_x(XRS_SPECULARENABLE), rs_x(XRS_LIGHTING), rs_x(XPS_FINAL_ABCD), rs_x(XPS_FINAL_EFG),
                rs_x(XPS_COMBINERCOUNT), MEM32(d + DEV_VS_HANDLE), MEM32(d + DEV_PIXEL_SHADER) ? ps_def_hash() : 0);
    }
    /* Step 50: the palettes bound to P8 stages, and their content. */
    {
        unsigned s;
        for (s = 0; s < 4; s++) {
            uint32_t th = MEM32(d + DEV_TEXTURES + 4 * s), pal = MEM32(d + DEV_PALETTES + 4 * s);
            if (th && ((MEM32(th + RES_FORMAT) >> 8) & 0xFF) == XDK_FMT_P8)
                fprintf(stderr, "[D3DB] drawlog   stage %u palette %08X data %08X sig %08X\n", s, pal, res_data(pal),
                        pal ? content_sig((const uint8_t *)XBOX_PTR(phys_to_va(res_data(pal))),
                                          4 * (256u >> ((MEM32(pal + RES_COMMON) >> 30) & 3))) : 0);
        }
    }
    /* Step 49: depth and stencil, and the depth offset (ZBIAS sets the
     * polygon offset states; xbox_d3d8 has no depth bias). */
    {
        uint32_t ss = rs_x(XRS_POLYGONOFFSETZSLOPESCALE), zo = rs_x(XRS_POLYGONOFFSETZOFFSET);
        float f_ss, f_zo;
        memcpy(&f_ss, &ss, 4);
        memcpy(&f_zo, &zo, 4);
        fprintf(stderr, "[D3DB] drawlog   zfunc %X; stencil %u func %X ref %X mask %X write %X ops %X %X %X; zbias %u,"
                " offset solid %u slope %g units %g; cull %X; colour write %X\n", rs_x(XRS_ZFUNC),
                rs_x(XRS_STENCILENABLE), rs_x(XRS_STENCILFUNC), rs_x(XRS_STENCILREF), rs_x(XRS_STENCILMASK),
                rs_x(XRS_STENCILWRITEMASK), rs_x(XRS_STENCILFAIL), rs_x(XRS_STENCILZFAIL), rs_x(XRS_STENCILPASS),
                rs_x(XRS_ZBIAS), rs_x(XRS_SOLIDOFFSETENABLE), (double)f_ss, (double)f_zo, rs_x(XRS_CULLMODE),
                rs_x(XRS_COLORWRITEENABLE));
    }
    if (v && nv <= 16 && pos == FVF_XYZRHW) {   /* step 49: small pre-transformed draws, with their colour */
        uint32_t c_off = 16 + ((fvf & FVF_NORMAL) ? 12 : 0) + ((fvf & FVF_PSIZE) ? 4 : 0);
        fprintf(stderr, "[D3DB] drawlog   pre-transformed");
        for (i = 0; i < nv; i++) {
            const float *p = (const float *)(v + (size_t)i * stride);
            fprintf(stderr, " (%.4g %.4g %.4g %.4g", (double)p[0], (double)p[1], (double)p[2], (double)p[3]);
            if (fvf & FVF_DIFFUSE) {
                uint32_t col;
                memcpy(&col, v + (size_t)i * stride + c_off, 4);
                fprintf(stderr, " %08X", col);
            }
            fputc(')', stderr);
        }
        fputc('\n', stderr);
    }
    if (v && nv <= 16 && pos == FVF_XYZ) {      /* small draws: every vertex, and the view, in full */
        d3dh_ff_t ff;
        describe_ff(&ff);
        fprintf(stderr, "[D3DB] drawlog   view");
        for (i = 0; i < 16; i++)
            fprintf(stderr, " %.4g", (double)ff.view[i]);
        fprintf(stderr, "\n[D3DB] drawlog   positions");
        for (i = 0; i < nv; i++) {
            const float *p = (const float *)(v + (size_t)i * stride);
            fprintf(stderr, " (%.4g %.4g %.4g)", (double)p[0], (double)p[1], (double)p[2]);
        }
        fputc('\n', stderr);
    }
}

/* End, after the lifted call: the buffered vertices as one UP draw.
 * QUADLIST and the other primitives DrawPrimitiveUP takes go as they are;
 * QUADSTRIP, POLYGON and LINELOOP are gathered into a triangle list or a
 * line strip through index_list. Vertex-program immediates wait for S5. */
static void mirror_draw_imm(void)
{
    uint32_t d = dev(), vs = MEM32(d + DEV_VS_HANDLE), n = g_imm.n, stride, prim, count = 0, i;
    d3dh_state_t rs[64], tss[4][16];
    d3dh_tex_t tex[4];
    d3dh_ps_t ps;
    d3dh_ff_t ff;
    d3dh_draw_t dr;
    uint8_t *v, *gathered = NULL, *lit = NULL;
    int cpu;

    draw_checks();
    g_imm.open = 0;
    if (vs & 1) {
        skip(SKIP_VS_PROGRAM, vs);
        if (g_vsdump) {                 /* none from splash to hub (S5) */
            static float c[VS_CONSTS * 4];
            vprog_t p;
            uint32_t base[16] = { 0 }, stride[16] = { 0 };
            if (describe_vprog(&p) == 0) {
                vp_constants(c);
                vs_dump_once("Immediate", &p, c, base, stride, NULL, 0, -1);
            }
        }
        if (g_skiptex && g_check_prefix[0] && skip_tex_once("improg")) {
            float b[4] = { 1e30f, 1e30f, -1e30f, -1e30f };
            for (i = 0; i < n; i++) {
                const float *p = g_imm.buf + (size_t)i * g_imm.rec;
                b[0] = p[0] < b[0] ? p[0] : b[0];
                b[1] = p[1] < b[1] ? p[1] : b[1];
                b[2] = p[0] > b[2] ? p[0] : b[2];
                b[3] = p[1] > b[3] ? p[1] : b[3];
            }
            fprintf(stderr, "[D3DB] skipped immediate draw: prim %u, %u vertices, position box (%g, %g)-(%g, %g),"
                    " vs %X, textures %08X %08X\n", g_imm.prim, n, (double)b[0], (double)b[1], (double)b[2],
                    (double)b[3], vs, MEM32(d + DEV_TEXTURES), MEM32(d + DEV_TEXTURES + 4));
        }
        if (drawlog_on())
            drawlog("Immediate", g_imm.prim, n, vs, NULL, 0, 0, "skipped: vertex program");
        return;
    }
    if (vs != g_imm.fvf || g_imm.dropped) {
        skip(SKIP_IMMEDIATE, g_imm.dropped ? g_imm.dropped : vs);
        event(&g_ev_imm_skip, 10, "immediate draw not mirrored: FVF %X at Begin, %X at End; %u vertices, %u dropped",
              g_imm.fvf, vs, n, g_imm.dropped);
        return;
    }
    if (!n)
        return;                         /* nothing to draw, as on the NV2A */
    stride = fvf_size(vs);
    v = imm_vertices(vs, stride);
    if (!v)
        return;
    prim = xl_prim(g_imm.prim, n, &count);
    if (!prim || !count) {              /* QUADSTRIP, POLYGON, LINELOOP: gathered */
        uint16_t *idx = (uint16_t *)malloc(((size_t)n * 4 + 6) * sizeof(uint16_t)), *list;
        uint32_t k = 0;
        if (idx) {
            list = idx + n;
            for (i = 0; i < n; i++)
                idx[i] = (uint16_t)i;
            k = index_list(g_imm.prim, idx, n, 0, list, &prim, &count);
            gathered = k ? (uint8_t *)malloc((size_t)k * stride) : NULL;
            for (i = 0; gathered && i < k; i++)
                memcpy(gathered + (size_t)i * stride, v + (size_t)list[i] * stride, stride);
            free(idx);
        }
        if (!gathered || !prim || !count) {
            free(gathered);
            skip(SKIP_PRIM, g_imm.prim);
            return;
        }
        v = gathered;
        n = k;
    }
    memset(tex, 0, sizeof(tex));
    memset(&dr, 0, sizeof(dr));
    cpu = setup_draw(&dr, vs, tex, rs, tss, &ps, &ff);
    if (cpu < 0) {
        if (drawlog_on())
            drawlog("Immediate", g_imm.prim, g_imm.n, vs, v, n, stride, "skipped");
        free(gathered);
        return;
    }
    dr.prim = prim;
    dr.prim_count = count;
    dr.stride = stride;
    dr.verts = v;
    dr.vert_count = n;
    if (cpu) {
        lit = cpu_lit_copy(v, n, stride, vs, &ff);
        if (lit)
            dr.verts = lit;
    }
    ff_texel_scale(&dr, vs, &lit);
    if (g_ev_imm < 10) {
        const float *s = g_imm.buf;
        event(&g_ev_imm, 10, "Immediate prim %u, %u vertices, FVF %X (stride %u) -> host prim %u count %u; vertex 0:"
              " (%g, %g, %g, w %g) attributes %g %g %g %g | %g %g %g %g; tex0 %08X (%s)", g_imm.prim, g_imm.n, vs,
              stride, prim, count, (double)s[0], (double)s[1], (double)s[2], (double)s[3],
              (double)(g_imm.nreg > 0 ? s[4] : 0), (double)(g_imm.nreg > 0 ? s[5] : 0),
              (double)(g_imm.nreg > 0 ? s[6] : 0), (double)(g_imm.nreg > 0 ? s[7] : 0),
              (double)(g_imm.nreg > 1 ? s[8] : 0), (double)(g_imm.nreg > 1 ? s[9] : 0),
              (double)(g_imm.nreg > 1 ? s[10] : 0), (double)(g_imm.nreg > 1 ? s[11] : 0), tex[0].key,
              dr.tex[0] ? "described" : "none");
    }
    if (drawlog_on() || (g_draw_samples_copy && InterlockedIncrement(&g_ev_copy_draw) <= 10))
        drawlog("Immediate", g_imm.prim, g_imm.n, vs, (const uint8_t *)dr.verts, n, stride,
                g_draw_samples_copy ? "mirrored, samples a CopyRects copy" : "mirrored");
    for (i = 0; i < 16 && g_imm_fvf_seen[i] && g_imm_fvf_seen[i] != vs; i++)
        ;
    if (i < 16 && !g_imm_fvf_seen[i]) {
        g_imm_fvf_seen[i] = vs;
        event(&g_ev_imm_fvf, 16, "immediate draws with FVF %X (%s), first: prim %u, %u vertices, tex0 %08X", vs,
              (vs & FVF_POSITION_MASK) == FVF_XYZRHW ? "pre-transformed" : "untransformed", g_imm.prim, g_imm.n,
              tex[0].key);
    }
    if (d3dh_draw_up(&dr) == 0) {
        count_mirrored(&dr);
        InterlockedIncrement(&g_mirrored_imm);
        if ((vs & FVF_POSITION_MASK) == FVF_XYZRHW)
            InterlockedIncrement(&g_mirrored_imm_rhw);
    }
    free(lit);
    free(gathered);
}

/* CopyRects(pSourceSurface, pSourceRects, cRects, pDestinationSurface,
 * pDestPoints), after the lifted call. The hub copies the whole back buffer
 * into a 640x480 linear A8R8G8B8 texture (census, step 30); that kind is
 * copied on the host (d3dh_copy_back), any other is counted. The copy's
 * Data is remembered, so the draws that sample it can be logged. */
static void mirror_copy_rects(const d3db_args_t *a)
{
    uint32_t src = a->arg[0], dst = a->arg[3], n = a->arg[2], f, sz, i;
    d3dh_target_t t;
    int rc;

    if (!src || !dst || strcmp(surface_kind(src), "back") != 0 || !MEM32(dst + RES_PARENT) || (n && !a->arg[1])) {
        InterlockedIncrement(&g_copies_skipped);
        event(&g_ev_copy_host, 12, "CopyRects not mirrored: source %08X (%s), destination %08X (%s), %u rects", src,
              surface_kind(src), dst, surface_kind(dst), n);
        return;
    }
    memset(&t, 0, sizeof(t));
    f = MEM32(dst + RES_FORMAT);
    sz = MEM32(dst + RES_SIZE);
    t.data = res_data(dst);
    t.format = (f >> 8) & 0xFF;
    if (sz) {
        t.width = (sz & 0xFFF) + 1;
        t.height = ((sz >> 12) & 0xFFF) + 1;
    } else {
        t.width = 1u << ((f >> 20) & 15);
        t.height = 1u << ((f >> 24) & 15);
    }
    rc = d3dh_copy_back(&t, n ? (const int32_t *)XBOX_PTR(a->arg[1]) : NULL,
                        n && a->arg[4] ? (const int32_t *)XBOX_PTR(a->arg[4]) : NULL, n);
    if (rc == 0)
        InterlockedIncrement(&g_copies);
    for (i = 0; i < COPY_DST_MAX; i++)
        if (!g_copy_dst[i] || g_copy_dst[i] == t.data) {
            g_copy_dst[i] = t.data;
            break;
        }
    event(&g_ev_copy_host, 12, "CopyRects back buffer -> Data %08X (%ux%u format %02X%s), %u rects: host %s", t.data,
          t.width, t.height, t.format, sz ? " linear" : "", n, rc == 0 ? "copied" : "FAILED");
    if (drawlog_on())                   /* step 52: where the copies fall among the draws */
        fprintf(stderr, "[D3DB] drawlog swap %ld copy before #%u: CopyRects back buffer -> Data %08X, %u rects, host %s\n",
                (long)g_swaps + 1, g_drawlog_n, t.data, n, rc == 0 ? "copied" : "FAILED");
}

/* DrawVertices(PrimitiveType, StartVertex, VertexCount) (0x101B20; never
 * seen splash to hub, the levels' snow and particles, step 48): vertices
 * StartVertex on of the bound streams. D3D sets up the vertex arrays with no
 * base vertex for it (sub_00108F40(0), where the indexed draw passes the
 * device's +0x1C). A program draw takes every stream from there; a
 * fixed-function one is stream 0's vertices drawn as a UP draw would be. */
static volatile LONG g_mirrored_vertices, g_ev_vertices;
#define DV_SEEN_MAX 32
static uint32_t g_dv_seen[DV_SEEN_MAX];

static void mirror_draw_vertices(const d3db_args_t *a)
{
    uint32_t vs = MEM32(dev() + DEV_VS_HANDLE), start = a->arg[1], n = a->arg[2], base[16], stride[16], s;
    d3db_args_t up;

    for (s = 0; s < 16; s++) {
        uint32_t vb = MEM32(D3D_STREAMS + 12 * s + 8);
        stride[s] = MEM32(D3D_STREAMS + 12 * s);
        base[s] = vb ? phys_to_va(MEM32(vb + RES_DATA)) + MEM32(D3D_STREAMS + 12 * s + 4) + start * stride[s] : 0;
    }
    InterlockedIncrement(&g_mirrored_vertices);
    for (s = 0; s < DV_SEEN_MAX && g_dv_seen[s] && g_dv_seen[s] != (vs ^ (a->arg[0] << 28)); s++)
        ;
    if (s < DV_SEEN_MAX && !g_dv_seen[s]) {     /* each kind (primitive, vertex shader) once */
        g_dv_seen[s] = vs ^ (a->arg[0] << 28);
        event(&g_ev_vertices, DV_SEEN_MAX, "DrawVertices prim %u vertices %u from %u, vs %08X, stream 0 stride %u"
              " at %08X, stream 1 stride %u, pixel shader %s, point size %08X sprite %u scale %u, guest swap %ld",
              a->arg[0], n, start, vs, stride[0], base[0], stride[1], MEM32(dev() + DEV_PIXEL_SHADER) ? "set" : "none",
              rs_x(XRS_POINTSIZE), rs_x(XRS_POINTSPRITEENABLE), rs_x(XRS_POINTSCALEENABLE), (long)g_swaps);
    }
    if (vs & 1) {                       /* a vertex program */
        draw_checks();
        mirror_draw_vp("Vertices", a->arg[0], n, NULL, base, stride);
        return;
    }
    if (!n || !base[0] || stride[0] < fvf_size(vs)) {
        draw_checks();
        skip(SKIP_VERTICES, stride[0]);
        return;
    }
    up = *a;
    up.arg[0] = a->arg[0];
    up.arg[1] = n;
    up.arg[2] = base[0];
    up.arg[3] = stride[0];
    mirror_draw_up_path(&up, "Vertices");
}

/* Time spent in the mirror (translate + host call, in d3db_leave), in QPC
 * ticks: draws, swaps (dump, copy, present), everything else; and, within
 * draws, immediate ones (End) on their own (step 35). */
static volatile LONG64 g_mirror_ticks[4];

/* Gaps between guest swaps this interval (18.46): the stutter a player
 * sees, counted in the log so a playtest's own log measures it. */
static unsigned g_gap25, g_gap50, g_gap100;
static double g_gap_max;

static void mirror_swap(void)
{
    LONG n = InterlockedIncrement(&g_swaps);
    ULONGLONG now;

    motion_swap(n);
    drawlog_trigger(n);
    d3dh_present(now_s(), (unsigned)n);
    {
        static LARGE_INTEGER f, last;
        LARGE_INTEGER q;
        double ms;

        QueryPerformanceCounter(&q);
        if (!f.QuadPart)
            QueryPerformanceFrequency(&f);
        if (last.QuadPart) {
            ms = (double)(q.QuadPart - last.QuadPart) * 1000.0 / (double)f.QuadPart;
            g_gap25 += ms > 25;
            g_gap50 += ms > 50;
            g_gap100 += ms > 100;
            if (ms > g_gap_max)
                g_gap_max = ms;
        }
        last = q;
    }
    now = GetTickCount64();
    if (now - g_last_host_ms >= HOST_PERIOD_MS) {
        static SRWLOCK line_lock = SRWLOCK_INIT;
        static LONG last_swaps;
        static unsigned last_presents, last_paced, last_late;
        static double last_wait_ms;
        d3dh_stats_t st;
        log_block_t lb;
        if (!TryAcquireSRWLockExclusive(&line_lock))
            return;
        if (now - g_last_host_ms >= HOST_PERIOD_MS) {
            double dt = g_last_host_ms ? (now - g_last_host_ms) / 1000.0 : now_s();
            lb.len = 0;         /* the lines go out as one write (log_block.h) */
            d3dh_stats(&st);
            log_block_printf(&lb, "[D3DB] host +%.1f s: guest swaps %ld (+%ld, %.1f/s), host presents %u (+%u),"
                    " frame counter %u; clears %u (colour %u, z/stencil %u, partial colour %u),"
                    " other-target clears %ld; no game lock %ld (%ld while another thread held it),"
                    " D3DX draws %ld; dumps %u, present fails %u, other status %u\n",
                    now_s(), (long)n, (long)(n - last_swaps), (n - last_swaps) / dt,
                    st.presents, st.presents - last_presents, MEM32(GAME_FRAME_COUNTER),
                    st.clears, st.clears_colour, st.clears_depth, st.clears_partial_colour,
                    (long)g_clears_other_rt, (long)g_nolock, (long)g_interleaved, (long)g_d3dx_draws,
                    st.dumps, st.present_fails, st.present_status);
            if (st.paced)       /* S7: the frame limiter */
                log_block_printf(&lb, "[D3DB] host limiter: paced %u (+%u), late %u (+%u), waited %.0f ms this interval"
                        " (%.2f ms a frame)\n", st.paced, st.paced - last_paced, st.paced_late,
                        st.paced_late - last_late, st.paced_wait_ms - last_wait_ms,
                        st.paced > last_paced ? (st.paced_wait_ms - last_wait_ms) / (st.paced - last_paced) : 0.0);
            log_block_printf(&lb, "[D3DB] host swap gaps this interval: over 25 ms %u, over 50 ms %u, over 100 ms %u,"
                    " longest %.1f ms\n", g_gap25, g_gap50, g_gap100, g_gap_max);
            g_gap25 = g_gap50 = g_gap100 = 0;
            g_gap_max = 0;
            last_paced = st.paced;
            last_late = st.paced_late;
            last_wait_ms = st.paced_wait_ms;
            {   /* 18.45: DWM compositions between presents (d3d_host.c) */
                static unsigned last_shown[9];
                unsigned k, d[9], total = 0;
                for (k = 0; k < 9; k++) {
                    d[k] = st.shown[k] - last_shown[k];
                    last_shown[k] = st.shown[k];
                    total += d[k];
                }
                if (total)
                    log_block_printf(&lb, "[D3DB] host presents this interval, by DWM compositions since the last: 0 %u, 1 %u, 2 %u, 3 %u,"
                            " 4 %u, 5 %u, 6 %u, 7 %u, more %u\n", d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8]);
            }
            {   /* 18.48: refreshes between consecutive frames on screen (d3d_host.c) */
                static unsigned last_disp[9], last_unmeasured;
                unsigned k, d[9], total = 0;
                for (k = 0; k < 9; k++) {
                    d[k] = st.displayed[k] - last_disp[k];
                    last_disp[k] = st.displayed[k];
                    total += d[k];
                }
                if (total || st.display_unmeasured != last_unmeasured)
                    log_block_printf(&lb, "[D3DB] host frames shown this interval, by refreshes since the last shown: 1 %u, 2 %u,"
                            " 3 %u, 4 %u, 5 %u, 6 %u, 7 %u, more %u; unmeasured %u\n", d[1], d[2], d[3], d[4], d[5],
                            d[6], d[7], d[8], st.display_unmeasured - last_unmeasured);
                last_unmeasured = st.display_unmeasured;
            }
            log_block_printf(&lb, "[D3DB] host draws: mirrored %ld (to render targets %ld, with pixel shaders %ld,"
                    " immediate %ld, of them pre-transformed %ld; DrawVertices calls %ld),"
                    " host draws %u (fails %u); not mirrored: target %ld, vertex program %ld, primitive %ld,"
                    " texture %ld, stream %ld, vertex format %ld, immediate %ld, DrawVertices %ld; textures uploaded"
                    " %u (P8 %u, cube %u, fails %u, evictions %u; cube binds %ld; unbound stages as black %ld); translation gaps: value %ld, texop"
                    " %ld, diffuse arg %ld,"
                    " frontface %ld\n",
                    (long)g_mirrored_draws, (long)g_mirrored_rt_draws, (long)g_mirrored_ps_draws,
                    (long)g_mirrored_imm, (long)g_mirrored_imm_rhw, (long)g_mirrored_vertices, st.draws,
                    st.draw_fails, (long)g_skip[SKIP_TARGET], (long)g_skip[SKIP_VS_PROGRAM],
                    (long)g_skip[SKIP_PRIM], (long)g_skip[SKIP_TEXTURE], (long)g_skip[SKIP_STREAM],
                    (long)g_skip[SKIP_FVF], (long)g_skip[SKIP_IMMEDIATE], (long)g_skip[SKIP_VERTICES],
                    st.tex_uploads, st.p8_uploads, st.cube_uploads, st.tex_upload_fails, st.tex_evictions,
                    (long)g_cube_binds, (long)g_null_stages, (long)g_gap[GAP_VALUE],
                    (long)g_gap[GAP_TEXOP], (long)g_gap[GAP_DIFFUSE_ARG], (long)g_gap[GAP_FRONTFACE]);
            log_block_printf(&lb, "[D3DB] host fixed function: indexed draws %u, untransformed draws %u (lit by xbox_d3d8"
                    " %ld, lit on the CPU %ld); gaps: over 8 lights %ld, texture transform %ld, CPU lighting"
                    " without a diffuse %ld, specular %ld, texcoord format %ld, immediate w not 1 %ld; linear textures"
                    " scaled to 0..1 %ld (not scaled %ld)\n",
                    st.indexed_draws, st.ff_draws, (long)g_host_lit_draws, (long)g_cpu_lit_draws,
                    (long)g_gap[GAP_LIGHTS], (long)g_gap[GAP_TEXTRANSFORM], (long)g_gap[GAP_CPU_LIGHT],
                    (long)g_gap[GAP_SPECULAR], (long)g_gap[GAP_TEXCOORD_SIZE], (long)g_gap[GAP_IMM_W],
                    (long)g_ff_texel_draws, (long)g_ff_texel_gaps);
            log_block_printf(&lb, "[D3DB] host vertex programs: mirrored %ld (host draws %u), shaders %u (fails %u), constant"
                    " uploads %u, probes %u; linear textures scaled to 0..1 %ld (not patchable %ld); point sprites as quads %ld"
                    " (not patchable %ld); sampling a copy,"
                    " not mirrored %ld; gaps: undeclared register %ld, stream %ld, array type %ld, tessellator %ld,"
                    " NORESERVEDCONSTANTS %ld\n", (long)g_mirrored_vp,
                    st.vp_draws, st.vs_created, st.vs_fails, st.vs_const_uploads, st.vs_probes,
                    (long)g_texel_draws, (long)g_texel_fails, (long)g_sprite_draws, (long)g_sprite_fails,
                    (long)g_skip[SKIP_VP_COPY], (long)g_vpgap[VPGAP_UNDECLARED], (long)g_vpgap[VPGAP_STREAM],
                    (long)g_vpgap[VPGAP_FORMAT], (long)g_vpgap[VPGAP_TESS], (long)g_vpgap[VPGAP_NORESERVED]);
            log_block_printf(&lb, "[D3DB] host copies: CopyRects back buffer -> texture %ld (host %u, fails %u), not"
                    " mirrored %ld; draws sampling a copy: mirrored %ld, vertex program %ld\n", (long)g_copies,
                    st.copies, st.copy_fails, (long)g_copies_skipped, (long)g_copy_sampled, (long)g_copy_sampled_vp);
            log_block_printf(&lb, "[D3DB] host S4: render targets %u (fails %u; released with their memory %u,"
                    " evicted %u), draws to them %u, clears %u, sampled %u"
                    " (while bound as the target %u); pixel-shader draws %u, probes %u; pixel-shader gaps:"
                    " blue %ld, register %ld, output map %ld, blue-to-alpha %ld, mux LSB %ld, texture mode %ld,"
                    " final constants %ld, V1R0 %ld\n",
                    st.rt_created, st.rt_fails, st.rt_released, st.rt_evicted, st.rt_draws, st.rt_clears,
                    st.rt_sampled, st.rt_feedback,
                    st.ps_draws, st.ps_probes, (long)g_psgap[PSGAP_BLUE], (long)g_psgap[PSGAP_REG],
                    (long)g_psgap[PSGAP_OUTMAP], (long)g_psgap[PSGAP_BLUE_TO_ALPHA], (long)g_psgap[PSGAP_MUX_LSB],
                    (long)g_psgap[PSGAP_TEXMODE], (long)g_psgap[PSGAP_FINALCONST], (long)g_psgap[PSGAP_V1R0]);
            {
                static LONG64 last[4];
                LARGE_INTEGER f;
                LONG64 cur[4];
                int i;
                QueryPerformanceFrequency(&f);
                for (i = 0; i < 4; i++)
                    cur[i] = g_mirror_ticks[i];
                log_block_printf(&lb, "[D3DB] host mirror time this interval: draws %.1f ms (immediate %.1f ms), swaps %.1f ms,"
                        " other %.1f ms (%.1f%% of the interval)\n",
                        1000.0 * (cur[0] - last[0]) / f.QuadPart, 1000.0 * (cur[3] - last[3]) / f.QuadPart,
                        1000.0 * (cur[1] - last[1]) / f.QuadPart, 1000.0 * (cur[2] - last[2]) / f.QuadPart,
                        100.0 * ((cur[0] - last[0]) + (cur[1] - last[1]) + (cur[2] - last[2])) / f.QuadPart / dt);
                for (i = 0; i < 4; i++)
                    last[i] = cur[i];
            }
            if (d3db_wide > 0 || g_render_w)    /* 18.56 */
                log_block_printf(&lb, "[D3DB] host widescreen: projections widened %ld, kept %ld; fixed-function"
                                 " draws with a 4:3 projection pillarboxed %ld; pre-transformed draws stretched %u,"
                                 " pillarboxed %u; screen copies cropped to 4:3 %ld; copies not scaled %u\n",
                                 (long)g_wide_cameras, (long)g_wide_kept, (long)g_wide_ff_pillar, st.rhw_full,
                                 st.rhw_pillar, (long)g_wide_crops, st.copy_unscaled);
            if (st.aa_mode || st.ms_targets || st.post_aa)      /* step 58 */
                log_block_printf(&lb, "[D3DB] host anti-aliasing: %s; back buffer samples %u, multisampled targets %u"
                                 " (fails %u), resolves %u, fills %u; frames through FXAA/SMAA %u (fails %u)\n",
                                 twoc_aa_name((int)st.aa_mode), st.ms_samples ? st.ms_samples : 1, st.ms_targets,
                                 st.ms_fails, st.ms_resolves, st.ms_fills, st.post_aa, st.post_aa_fails);
            {
                char buf[512];
                int i, len = 0;
                for (i = 0; i < LOCK_THREADS && g_lock_by_thread[i].tid && len < (int)sizeof(buf) - 40; i++)
                    len += snprintf(buf + len, sizeof(buf) - len, " %ld:%ld/%ld",
                                    (long)g_lock_by_thread[i].tid, (long)g_lock_by_thread[i].locked,
                                    (long)g_lock_by_thread[i].calls);
                log_block_printf(&lb, "[D3DB] host threads (tid:with game lock/calls):%s\n", len ? buf : " none");
            }
            log_block_flush(&lb);
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

    /* The motion log's call chain for a write to c0: the return address, then the next three words
     * on the guest stack that follow a call instruction in the game's code (a heuristic walk). */
    if (g_motion_live && id == D3DB_VS_CONSTANT && (int)a->arg[0] <= 0 && (int)(a->arg[0] + a->arg[2]) > 0) {
        uint32_t off, n = 1, v;
        memset(g_motion_vsc_ret, 0, sizeof(g_motion_vsc_ret));
        g_motion_vsc_ret[0] = a->ret;
        for (off = 4; off < 0x400 && n < 4; off += 4) {
            v = MEM32(esp + off);
            if (v > 0x11005u && v < 0x135AA0u &&
                (MEM8(v - 5) == 0xE8 || MEM8(v - 6) == 0xFF || MEM8(v - 2) == 0xFF || MEM8(v - 3) == 0xFF))
                g_motion_vsc_ret[n++] = v;
        }
    }

    switch (id) {
    case D3DB_CLEAR: case D3DB_SWAP: case D3DB_DRAW_UP: case D3DB_DRAW:
    case D3DB_DRAW_INDEXED: case D3DB_END: case D3DB_COPYRECTS:
        check_lock(id, a);
        break;
    case D3DB_TEX_LOCK: case D3DB_SURF_LOCK:
        if (d3db_host_on) {                /* the game is about to write it: re-upload at the next draw */
            d3dh_texture_touch(res_data(a->arg[0]));
            sig_forget(res_data(a->arg[0]));
        }
        break;
    case D3DB_BEGIN:
        check_lock(id, a);
        if (d3db_host_on)
            imm_begin(a->arg[0]);
        break;
    }
    if (!d3db_census_on)
        return;

    cen_addf(1, "call %s", d3db_name[id]);
    cen_addf(1, "tid %s %lu", d3db_name[id], a->tid);

    switch (id) {
    case D3DB_PROBE_SPLASH:
        d3db_mark("splash routine sub_000875A0 entered");
        break;
    case D3DB_DRAW_UP:              /* (Prim, VertexCount, pData, Stride) */
        scrape("UP", a->arg[0], a->arg[1]);
        scrape_ff("UP");
        cen_addf(1, "stride UP %u", a->arg[3]);
        break;
    case D3DB_DRAW:                 /* (Prim, StartVertex, VertexCount) */
        scrape("Vertices", a->arg[0], a->arg[2]);
        scrape_streams("Vertices");
        break;
    case D3DB_DRAW_INDEXED:         /* (Prim, IndexCount, pIndexData) */
        scrape("Indexed", a->arg[0], a->arg[1]);
        scrape_streams("Indexed");
        scrape_ff("Indexed");
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
    if (d3db_host_on && id != D3DB_DATA2F && id != D3DB_DATA4F) {  /* those: d3db_vertex_data */
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        switch (id) {
        case D3DB_CREATE_DEVICE: mirror_create_device(a); break;
        case D3DB_CLEAR:         mirror_clear(a);         break;
        case D3DB_SWAP:          mirror_swap();           break;
        case D3DB_DRAW_UP:       g_draw_ret = a->ret; mirror_draw_up(a); break;
        case D3DB_DRAW_INDEXED:  g_motion_draw_ret = g_draw_ret = a->ret; mirror_draw_indexed(a);  break;
        case D3DB_END:           g_draw_ret = a->ret; mirror_draw_imm(); break;
        case D3DB_DRAW:          g_draw_ret = a->ret; mirror_draw_vertices(a); break;
        case D3DB_COPYRECTS:     mirror_copy_rects(a);    break;
        }
        QueryPerformanceCounter(&t1);
        InterlockedAdd64(&g_mirror_ticks[id == D3DB_DRAW_UP || id == D3DB_DRAW_INDEXED || id == D3DB_END ||
                                         id == D3DB_DRAW ? 0 : id == D3DB_SWAP ? 1 : 2],
                         t1.QuadPart - t0.QuadPart);
        if (id == D3DB_END)
            InterlockedAdd64(&g_mirror_ticks[3], t1.QuadPart - t0.QuadPart);
    }
    if (!d3db_census_on)
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
    if (!d3db_census_on)
        return;
    fprintf(stderr, "[D3DB] +%.1f s mark: %s\n", now_s(), label);
    cen_dump(1, label);
}

/* Step 52: the guest freed a contiguous block (VA 0x80000000 + its physical
 * address, phys_to_va's inverse): a render target whose Data was in it is
 * gone, and a later texture or target at that Data starts afresh. */
void d3db_memory_freed(uint32_t va, uint32_t bytes)
{
    if (!d3db_host_on || va < 0x80000000u)
        return;
    d3dh_memory_freed(va - 0x80000000u, bytes);
}
