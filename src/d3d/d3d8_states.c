/**
 * D3D8 Render State → D3D11 State Object Translation
 *
 * Converts D3D8 render state values into D3D11 state objects:
 *   - Blend state (alpha blending, color write mask)
 *   - Depth-stencil state (z-test, z-write, stencil)
 *   - Rasterizer state (cull mode, fill mode)
 *   - Sampler state (texture filtering, addressing)
 *
 * State objects are cached and recreated only when dirty.
 */

#include "d3d8_internal.h"
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Cached D3D11 state objects
 * ================================================================ */

static ID3D11BlendState        *g_blend_state = NULL;
static ID3D11DepthStencilState *g_ds_state = NULL;
static ID3D11RasterizerState   *g_raster_state = NULL;
static ID3D11SamplerState      *g_sampler_states[4] = { NULL, NULL, NULL, NULL };   /* bound, from the cache */

/* Sampler objects by description (local perf change, twoc notes 18.63),
 * found by a hash of the description (18.79: a scan of the list for each of
 * 4 stages at every draw), and each stage's last description, whose object
 * it still has bound. */
#define SAMPLER_CACHE_MAX 64
#define SAMPLER_HASH_SLOTS 256                  /* a power of two, over 2x the most entries */
static struct {
    D3D11_SAMPLER_DESC desc;
    ID3D11SamplerState *state;
} g_sampler_cache[SAMPLER_CACHE_MAX];
static unsigned g_sampler_cache_n;
static unsigned char g_sampler_hash[SAMPLER_HASH_SLOTS];   /* entry + 1, 0 empty */
static D3D11_SAMPLER_DESC g_sampler_last[4];
static BOOL g_sampler_last_ok[4];

/* What the draws last bound (d3d8_internal.h). */
D3D8Bound g_d3d8_bound;

/* The context calls' time while the caller measures (d3d8_internal.h). */
int    d3d8_ctx_timing;
LONG64 d3d8_ctx_ticks;
UINT   d3d8_ctx_calls;

void d3d8_bound_reset(void)
{
    /* An address nothing has: every next bind is made. */
    memset(&g_d3d8_bound, 0xFF, sizeof(g_d3d8_bound));
}

/* Last known render state identity for dirty detection */
static DWORD g_last_blend_hash = 0;
static DWORD g_last_raster_hash = 0;
static D3D11_DEPTH_STENCIL_DESC g_last_ds_desc;

/* Blend, depth-stencil and rasterizer objects by description (local perf
 * change, twoc notes 18.90): each made once and kept, where one was released
 * and created again at every change, so a baked draw can hold them. The
 * runtime gives the object it already has for a description it has seen, so
 * the same description binds the same object as before. Full: all released
 * and started again (d3d8_bake_gen bumped). */
#define STATE_CACHE_MAX 64
typedef struct {
    union {
        D3D11_BLEND_DESC blend;
        D3D11_DEPTH_STENCIL_DESC ds;
        D3D11_RASTERIZER_DESC raster;
    } desc;
    IUnknown *state;
} StateCacheEntry;
static StateCacheEntry g_state_cache[3][STATE_CACHE_MAX];
static unsigned g_state_cache_n[3];
UINT d3d8_bake_gen = 1;

static void state_cache_release(void)
{
    unsigned k, i;

    for (k = 0; k < 3; k++) {
        for (i = 0; i < g_state_cache_n[k]; i++)
            IUnknown_Release(g_state_cache[k][i].state);
        g_state_cache_n[k] = 0;
    }
    d3d8_bake_gen++;
}

/* The object kept for description `d` (`size` bytes) of kind `k`, NULL if
 * none yet. */
static IUnknown *state_cache_find(unsigned k, const void *d, size_t size)
{
    unsigned i;

    for (i = 0; i < g_state_cache_n[k]; i++)
        if (memcmp(&g_state_cache[k][i].desc, d, size) == 0)
            return g_state_cache[k][i].state;
    return NULL;
}

/* Keep `state` (one reference, the cache's) for description `d`. */
static void state_cache_add(unsigned k, const void *d, size_t size, IUnknown *state)
{
    unsigned i;

    if (g_state_cache_n[k] == STATE_CACHE_MAX) {
        for (i = 0; i < g_state_cache_n[k]; i++)
            IUnknown_Release(g_state_cache[k][i].state);
        g_state_cache_n[k] = 0;
        d3d8_bake_gen++;
    }
    i = g_state_cache_n[k]++;
    memset(&g_state_cache[k][i].desc, 0, sizeof(g_state_cache[k][i].desc));
    memcpy(&g_state_cache[k][i].desc, d, size);
    g_state_cache[k][i].state = state;
}

/* ================================================================
 * D3D8 → D3D11 enum translation
 * ================================================================ */

static D3D11_BLEND d3d8_to_d3d11_blend(DWORD d3d8blend)
{
    switch (d3d8blend) {
    case D3DBLEND_ZERO:         return D3D11_BLEND_ZERO;
    case D3DBLEND_ONE:          return D3D11_BLEND_ONE;
    case D3DBLEND_SRCCOLOR:     return D3D11_BLEND_SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR:  return D3D11_BLEND_INV_SRC_COLOR;
    case D3DBLEND_SRCALPHA:     return D3D11_BLEND_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA:  return D3D11_BLEND_INV_SRC_ALPHA;
    case D3DBLEND_DESTALPHA:    return D3D11_BLEND_DEST_ALPHA;
    case D3DBLEND_INVDESTALPHA: return D3D11_BLEND_INV_DEST_ALPHA;
    case D3DBLEND_DESTCOLOR:    return D3D11_BLEND_DEST_COLOR;
    case D3DBLEND_INVDESTCOLOR: return D3D11_BLEND_INV_DEST_COLOR;
    case D3DBLEND_SRCALPHASAT:  return D3D11_BLEND_SRC_ALPHA_SAT;
    default:                    return D3D11_BLEND_ONE;
    }
}

/* D3D11 forbids color factors in the alpha channel. Use their alpha components. */
static D3D11_BLEND blend_alpha_factor(D3D11_BLEND blend)
{
    switch (blend) {
    case D3D11_BLEND_SRC_COLOR:      return D3D11_BLEND_SRC_ALPHA;
    case D3D11_BLEND_INV_SRC_COLOR:  return D3D11_BLEND_INV_SRC_ALPHA;
    case D3D11_BLEND_DEST_COLOR:     return D3D11_BLEND_DEST_ALPHA;
    case D3D11_BLEND_INV_DEST_COLOR: return D3D11_BLEND_INV_DEST_ALPHA;
    default:                       return blend;
    }
}

static D3D11_COMPARISON_FUNC d3d8_to_d3d11_cmp(DWORD d3d8cmp)
{
    switch (d3d8cmp) {
    case D3DCMP_NEVER:        return D3D11_COMPARISON_NEVER;
    case D3DCMP_LESS:         return D3D11_COMPARISON_LESS;
    case D3DCMP_EQUAL:        return D3D11_COMPARISON_EQUAL;
    case D3DCMP_LESSEQUAL:    return D3D11_COMPARISON_LESS_EQUAL;
    case D3DCMP_GREATER:      return D3D11_COMPARISON_GREATER;
    case D3DCMP_NOTEQUAL:     return D3D11_COMPARISON_NOT_EQUAL;
    case D3DCMP_GREATEREQUAL: return D3D11_COMPARISON_GREATER_EQUAL;
    case D3DCMP_ALWAYS:       return D3D11_COMPARISON_ALWAYS;
    default:                  return D3D11_COMPARISON_LESS_EQUAL;
    }
}

static D3D11_STENCIL_OP d3d8_to_d3d11_stencilop(DWORD op)
{
    switch (op) {
    case 1: return D3D11_STENCIL_OP_KEEP;
    case 2: return D3D11_STENCIL_OP_ZERO;
    case 3: return D3D11_STENCIL_OP_REPLACE;
    case 4: return D3D11_STENCIL_OP_INCR_SAT;
    case 5: return D3D11_STENCIL_OP_DECR_SAT;
    case 6: return D3D11_STENCIL_OP_INVERT;
    case 7: return D3D11_STENCIL_OP_INCR;
    case 8: return D3D11_STENCIL_OP_DECR;
    default: return D3D11_STENCIL_OP_KEEP;
    }
}

static D3D11_BLEND_OP d3d8_to_d3d11_blendop(DWORD op)
{
    switch (op) {
    case 1: return D3D11_BLEND_OP_ADD;
    case 2: return D3D11_BLEND_OP_SUBTRACT;
    case 3: return D3D11_BLEND_OP_REV_SUBTRACT;
    case 4: return D3D11_BLEND_OP_MIN;
    case 5: return D3D11_BLEND_OP_MAX;
    default: return D3D11_BLEND_OP_ADD;
    }
}

/* Simple hash of relevant render state values for dirty detection */
static DWORD hash_blend_states(const DWORD *rs)
{
    return rs[D3DRS_ALPHABLENDENABLE] ^
           (rs[D3DRS_SRCBLEND] << 4) ^
           (rs[D3DRS_DESTBLEND] << 8) ^
           (rs[D3DRS_BLENDOP] << 12) ^
           (rs[D3DRS_COLORWRITEENABLE] << 16);
}

static DWORD hash_raster_states(const DWORD *rs)
{
    return rs[D3DRS_CULLMODE] ^
           (rs[D3DRS_FILLMODE] << 4);
}

/* ================================================================
 * State object creation
 * ================================================================ */

static void update_blend_state(const DWORD *rs)
{
    DWORD hash = hash_blend_states(rs);
    D3D11_BLEND_DESC bd;
    HRESULT hr;

    if (hash == g_last_blend_hash && g_blend_state) return;
    g_last_blend_hash = hash;
    g_blend_state = NULL;               /* the cache's (18.90) */

    memset(&bd, 0, sizeof(bd));
    bd.RenderTarget[0].BlendEnable = rs[D3DRS_ALPHABLENDENABLE] ? TRUE : FALSE;
    bd.RenderTarget[0].SrcBlend = d3d8_to_d3d11_blend(rs[D3DRS_SRCBLEND]);
    bd.RenderTarget[0].DestBlend = d3d8_to_d3d11_blend(rs[D3DRS_DESTBLEND]);
    bd.RenderTarget[0].BlendOp = d3d8_to_d3d11_blendop(rs[D3DRS_BLENDOP] ? rs[D3DRS_BLENDOP] : 1);
    bd.RenderTarget[0].SrcBlendAlpha = blend_alpha_factor(bd.RenderTarget[0].SrcBlend);
    bd.RenderTarget[0].DestBlendAlpha = blend_alpha_factor(bd.RenderTarget[0].DestBlend);
    bd.RenderTarget[0].BlendOpAlpha = bd.RenderTarget[0].BlendOp;
    bd.RenderTarget[0].RenderTargetWriteMask = (UINT8)(rs[D3DRS_COLORWRITEENABLE] & 0x0F);

    if ((g_blend_state = (ID3D11BlendState *)state_cache_find(0, &bd, sizeof(bd))) != NULL)
        return;
    hr = ID3D11Device_CreateBlendState(d3d8_GetD3D11Device(), &bd, &g_blend_state);
    if (FAILED(hr))
        fprintf(stderr, "D3D8: CreateBlendState failed: 0x%08lX\n", hr);
    else
        state_cache_add(0, &bd, sizeof(bd), (IUnknown *)g_blend_state);
}

static void update_depth_stencil_state(const DWORD *rs)
{
    D3D11_DEPTH_STENCIL_DESC dsd;
    HRESULT hr;

    memset(&dsd, 0, sizeof(dsd));
    dsd.DepthEnable = rs[D3DRS_ZENABLE] ? TRUE : FALSE;
    dsd.DepthWriteMask = rs[D3DRS_ZWRITEENABLE] ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    dsd.DepthFunc = d3d8_to_d3d11_cmp(rs[D3DRS_ZFUNC]);

    dsd.StencilEnable = rs[D3DRS_STENCILENABLE] ? TRUE : FALSE;
    dsd.StencilReadMask = (UINT8)(rs[D3DRS_STENCILMASK] & 0xFF);
    dsd.StencilWriteMask = (UINT8)(rs[D3DRS_STENCILWRITEMASK] & 0xFF);

    dsd.FrontFace.StencilFunc = d3d8_to_d3d11_cmp(rs[D3DRS_STENCILFUNC]);
    dsd.FrontFace.StencilFailOp = d3d8_to_d3d11_stencilop(rs[D3DRS_STENCILFAIL]);
    dsd.FrontFace.StencilDepthFailOp = d3d8_to_d3d11_stencilop(rs[D3DRS_STENCILZFAIL]);
    dsd.FrontFace.StencilPassOp = d3d8_to_d3d11_stencilop(rs[D3DRS_STENCILPASS]);
    dsd.BackFace = dsd.FrontFace;

    /* Zeroed padding makes the complete descriptor comparable; the reference
     * is bound separately. */
    if (g_ds_state && memcmp(&dsd, &g_last_ds_desc, sizeof(dsd)) == 0) return;
    g_ds_state = NULL;                  /* the cache's (18.90) */

    if ((g_ds_state = (ID3D11DepthStencilState *)state_cache_find(1, &dsd, sizeof(dsd))) != NULL) {
        memcpy(&g_last_ds_desc, &dsd, sizeof(dsd));
        return;
    }
    hr = ID3D11Device_CreateDepthStencilState(d3d8_GetD3D11Device(), &dsd, &g_ds_state);
    if (FAILED(hr))
        fprintf(stderr, "D3D8: CreateDepthStencilState failed: 0x%08lX\n", hr);
    else {
        memcpy(&g_last_ds_desc, &dsd, sizeof(dsd));
        state_cache_add(1, &dsd, sizeof(dsd), (IUnknown *)g_ds_state);
    }
}

static void update_rasterizer_state(const DWORD *rs)
{
    DWORD hash = hash_raster_states(rs);
    D3D11_RASTERIZER_DESC rd;
    HRESULT hr;

    if (hash == g_last_raster_hash && g_raster_state) return;
    g_last_raster_hash = hash;
    g_raster_state = NULL;              /* the cache's (18.90) */

    memset(&rd, 0, sizeof(rd));

    switch (rs[D3DRS_FILLMODE]) {
    case D3DFILL_POINT:     rd.FillMode = D3D11_FILL_WIREFRAME; break;  /* D3D11 has no point fill */
    case D3DFILL_WIREFRAME: rd.FillMode = D3D11_FILL_WIREFRAME; break;
    default:                rd.FillMode = D3D11_FILL_SOLID; break;
    }

    switch (rs[D3DRS_CULLMODE]) {
    case D3DCULL_NONE: rd.CullMode = D3D11_CULL_NONE; break;
    case D3DCULL_CW:   rd.CullMode = D3D11_CULL_FRONT; break;  /* D3D8 CW = cull front in D3D11 convention */
    case D3DCULL_CCW:  rd.CullMode = D3D11_CULL_BACK; break;
    default:           rd.CullMode = D3D11_CULL_BACK; break;
    }

    rd.FrontCounterClockwise = FALSE;
    rd.DepthClipEnable = TRUE;
    rd.ScissorEnable = FALSE;
    rd.MultisampleEnable = FALSE;
    rd.AntialiasedLineEnable = FALSE;

    if ((g_raster_state = (ID3D11RasterizerState *)state_cache_find(2, &rd, sizeof(rd))) != NULL)
        return;
    hr = ID3D11Device_CreateRasterizerState(d3d8_GetD3D11Device(), &rd, &g_raster_state);
    if (FAILED(hr))
        fprintf(stderr, "D3D8: CreateRasterizerState failed: 0x%08lX\n", hr);
    else
        state_cache_add(2, &rd, sizeof(rd), (IUnknown *)g_raster_state);
}

/* ================================================================
 * Sampler state
 * ================================================================ */

static D3D11_TEXTURE_ADDRESS_MODE d3d8_to_d3d11_address(DWORD mode)
{
    switch (mode) {
    case D3DTADDRESS_WRAP:       return D3D11_TEXTURE_ADDRESS_WRAP;
    case D3DTADDRESS_MIRROR:     return D3D11_TEXTURE_ADDRESS_MIRROR;
    case D3DTADDRESS_CLAMP:      return D3D11_TEXTURE_ADDRESS_CLAMP;
    case D3DTADDRESS_BORDER:     return D3D11_TEXTURE_ADDRESS_BORDER;
    case D3DTADDRESS_MIRRORONCE: return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
    default:                     return D3D11_TEXTURE_ADDRESS_WRAP;
    }
}

static D3D11_FILTER d3d8_to_d3d11_filter(DWORD mag, DWORD min, DWORD mip)
{
    /* Simplified filter mapping */
    BOOL mag_linear = (mag == D3DTEXF_LINEAR || mag == D3DTEXF_ANISOTROPIC);
    BOOL min_linear = (min == D3DTEXF_LINEAR || min == D3DTEXF_ANISOTROPIC);
    BOOL mip_linear = (mip == D3DTEXF_LINEAR);

    if (mag == D3DTEXF_ANISOTROPIC || min == D3DTEXF_ANISOTROPIC)
        return D3D11_FILTER_ANISOTROPIC;

    if (min_linear && mag_linear && mip_linear)
        return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    if (min_linear && mag_linear)
        return D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    if (min_linear)
        return D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT;
    if (mag_linear)
        return D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;

    return D3D11_FILTER_MIN_MAG_MIP_POINT;
}

/* A description's first slot in g_sampler_hash (FNV-1a over its words). */
static unsigned sampler_slot(const D3D11_SAMPLER_DESC *sd)
{
    const DWORD *w = (const DWORD *)sd;
    unsigned k, h = 2166136261u;

    for (k = 0; k < sizeof(*sd) / sizeof(DWORD); k++)
        h = (h ^ w[k]) * 16777619u;
    return h & (SAMPLER_HASH_SLOTS - 1);
}

void d3d8_states_apply_sampler(DWORD stage)
{
    const DWORD *tss;
    D3D11_SAMPLER_DESC sd;
    HRESULT hr;
    ID3D11DeviceContext *ctx = d3d8_GetD3D11Context();

    if (stage >= 4) return;
    tss = d3d8_GetTSS(stage);
    if (!tss) return;

    memset(&sd, 0, sizeof(sd));
    sd.Filter = d3d8_to_d3d11_filter(
        tss[D3DTSS_MAGFILTER],
        tss[D3DTSS_MINFILTER],
        tss[D3DTSS_MIPFILTER]);
    sd.AddressU = d3d8_to_d3d11_address(tss[D3DTSS_ADDRESSU] ? tss[D3DTSS_ADDRESSU] : D3DTADDRESS_WRAP);
    sd.AddressV = d3d8_to_d3d11_address(tss[D3DTSS_ADDRESSV] ? tss[D3DTSS_ADDRESSV] : D3DTADDRESS_WRAP);
    sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxAnisotropy = tss[D3DTSS_MAXANISOTROPY] ? tss[D3DTSS_MAXANISOTROPY] : 1;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;

    /* The stage's description as at its last draw, and its object still
     * bound: nothing to do (18.79). */
    if (g_sampler_last_ok[stage] && g_d3d8_bound.samp[stage] == g_sampler_states[stage] &&
        memcmp(&g_sampler_last[stage], &sd, sizeof(sd)) == 0)
        return;

    /* A sampler object per distinct description, made once and kept (local
     * perf change, twoc notes 18.63: one was created and released for every
     * stage of every draw). The same description binds an object with the
     * same description, as before. */
    {
        unsigned i, k, h = sampler_slot(&sd);
        for (;;) {
            i = g_sampler_hash[h];
            if (!i || memcmp(&g_sampler_cache[i - 1].desc, &sd, sizeof(sd)) == 0)
                break;
            h = (h + 1) & (SAMPLER_HASH_SLOTS - 1);
        }
        if (i) {
            i--;
        } else {
            ID3D11SamplerState *st = NULL;
            hr = ID3D11Device_CreateSamplerState(d3d8_GetD3D11Device(), &sd, &st);
            if (FAILED(hr))
                return;
            if (g_sampler_cache_n == SAMPLER_CACHE_MAX) {   /* full: start again */
                for (k = 0; k < g_sampler_cache_n; k++)
                    ID3D11SamplerState_Release(g_sampler_cache[k].state);
                g_sampler_cache_n = 0;
                d3d8_bake_gen++;                /* a baked draw may hold one (18.90) */
                memset(g_sampler_hash, 0, sizeof(g_sampler_hash));
                h = sampler_slot(&sd);
            }
            i = g_sampler_cache_n;
            g_sampler_cache[i].desc = sd;
            g_sampler_cache[i].state = st;
            g_sampler_cache_n = i + 1;
            g_sampler_hash[h] = (unsigned char)(i + 1);
        }
        g_sampler_states[stage] = g_sampler_cache[i].state;
        g_sampler_last[stage] = sd;
        g_sampler_last_ok[stage] = TRUE;
        if (g_d3d8_bound.samp[stage] != g_sampler_states[stage]) {
            g_d3d8_bound.samp[stage] = g_sampler_states[stage];
            ID3D11DeviceContext_PSSetSamplers(ctx, stage, 1, &g_sampler_states[stage]);
        }
    }
}

/* ================================================================
 * Apply all states before draw call
 * ================================================================ */

HRESULT d3d8_states_init(void)
{
    /* States are created on first apply */
    d3d8_bound_reset();
    return S_OK;
}

void d3d8_states_shutdown(void)
{
    int i;
    g_blend_state = NULL;               /* owned by the cache (18.90) */
    g_ds_state = NULL;
    g_raster_state = NULL;
    state_cache_release();
    for (i = 0; i < 4; i++)
        g_sampler_states[i] = NULL;         /* owned by the cache */
    for (i = 0; i < (int)g_sampler_cache_n; i++)
        ID3D11SamplerState_Release(g_sampler_cache[i].state);
    g_sampler_cache_n = 0;
    memset(g_sampler_hash, 0, sizeof(g_sampler_hash));
    memset(g_sampler_last_ok, 0, sizeof(g_sampler_last_ok));
    g_last_blend_hash = 0;
    g_last_raster_hash = 0;
    d3d8_bound_reset();
}

void d3d8_states_apply(void)
{
    const DWORD *rs = d3d8_GetRenderStates();
    ID3D11DeviceContext *ctx = d3d8_GetD3D11Context();
    float blend_factor[4] = { 1, 1, 1, 1 };

    if (!rs || !ctx) return;

    update_blend_state(rs);
    update_depth_stencil_state(rs);
    update_rasterizer_state(rs);

    /* Only what changed is bound (18.79). The blend factor and sample mask
     * are always these. */
    if (g_blend_state && g_d3d8_bound.bs != g_blend_state) {
        g_d3d8_bound.bs = g_blend_state;
        ID3D11DeviceContext_OMSetBlendState(ctx, g_blend_state, blend_factor, 0xFFFFFFFF);
    }
    if (g_ds_state && (g_d3d8_bound.dss != g_ds_state || g_d3d8_bound.stencil_ref != rs[D3DRS_STENCILREF])) {
        g_d3d8_bound.dss = g_ds_state;
        g_d3d8_bound.stencil_ref = rs[D3DRS_STENCILREF];
        ID3D11DeviceContext_OMSetDepthStencilState(ctx, g_ds_state, rs[D3DRS_STENCILREF]);
    }
    if (g_raster_state && g_d3d8_bound.rs != g_raster_state) {
        g_d3d8_bound.rs = g_raster_state;
        ID3D11DeviceContext_RSSetState(ctx, g_raster_state);
    }

    /* Apply samplers for all 4 texture stages */
    {
        DWORD s;
        for (s = 0; s < 4; s++)
            d3d8_states_apply_sampler(s);
    }
}
