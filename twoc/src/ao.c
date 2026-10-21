/**
 * Ambient occlusion on the host (step 92, notes 18.95). See ao.h.
 *
 * Both modes work from the back buffer's depth alone (the game keeps no
 * normals), at the back buffer's size or half of it:
 *
 *   1. depth: a pixel shader copies the depth (sample 0 of a multisampled
 *      one; at half size the nearest of each 2 x 2) into an R32_FLOAT
 *      texture at the working size;
 *   2. prefilter: XeGTAO's XeGTAO_PrefilterDepths16x16 turns it into view
 *      depth with 5 MIP levels (the game's projection gives the near and far
 *      planes);
 *   3. occlusion, one of
 *      - GTAO: XeGTAO_MainPass, 3 slices of 3 steps a side ("High"), the
 *        normals from the depth in place (XE_GTAO_GENERATE_NORMALS_INPLACE);
 *      - SSAO: 16 points in the hemisphere over the normal (the same
 *        normals from depth), each one occluded when the depth buffer is in
 *        front of it, weighted down when the occluder is far from the point
 *        (Crytek's range check), the hemisphere turned per pixel by
 *        XeGTAO's Hilbert-curve R2 noise, far points read from the
 *        depth's smaller MIP levels as XeGTAO reads them;
 *      both writing XeGTAO's working term (visibility) and its edges;
 *   4. denoise: XeGTAO_Denoise, two passes by default (its "medium" level:
 *      with no TAA here one pass leaves the noise visible as grain);
 *   5. apply: a full-screen pass over the back buffer that multiplies each
 *      pixel by the visibility, faded by the strength and by the fog the
 *      world's draws had (by its table mode from the view depth, so far
 *      things in fog keep their colour), nothing where the depth is the cleared 1 (the sky's
 *      own draws write it, but they are at the far plane), and at half size
 *      upsampled with weights by depth so edges keep their own value.
 *
 * XeGTAO (MIT, Copyright (C) 2016-2021 Intel Corporation; Filip Strugar,
 * Steve Mccalla) is third_party/xegtao, embedded unchanged; the entry points
 * below follow its vaGTAO.hlsl, and ao_constants() is XeGTAO.h's
 * GTAOUpdateConstants in C. No TAA: the noise index stays 0, so the noise
 * is the same every frame and every sub-frame.
 *
 * GPU time (RECOMP_AO_GPU=1): timestamps around each pass, read back a few
 * frames later without waiting.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "d3d8_internal.h"      /* xbox_d3d8's D3D11 device and context */
#include <d3dcompiler.h>

#include "settings.h"
#include "ao.h"
#include "xegtao_hlsl.h"        /* XeGTAO.h and XeGTAO.hlsli as g_xegtao_h, g_xegtao_hlsli (CMakeLists.txt) */

#define RELEASE(T, p) do { if (p) { T##_Release(p); (p) = NULL; } } while (0)

/* ── Shaders ───────────────────────────────────────────────── */

/* The compute passes, around XeGTAO.hlsli (after its vaGTAO.hlsl). */
static const char g_cs_hlsl[] =
    "#define VA_SATURATE saturate\n"
    "#define XE_GTAO_FP32_DEPTHS 1\n"
    "#define XE_GTAO_USE_HALF_FLOAT_PRECISION 0\n"
    "#define XE_GTAO_GENERATE_NORMALS_INPLACE 1\n"
    "#include \"XeGTAO.h\"\n"
    "cbuffer GTAOConstantBuffer : register(b0) { GTAOConstants g_GTAOConsts; };\n"
    "#include \"XeGTAO.hlsli\"\n"
    "SamplerState g_samplerPointClamp : register(s0);\n"
    "Texture2D<float> g_srcRawDepth : register(t0);\n"
    "RWTexture2D<lpfloat> g_outWorkingDepthMIP0 : register(u0);\n"
    "RWTexture2D<lpfloat> g_outWorkingDepthMIP1 : register(u1);\n"
    "RWTexture2D<lpfloat> g_outWorkingDepthMIP2 : register(u2);\n"
    "RWTexture2D<lpfloat> g_outWorkingDepthMIP3 : register(u3);\n"
    "RWTexture2D<lpfloat> g_outWorkingDepthMIP4 : register(u4);\n"
    "Texture2D<lpfloat> g_srcWorkingDepth : register(t0);\n"
    "RWTexture2D<uint> g_outWorkingAOTerm : register(u0);\n"
    "RWTexture2D<unorm float> g_outWorkingEdges : register(u1);\n"
    "Texture2D<uint> g_srcWorkingAOTerm : register(t0);\n"
    "Texture2D<lpfloat> g_srcWorkingEdges : register(t1);\n"
    "RWTexture2D<uint> g_outFinalAOTerm : register(u0);\n"
    "\n"
    "lpfloat2 SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex)\n"
    "{\n"
    "    uint index = HilbertIndex(pixCoord.x, pixCoord.y);\n"
    "    index += 288 * (temporalIndex % 64);\n"
    "    return lpfloat2(frac(0.5 + index * float2(0.75487766624669276005, 0.5698402909980532659114)));\n"
    "}\n"
    "\n"
    "[numthreads(8, 8, 1)]\n"
    "void CSPrefilterDepths16x16(uint2 dispatchThreadID : SV_DispatchThreadID, uint2 groupThreadID : SV_GroupThreadID)\n"
    "{\n"
    "    XeGTAO_PrefilterDepths16x16(dispatchThreadID, groupThreadID, g_GTAOConsts, g_srcRawDepth, g_samplerPointClamp,\n"
    "        g_outWorkingDepthMIP0, g_outWorkingDepthMIP1, g_outWorkingDepthMIP2, g_outWorkingDepthMIP3,\n"
    "        g_outWorkingDepthMIP4);\n"
    "}\n"
    "\n"
    "[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]\n"
    "void CSGTAOHigh(const uint2 pixCoord : SV_DispatchThreadID)\n"
    "{\n"
    "    XeGTAO_MainPass(pixCoord, 3, 3, SpatioTemporalNoise(pixCoord, g_GTAOConsts.NoiseIndex), lpfloat3(0, 0, -1),\n"
    "        g_GTAOConsts, g_srcWorkingDepth, g_samplerPointClamp, g_outWorkingAOTerm, g_outWorkingEdges);\n"
    "}\n"
    "\n"
    "[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]\n"
    "void CSDenoisePass(const uint2 dispatchThreadID : SV_DispatchThreadID)\n"
    "{\n"
    "    const uint2 pixCoordBase = dispatchThreadID * uint2(2, 1);\n"
    "    XeGTAO_Denoise(pixCoordBase, g_GTAOConsts, g_srcWorkingAOTerm, g_srcWorkingEdges, g_samplerPointClamp,\n"
    "        g_outFinalAOTerm, false);\n"
    "}\n"
    "\n"
    "[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]\n"
    "void CSDenoiseLastPass(const uint2 dispatchThreadID : SV_DispatchThreadID)\n"
    "{\n"
    "    const uint2 pixCoordBase = dispatchThreadID * uint2(2, 1);\n"
    "    XeGTAO_Denoise(pixCoordBase, g_GTAOConsts, g_srcWorkingAOTerm, g_srcWorkingEdges, g_samplerPointClamp,\n"
    "        g_outFinalAOTerm, true);\n"
    "}\n"
    "\n"
    /* SSAO: 16 points in the hemisphere over the normal, cosine-distributed
     * around a golden-angle spiral, nearer the centre first. */
    "#define SSAO_N 16\n"
    "float3 ssao_point(int i)\n"
    "{\n"
    "    float u = (i + 0.5) / SSAO_N, a = i * 2.39996323, r = sqrt(u);\n"
    "    float k = (i + 1.0) / SSAO_N;\n"
    "    return float3(r * cos(a), r * sin(a), sqrt(1.0 - u)) * lerp(0.15, 1.0, k * k);\n"
    "}\n"
    "\n"
    "[numthreads(8, 8, 1)]\n"
    "void CSSSAO(const uint2 pix : SV_DispatchThreadID)\n"
    "{\n"
    "    const GTAOConstants c = g_GTAOConsts;\n"
    "    if (any(pix >= (uint2)c.ViewportSize))\n"
    "        return;\n"
    "    int2 mx = c.ViewportSize - 1;\n"
    "    float2 uv = (pix + 0.5) * c.ViewportPixelSize;\n"
    "    float z = g_srcWorkingDepth.Load(int3(pix, 0));\n"
    "    float zl = g_srcWorkingDepth.Load(int3(max((int2)pix - int2(1, 0), 0), 0));\n"
    "    float zr = g_srcWorkingDepth.Load(int3(min((int2)pix + int2(1, 0), mx), 0));\n"
    "    float zt = g_srcWorkingDepth.Load(int3(max((int2)pix - int2(0, 1), 0), 0));\n"
    "    float zb = g_srcWorkingDepth.Load(int3(min((int2)pix + int2(0, 1), mx), 0));\n"
    "    float4 e = XeGTAO_CalculateEdges(z, zl, zr, zt, zb);\n"
    "    g_outWorkingEdges[pix] = XeGTAO_PackEdges(e);\n"
    "    float3 P = XeGTAO_ComputeViewspacePosition(uv, z, c);\n"
    "    float3 n = XeGTAO_CalculateNormal(e, P,\n"
    "        XeGTAO_ComputeViewspacePosition(uv + float2(-1, 0) * c.ViewportPixelSize, zl, c),\n"
    "        XeGTAO_ComputeViewspacePosition(uv + float2(1, 0) * c.ViewportPixelSize, zr, c),\n"
    "        XeGTAO_ComputeViewspacePosition(uv + float2(0, -1) * c.ViewportPixelSize, zt, c),\n"
    "        XeGTAO_ComputeViewspacePosition(uv + float2(0, 1) * c.ViewportPixelSize, zb, c));\n"
    "    float a = SpatioTemporalNoise(pix, 0).x * 6.28318531;\n"
    "    float3 rv = float3(cos(a), sin(a), 0);\n"
    "    float3 T = normalize(rv - n * dot(rv, n) + float3(0, 0, 1e-4));\n"
    "    float3 B = cross(n, T);\n"
    "    float R = c.EffectRadius, occ = 0;\n"
    "    [unroll] for (int i = 0; i < SSAO_N; i++) {\n"
    "        float3 k = ssao_point(i);\n"
    "        float3 s = P + (T * k.x + B * k.y + n * k.z) * R;\n"
    "        if (s.z <= 1e-3)\n"
    "            continue;\n"
    "        float2 suv = (s.xy / s.z - c.NDCToViewAdd) / c.NDCToViewMul;\n"
    "        if (any(suv < 0) || any(suv > 1))\n"
    "            continue;\n"
    "        float px = length((suv - uv) * c.ViewportSize);\n"
    "        float mip = clamp(log2(max(px, 1.0)) - c.DepthMIPSamplingOffset, 0, XE_GTAO_DEPTH_MIP_LEVELS - 1);\n"
    "        float sz = g_srcWorkingDepth.SampleLevel(g_samplerPointClamp, suv, mip);\n"
    "        float range = saturate(R / max(abs(z - sz), 1e-4));\n"
    "        occ += (sz < s.z - 0.03 * R) ? range * range : 0;\n"
    "    }\n"
    "    float vis = max(0.03, 1.0 - occ / SSAO_N);\n"
    "    XeGTAO_OutputWorkingTerm(pix, vis, n, g_outWorkingAOTerm);\n"
    "}\n";

/* The depth copy and the apply pass (pixel shaders, compiled with TWOC_MS
 * and TWOC_HALF 0 or 1). Their bindings are high (b8, t8-t10) so that only
 * those slots need clearing afterwards. */
static const char g_ps_hlsl[] =
    "cbuffer twoc_ao : register(b8)\n"
    "{\n"
    "    float4 g_fog;   /* start, end, density, mode (D3DFOG_*) */\n"
    "    float4 g_full;  /* 1 / w, 1 / h, w, h of the back buffer */\n"
    "    float4 g_mix;   /* strength, depth unpack mul, depth unpack add, - */\n"
    "    uint4 g_opt;    /* debug view, -, working w, working h */\n"
    "};\n"
    "#if TWOC_MS\n"
    "Texture2DMS<float> g_depth : register(t8);\n"
    "float raw_depth(int2 p) { return g_depth.Load(p, 0); }\n"
    "#else\n"
    "Texture2D<float> g_depth : register(t8);\n"
    "float raw_depth(int2 p) { return g_depth.Load(int3(p, 0)); }\n"
    "#endif\n"
    "Texture2D<uint> g_aot : register(t9);\n"
    "Texture2D<float> g_vz : register(t10);\n"
    "struct V { float4 p : SV_Position; };\n"
    "V vs(uint id : SV_VertexID)\n"
    "{\n"
    "    V o;\n"
    "    float2 uv = float2((id << 1) & 2, id & 2);\n"
    "    o.p = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "    return o;\n"
    "}\n"
    "float prep_ps(V i) : SV_Target\n"
    "{\n"
    "    int2 p = int2(i.p.xy);\n"
    "#if TWOC_HALF\n"
    "    int2 q = p * 2, m = int2(g_full.zw) - 1;\n"
    "    return min(min(raw_depth(min(q, m)), raw_depth(min(q + int2(1, 0), m))),\n"
    "               min(raw_depth(min(q + int2(0, 1), m)), raw_depth(min(q + int2(1, 1), m))));\n"
    "#else\n"
    "    return raw_depth(p);\n"
    "#endif\n"
    "}\n"
    "float4 apply_ps(V i) : SV_Target\n"
    "{\n"
    "    int2 p = int2(i.p.xy);\n"
    "    float d = raw_depth(p);\n"
    "    if (d >= 0.9999999)\n"
    "        return 1;\n"
    "    float z = g_mix.y / (g_mix.z - d), ao;\n"
    "#if TWOC_HALF\n"
    "    float2 h = i.p.xy * 0.5 - 0.5;\n"
    "    int2 b = int2(floor(h)), m = int2(g_opt.zw) - 1;\n"
    "    float2 f = h - b;\n"
    "    float ws = 0, as = 0;\n"
    "    [unroll] for (int k = 0; k < 4; k++) {\n"
    "        int2 o = int2(k & 1, k >> 1), t = clamp(b + o, 0, m);\n"
    "        float bw = (o.x ? f.x : 1 - f.x) * (o.y ? f.y : 1 - f.y) + 1e-3;\n"
    "        float dz = abs(g_vz.Load(int3(t, 0)) - z) / z;\n"
    "        float w = bw / (1e-3 + dz * dz * 400.0);\n"
    "        ws += w;\n"
    "        as += w * (g_aot.Load(int3(t, 0)).x / 255.0);\n"
    "    }\n"
    "    ao = as / ws;\n"
    "#else\n"
    "    ao = g_aot.Load(int3(p, 0)).x / 255.0;\n"
    "#endif\n"
    "    float fogk = 1;\n"
    "    if (g_fog.w == 3)\n"
    "        fogk = saturate((g_fog.y - z) / max(g_fog.y - g_fog.x, 1e-6));\n"
    "    else if (g_fog.w == 1)\n"
    "        fogk = saturate(exp(-g_fog.z * z));\n"
    "    else if (g_fog.w == 2)\n"
    "        fogk = saturate(exp(-(g_fog.z * z) * (g_fog.z * z)));\n"
    "    ao = lerp(1, saturate(ao), g_mix.x * fogk);\n"
    "    return float4(ao, ao, ao, 1);\n"
    "}\n";

/* fxc (D3DCompile) can't swizzle a literal as DXC can (XeGTAO writes 2.xx,
 * 0.5.xx, 1.xxxx): the copy of XeGTAO.hlsli it is given has each such
 * literal in parentheses ((2).xx), nothing else changed. */
static char *g_hlsli_fxc;
static size_t g_hlsli_fxc_n;

static int is_word(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int hlsli_for_fxc(void)
{
    const char *s = (const char *)g_xegtao_hlsli;
    size_t n = sizeof(g_xegtao_hlsli) - 1, i = 0, j = 0, k, m;

    if (g_hlsli_fxc)
        return 0;
    g_hlsli_fxc = (char *)malloc(n * 2);
    if (!g_hlsli_fxc)
        return -1;
    while (i < n) {
        if (s[i] >= '0' && s[i] <= '9' && (i == 0 || (!is_word(s[i - 1]) && s[i - 1] != '.'))) {
            k = i;
            while (k < n && s[k] >= '0' && s[k] <= '9')
                k++;
            if (k + 1 < n && s[k] == '.' && s[k + 1] >= '0' && s[k + 1] <= '9')
                for (k++; k < n && s[k] >= '0' && s[k] <= '9'; k++)
                    ;
            for (m = 0; k + 1 + m < n && s[k + 1 + m] == 'x'; m++)
                ;
            if (k < n && s[k] == '.' && m >= 2 && m <= 4 && (k + 1 + m >= n || !is_word(s[k + 1 + m]))) {
                g_hlsli_fxc[j++] = '(';
                memcpy(g_hlsli_fxc + j, s + i, k - i);
                j += k - i;
                g_hlsli_fxc[j++] = ')';
            } else {
                memcpy(g_hlsli_fxc + j, s + i, k - i);
                j += k - i;
            }
            i = k;
            continue;
        }
        g_hlsli_fxc[j++] = s[i++];
    }
    g_hlsli_fxc_n = j;
    return 0;
}

/* XeGTAO.hlsli includes XeGTAO.h; both come from the embedded copies. */
static HRESULT STDMETHODCALLTYPE inc_open(ID3DInclude *self, D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent,
                                          LPCVOID *data, UINT *bytes)
{
    (void)self; (void)type; (void)parent;
    if (_stricmp(name, "XeGTAO.h") == 0) {
        *data = g_xegtao_h;
        *bytes = (UINT)(sizeof(g_xegtao_h) - 1);
        return S_OK;
    }
    if (_stricmp(name, "XeGTAO.hlsli") == 0 && hlsli_for_fxc() == 0) {
        *data = g_hlsli_fxc;
        *bytes = (UINT)g_hlsli_fxc_n;
        return S_OK;
    }
    return E_FAIL;
}

static HRESULT STDMETHODCALLTYPE inc_close(ID3DInclude *self, LPCVOID data)
{
    (void)self; (void)data;
    return S_OK;
}

static const ID3DIncludeVtbl g_inc_vtbl = { inc_open, inc_close };
static ID3DInclude g_inc = { (ID3DIncludeVtbl *)&g_inc_vtbl };

/* GTAOConstants (XeGTAO.h), as HLSL packs it. */
typedef struct {
    int vp_size[2];
    float vp_pixel[2];
    float depth_unpack[2], tan_half_fov[2];
    float ndc_mul[2], ndc_add[2];
    float ndc_mul_px[2];
    float effect_radius, falloff_range;
    float radius_mul, pad0, final_power, denoise_beta;
    float sample_power, thin_comp, mip_offset;
    int noise_index;
} gtao_cb_t;

typedef struct {
    float fog[4], full[4], mix[4];
    unsigned opt[4];
} ps_cb_t;

static struct {
    int cs_state;                       /* 0 not tried, 1 compiled, -1 failed */
    ID3D11ComputeShader *prefilter, *gtao, *ssao, *denoise, *denoise_mid;
    int ps_state;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *prep[2][2], *apply[2][2];    /* [TWOC_MS][TWOC_HALF] */
    ID3D11SamplerState *point;
    ID3D11Buffer *gtao_cb, *ps_cb;
    ID3D11BlendState *multiply;
    ID3D11DepthStencilState *no_depth;
    /* At the working size ww x wh */
    unsigned ww, wh;
    ID3D11Texture2D *ndc, *vz, *aot, *edges, *fin;
    ID3D11RenderTargetView *ndc_rtv;
    ID3D11ShaderResourceView *ndc_srv, *vz_srv, *aot_srv, *edges_srv, *fin_srv;
    ID3D11UnorderedAccessView *vz_uav[5], *aot_uav, *edges_uav, *fin_uav;
    /* GPU time */
#define AO_QSETS 6
    struct {
        ID3D11Query *dis, *ts[AO_PASSES + 1];
        int pending;
    } q[AO_QSETS];
    unsigned q_next;
    ao_stats_t st;
} g_ao;
static int g_ao_gpu = -1;               /* RECOMP_AO_GPU: -1 not read yet */

static HRESULT compile(const char *src, size_t n, const char *name, const char *entry, const char *target,
                       const D3D_SHADER_MACRO *defs, ID3DBlob **out)
{
    ID3DBlob *err = NULL;
    HRESULT hr = D3DCompile(src, n, name, defs, &g_inc, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err);

    if (FAILED(hr))
        fprintf(stderr, "[AO] %s %s not compiled (0x%08lX)%s%.900s\n", name, entry, (unsigned long)hr,
                err ? ": " : "", err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "");
    RELEASE(ID3D10Blob, err);
    return hr;
}

static HRESULT make_cs(const char *entry, ID3D11ComputeShader **cs)
{
    ID3DBlob *b = NULL;
    HRESULT hr = compile(g_cs_hlsl, sizeof(g_cs_hlsl) - 1, "twoc_ao_cs", entry, "cs_5_0", NULL, &b);

    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateComputeShader(d3d8_GetD3D11Device(), ID3D10Blob_GetBufferPointer(b),
                                              ID3D10Blob_GetBufferSize(b), NULL, cs);
    RELEASE(ID3D10Blob, b);
    return hr;
}

static HRESULT make_ps(const char *entry, int ms, int half, ID3D11PixelShader **ps)
{
    D3D_SHADER_MACRO defs[3] = { { "TWOC_MS", ms ? "1" : "0" }, { "TWOC_HALF", half ? "1" : "0" }, { NULL, NULL } };
    ID3DBlob *b = NULL;
    HRESULT hr = compile(g_ps_hlsl, sizeof(g_ps_hlsl) - 1, "twoc_ao_ps", entry, "ps_5_0", defs, &b);

    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreatePixelShader(d3d8_GetD3D11Device(), ID3D10Blob_GetBufferPointer(b),
                                            ID3D10Blob_GetBufferSize(b), NULL, ps);
    RELEASE(ID3D10Blob, b);
    return hr;
}

static double ms_since(const LARGE_INTEGER *t0)
{
    LARGE_INTEGER f, t1;

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t1);
    return (double)(t1.QuadPart - t0->QuadPart) * 1000.0 / (double)f.QuadPart;
}

/* The compute shaders, the full-screen pass's states and the constants. */
static int cs_init(void)
{
    ID3D11Device *d = d3d8_GetD3D11Device();
    D3D11_SAMPLER_DESC sd;
    D3D11_BUFFER_DESC bd;
    D3D11_BLEND_DESC bl;
    D3D11_DEPTH_STENCIL_DESC ds;
    LARGE_INTEGER t0;
    HRESULT hr;

    if (g_ao.cs_state)
        return g_ao.cs_state > 0 ? 0 : -1;
    g_ao.cs_state = -1;
    QueryPerformanceCounter(&t0);
    hr = make_cs("CSPrefilterDepths16x16", &g_ao.prefilter);
    if (SUCCEEDED(hr))
        hr = make_cs("CSGTAOHigh", &g_ao.gtao);
    if (SUCCEEDED(hr))
        hr = make_cs("CSSSAO", &g_ao.ssao);
    if (SUCCEEDED(hr))
        hr = make_cs("CSDenoiseLastPass", &g_ao.denoise);
    if (SUCCEEDED(hr))
        hr = make_cs("CSDenoisePass", &g_ao.denoise_mid);
    if (SUCCEEDED(hr)) {
        ID3DBlob *b = NULL;
        D3D_SHADER_MACRO defs[3] = { { "TWOC_MS", "0" }, { "TWOC_HALF", "0" }, { NULL, NULL } };
        hr = compile(g_ps_hlsl, sizeof(g_ps_hlsl) - 1, "twoc_ao_ps", "vs", "vs_5_0", defs, &b);
        if (SUCCEEDED(hr))
            hr = ID3D11Device_CreateVertexShader(d, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b),
                                                 NULL, &g_ao.vs);
        RELEASE(ID3D10Blob, b);
    }
    if (SUCCEEDED(hr)) {
        memset(&sd, 0, sizeof(sd));
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        hr = ID3D11Device_CreateSamplerState(d, &sd, &g_ao.point);
    }
    if (SUCCEEDED(hr)) {
        memset(&bd, 0, sizeof(bd));
        bd.ByteWidth = sizeof(gtao_cb_t);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = ID3D11Device_CreateBuffer(d, &bd, NULL, &g_ao.gtao_cb);
        bd.ByteWidth = sizeof(ps_cb_t);
        if (SUCCEEDED(hr))
            hr = ID3D11Device_CreateBuffer(d, &bd, NULL, &g_ao.ps_cb);
    }
    if (SUCCEEDED(hr)) {                /* dest = dest x the pass's colour; alpha kept */
        memset(&bl, 0, sizeof(bl));
        bl.RenderTarget[0].BlendEnable = TRUE;
        bl.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
        bl.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_COLOR;
        bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
        bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = ID3D11Device_CreateBlendState(d, &bl, &g_ao.multiply);
    }
    if (SUCCEEDED(hr)) {
        memset(&ds, 0, sizeof(ds));
        ds.DepthEnable = FALSE;
        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        ds.DepthFunc = D3D11_COMPARISON_ALWAYS;
        hr = ID3D11Device_CreateDepthStencilState(d, &ds, &g_ao.no_depth);
    }
    if (FAILED(hr)) {
        fprintf(stderr, "[AO] ambient occlusion not available (0x%08lX): the picture is drawn without it\n",
                (unsigned long)hr);
        return -1;
    }
    fprintf(stderr, "[AO] XeGTAO's and the SSAO shaders compiled in %.0f ms\n", ms_since(&t0));
    g_ao.cs_state = 1;
    return 0;
}

/* The pixel shaders of one depth kind and size (compiled at their first use). */
static int ps_get(int ms, int half)
{
    LARGE_INTEGER t0;
    HRESULT hr = S_OK;

    if (g_ao.prep[ms][half] && g_ao.apply[ms][half])
        return 0;
    QueryPerformanceCounter(&t0);
    if (!g_ao.prep[ms][half])
        hr = make_ps("prep_ps", ms, half, &g_ao.prep[ms][half]);
    if (SUCCEEDED(hr) && !g_ao.apply[ms][half])
        hr = make_ps("apply_ps", ms, half, &g_ao.apply[ms][half]);
    if (FAILED(hr))
        return -1;
    fprintf(stderr, "[AO] depth and apply shaders (%s depth, %s size) compiled in %.0f ms\n",
            ms ? "multisampled" : "single-sampled", half ? "half" : "full", ms_since(&t0));
    return 0;
}

static void size_release(void)
{
    int i;

    RELEASE(ID3D11RenderTargetView, g_ao.ndc_rtv);
    RELEASE(ID3D11ShaderResourceView, g_ao.ndc_srv);
    RELEASE(ID3D11ShaderResourceView, g_ao.vz_srv);
    RELEASE(ID3D11ShaderResourceView, g_ao.aot_srv);
    RELEASE(ID3D11ShaderResourceView, g_ao.edges_srv);
    RELEASE(ID3D11ShaderResourceView, g_ao.fin_srv);
    for (i = 0; i < 5; i++)
        RELEASE(ID3D11UnorderedAccessView, g_ao.vz_uav[i]);
    RELEASE(ID3D11UnorderedAccessView, g_ao.aot_uav);
    RELEASE(ID3D11UnorderedAccessView, g_ao.edges_uav);
    RELEASE(ID3D11UnorderedAccessView, g_ao.fin_uav);
    RELEASE(ID3D11Texture2D, g_ao.ndc);
    RELEASE(ID3D11Texture2D, g_ao.vz);
    RELEASE(ID3D11Texture2D, g_ao.aot);
    RELEASE(ID3D11Texture2D, g_ao.edges);
    RELEASE(ID3D11Texture2D, g_ao.fin);
    g_ao.ww = g_ao.wh = 0;
}

static HRESULT make_tex(unsigned w, unsigned h, unsigned mips, DXGI_FORMAT fmt, UINT bind, ID3D11Texture2D **tex)
{
    D3D11_TEXTURE2D_DESC td;

    memset(&td, 0, sizeof(td));
    td.Width = w;
    td.Height = h;
    td.MipLevels = mips;
    td.ArraySize = 1;
    td.Format = fmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = bind;
    return ID3D11Device_CreateTexture2D(d3d8_GetD3D11Device(), &td, NULL, tex);
}

/* The working textures at w x h: the depth copy, XeGTAO's view depth with
 * its MIP levels, the working term, the edges and the denoised term. */
static int size_fit(unsigned w, unsigned h)
{
    ID3D11Device *d = d3d8_GetD3D11Device();
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud;
    HRESULT hr;
    unsigned i;

    if (g_ao.ww == w && g_ao.wh == h && g_ao.fin)
        return 0;
    size_release();
    hr = make_tex(w, h, 1, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, &g_ao.ndc);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateRenderTargetView(d, (ID3D11Resource *)g_ao.ndc, NULL, &g_ao.ndc_rtv);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateShaderResourceView(d, (ID3D11Resource *)g_ao.ndc, NULL, &g_ao.ndc_srv);
    if (SUCCEEDED(hr))
        hr = make_tex(w, h, 5, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
                      &g_ao.vz);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateShaderResourceView(d, (ID3D11Resource *)g_ao.vz, NULL, &g_ao.vz_srv);
    for (i = 0; i < 5 && SUCCEEDED(hr); i++) {
        memset(&ud, 0, sizeof(ud));
        ud.Format = DXGI_FORMAT_R32_FLOAT;
        ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        ud.Texture2D.MipSlice = i;
        hr = ID3D11Device_CreateUnorderedAccessView(d, (ID3D11Resource *)g_ao.vz, &ud, &g_ao.vz_uav[i]);
    }
    if (SUCCEEDED(hr))
        hr = make_tex(w, h, 1, DXGI_FORMAT_R8_UINT, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
                      &g_ao.aot);
    if (SUCCEEDED(hr))
        hr = make_tex(w, h, 1, DXGI_FORMAT_R8_UNORM, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
                      &g_ao.edges);
    if (SUCCEEDED(hr))
        hr = make_tex(w, h, 1, DXGI_FORMAT_R8_UINT, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
                      &g_ao.fin);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateShaderResourceView(d, (ID3D11Resource *)g_ao.aot, NULL, &g_ao.aot_srv);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateShaderResourceView(d, (ID3D11Resource *)g_ao.edges, NULL, &g_ao.edges_srv);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateShaderResourceView(d, (ID3D11Resource *)g_ao.fin, NULL, &g_ao.fin_srv);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateUnorderedAccessView(d, (ID3D11Resource *)g_ao.aot, NULL, &g_ao.aot_uav);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateUnorderedAccessView(d, (ID3D11Resource *)g_ao.edges, NULL, &g_ao.edges_uav);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateUnorderedAccessView(d, (ID3D11Resource *)g_ao.fin, NULL, &g_ao.fin_uav);
    if (FAILED(hr)) {
        size_release();
        fprintf(stderr, "[AO] no textures for a %ux%u occlusion (0x%08lX)\n", w, h, (unsigned long)hr);
        return -1;
    }
    g_ao.ww = w;
    g_ao.wh = h;
    fprintf(stderr, "[AO] working textures %ux%u\n", w, h);
    return 0;
}

/* XeGTAO::GTAOUpdateConstants (XeGTAO.h) in C: the settings at their
 * defaults (DenoisePasses 1) but the radius, the projection row-major. */
static void ao_constants(gtao_cb_t *c, unsigned w, unsigned h, const float *proj, float radius)
{
    float mul = -proj[3 * 4 + 2], add = proj[2 * 4 + 2];

    memset(c, 0, sizeof(*c));
    c->vp_size[0] = (int)w;
    c->vp_size[1] = (int)h;
    c->vp_pixel[0] = 1.0f / (float)w;
    c->vp_pixel[1] = 1.0f / (float)h;
    if (mul * add < 0)
        add = -add;
    c->depth_unpack[0] = mul;
    c->depth_unpack[1] = add;
    c->tan_half_fov[0] = 1.0f / proj[0 * 4 + 0];
    c->tan_half_fov[1] = 1.0f / proj[1 * 4 + 1];
    c->ndc_mul[0] = c->tan_half_fov[0] * 2.0f;
    c->ndc_mul[1] = c->tan_half_fov[1] * -2.0f;
    c->ndc_add[0] = c->tan_half_fov[0] * -1.0f;
    c->ndc_add[1] = c->tan_half_fov[1] * 1.0f;
    c->ndc_mul_px[0] = c->ndc_mul[0] * c->vp_pixel[0];
    c->ndc_mul_px[1] = c->ndc_mul[1] * c->vp_pixel[1];
    c->effect_radius = radius;
    c->falloff_range = 0.615f;          /* XE_GTAO_DEFAULT_FALLOFF_RANGE */
    c->denoise_beta = 1.2f;
    c->radius_mul = 1.457f;             /* XE_GTAO_DEFAULT_RADIUS_MULTIPLIER */
    c->sample_power = 2.0f;
    c->thin_comp = 0.0f;
    c->final_power = 2.2f;
    c->mip_offset = 3.30f;
    c->noise_index = 0;                 /* no TAA */
}

/* ── GPU time ──────────────────────────────────────────────── */

static void gpu_collect(ID3D11DeviceContext *ctx)
{
    unsigned k;

    for (k = 0; k < AO_QSETS; k++) {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
        UINT64 t[AO_PASSES + 1];
        int i, ok = 1;
        if (!g_ao.q[k].pending)
            continue;
        if (ID3D11DeviceContext_GetData(ctx, (ID3D11Asynchronous *)g_ao.q[k].dis, &dj, sizeof(dj),
                                        D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
            continue;
        for (i = 0; i <= AO_PASSES && ok; i++)
            ok = ID3D11DeviceContext_GetData(ctx, (ID3D11Asynchronous *)g_ao.q[k].ts[i], &t[i], sizeof(t[i]),
                                             D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
        if (!ok)
            continue;
        g_ao.q[k].pending = 0;
        if (dj.Disjoint || !dj.Frequency)
            continue;
        for (i = 0; i < AO_PASSES; i++)
            g_ao.st.ms[i] += (double)(t[i + 1] - t[i]) * 1000.0 / (double)dj.Frequency;
        g_ao.st.total_ms += (double)(t[AO_PASSES] - t[0]) * 1000.0 / (double)dj.Frequency;
        g_ao.st.timed++;
    }
}

/* The query set for this pass, or -1 (not timing, or none free). */
static int gpu_begin(ID3D11DeviceContext *ctx)
{
    ID3D11Device *d = d3d8_GetD3D11Device();
    D3D11_QUERY_DESC qd;
    unsigned k;
    int i;

    if (g_ao_gpu < 0) {
        const char *e = getenv("RECOMP_AO_GPU");
        g_ao_gpu = e && atoi(e) > 0;
    }
    if (!g_ao_gpu)
        return -1;
    gpu_collect(ctx);
    k = g_ao.q_next % AO_QSETS;
    if (g_ao.q[k].pending)
        return -1;
    if (!g_ao.q[k].dis) {
        memset(&qd, 0, sizeof(qd));
        qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        if (FAILED(ID3D11Device_CreateQuery(d, &qd, &g_ao.q[k].dis)))
            return -1;
        qd.Query = D3D11_QUERY_TIMESTAMP;
        for (i = 0; i <= AO_PASSES; i++)
            if (FAILED(ID3D11Device_CreateQuery(d, &qd, &g_ao.q[k].ts[i])))
                return -1;
    }
    g_ao.q_next++;
    ID3D11DeviceContext_Begin(ctx, (ID3D11Asynchronous *)g_ao.q[k].dis);
    ID3D11DeviceContext_End(ctx, (ID3D11Asynchronous *)g_ao.q[k].ts[0]);
    return (int)k;
}

static void gpu_mark(ID3D11DeviceContext *ctx, int k, int pass)
{
    if (k >= 0)
        ID3D11DeviceContext_End(ctx, (ID3D11Asynchronous *)g_ao.q[k].ts[pass + 1]);
}

static void gpu_end(ID3D11DeviceContext *ctx, int k)
{
    if (k < 0)
        return;
    ID3D11DeviceContext_End(ctx, (ID3D11Asynchronous *)g_ao.q[k].dis);
    g_ao.q[k].pending = 1;
}

void ao_stats_take(ao_stats_t *s)
{
    int i;

    *s = g_ao.st;
    if (s->timed) {
        for (i = 0; i < AO_PASSES; i++)
            s->ms[i] /= s->timed;
        s->total_ms /= s->timed;
    }
    memset(&g_ao.st, 0, sizeof(g_ao.st));
}

/* ── The pass ──────────────────────────────────────────────── */

typedef struct {
    ID3D11RenderTargetView *rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
    ID3D11DepthStencilView *dsv;
    D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT n_vp, sample_mask, stencil_ref;
    ID3D11RasterizerState *rs;
    ID3D11BlendState *bs;
    FLOAT blend_factor[4];
    ID3D11DepthStencilState *dss;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11GeometryShader *gs;
    ID3D11InputLayout *il;
    D3D11_PRIMITIVE_TOPOLOGY topo;
} saved_t;

static void state_save(ID3D11DeviceContext *ctx, saved_t *o)
{
    memset(o, 0, sizeof(*o));
    ID3D11DeviceContext_OMGetRenderTargets(ctx, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, o->rtv, &o->dsv);
    o->n_vp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ID3D11DeviceContext_RSGetViewports(ctx, &o->n_vp, o->vp);
    ID3D11DeviceContext_RSGetState(ctx, &o->rs);
    ID3D11DeviceContext_OMGetBlendState(ctx, &o->bs, o->blend_factor, &o->sample_mask);
    ID3D11DeviceContext_OMGetDepthStencilState(ctx, &o->dss, &o->stencil_ref);
    ID3D11DeviceContext_VSGetShader(ctx, &o->vs, NULL, NULL);
    ID3D11DeviceContext_PSGetShader(ctx, &o->ps, NULL, NULL);
    ID3D11DeviceContext_GSGetShader(ctx, &o->gs, NULL, NULL);
    ID3D11DeviceContext_IAGetInputLayout(ctx, &o->il);
    ID3D11DeviceContext_IAGetPrimitiveTopology(ctx, &o->topo);
}

static void state_restore(ID3D11DeviceContext *ctx, saved_t *o)
{
    int i;

    ID3D11DeviceContext_OMSetRenderTargets(ctx, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, o->rtv, o->dsv);
    ID3D11DeviceContext_RSSetViewports(ctx, o->n_vp, o->vp);
    ID3D11DeviceContext_RSSetState(ctx, o->rs);
    ID3D11DeviceContext_OMSetBlendState(ctx, o->bs, o->blend_factor, o->sample_mask);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx, o->dss, o->stencil_ref);
    ID3D11DeviceContext_VSSetShader(ctx, o->vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, o->ps, NULL, 0);
    ID3D11DeviceContext_GSSetShader(ctx, o->gs, NULL, 0);
    ID3D11DeviceContext_IASetInputLayout(ctx, o->il);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, o->topo);
    for (i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
        RELEASE(ID3D11RenderTargetView, o->rtv[i]);
    RELEASE(ID3D11DepthStencilView, o->dsv);
    RELEASE(ID3D11RasterizerState, o->rs);
    RELEASE(ID3D11BlendState, o->bs);
    RELEASE(ID3D11DepthStencilState, o->dss);
    RELEASE(ID3D11VertexShader, o->vs);
    RELEASE(ID3D11PixelShader, o->ps);
    RELEASE(ID3D11GeometryShader, o->gs);
    RELEASE(ID3D11InputLayout, o->il);
}

static void viewport(ID3D11DeviceContext *ctx, unsigned w, unsigned h)
{
    D3D11_VIEWPORT vp;

    vp.TopLeftX = vp.TopLeftY = 0;
    vp.Width = (FLOAT)w;
    vp.Height = (FLOAT)h;
    vp.MinDepth = 0;
    vp.MaxDepth = 1;
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
}

static void cs_unbind(ID3D11DeviceContext *ctx)
{
    static ID3D11ShaderResourceView *const no_srv[2];
    static ID3D11UnorderedAccessView *const no_uav[5];

    ID3D11DeviceContext_CSSetShaderResources(ctx, 0, 2, no_srv);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 5, no_uav, NULL);
}

int ao_apply(const ao_params_t *p, ID3D11ShaderResourceView *depth, unsigned samples, ID3D11RenderTargetView *rtv,
             unsigned w, unsigned h)
{
    ID3D11DeviceContext *ctx = d3d8_GetD3D11Context();
    static ID3D11ShaderResourceView *const none[3];
    int ms = samples > 1, half = p->half != 0, k;
    unsigned ww = half ? (w + 1) / 2 : w, wh = half ? (h + 1) / 2 : h;
    ID3D11ShaderResourceView *srv[3];
    ID3D11Buffer *cb;
    ID3D11ShaderResourceView *fin;
    gtao_cb_t gc;
    ps_cb_t pc;
    saved_t old;

    if (!depth || !rtv || !w || !h || p->proj[0] == 0.0f || p->proj[5] == 0.0f || cs_init() != 0 ||
        ps_get(ms, half) != 0 || size_fit(ww, wh) != 0)
        return -1;
    ao_constants(&gc, ww, wh, p->proj, p->radius);
    memset(&pc, 0, sizeof(pc));
    pc.fog[0] = p->fog_start;
    pc.fog[1] = p->fog_end;
    pc.fog[2] = p->fog_density;
    pc.fog[3] = p->fog_mode >= 1 && p->fog_mode <= 3 ? (float)p->fog_mode : 0.0f;
    pc.full[0] = 1.0f / (float)w;
    pc.full[1] = 1.0f / (float)h;
    pc.full[2] = (float)w;
    pc.full[3] = (float)h;
    pc.mix[0] = p->strength < 0 ? 0 : p->strength;
    pc.mix[1] = gc.depth_unpack[0];
    pc.mix[2] = gc.depth_unpack[1];
    pc.opt[0] = (unsigned)p->debug;
    pc.opt[2] = ww;
    pc.opt[3] = wh;
    ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)g_ao.gtao_cb, 0, NULL, &gc, 0, 0);
    ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)g_ao.ps_cb, 0, NULL, &pc, 0, 0);

    state_save(ctx, &old);
    k = gpu_begin(ctx);
    ID3D11DeviceContext_RSSetState(ctx, NULL);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx, g_ao.no_depth, 0);
    ID3D11DeviceContext_IASetInputLayout(ctx, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_GSSetShader(ctx, NULL, NULL, 0);
    ID3D11DeviceContext_VSSetShader(ctx, g_ao.vs, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx, 8, 1, &g_ao.ps_cb);

    /* 1. The depth at the working size. */
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &g_ao.ndc_rtv, NULL);
    ID3D11DeviceContext_OMSetBlendState(ctx, NULL, NULL, 0xFFFFFFFFu);
    viewport(ctx, ww, wh);
    srv[0] = depth;
    ID3D11DeviceContext_PSSetShaderResources(ctx, 8, 1, srv);
    ID3D11DeviceContext_PSSetShader(ctx, g_ao.prep[ms][half], NULL, 0);
    ID3D11DeviceContext_Draw(ctx, 3, 0);
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 0, NULL, NULL);
    gpu_mark(ctx, k, 0);

    /* 2. XeGTAO's view depth and its MIP levels. */
    cb = g_ao.gtao_cb;
    ID3D11DeviceContext_CSSetConstantBuffers(ctx, 0, 1, &cb);
    ID3D11DeviceContext_CSSetSamplers(ctx, 0, 1, &g_ao.point);
    ID3D11DeviceContext_CSSetShader(ctx, g_ao.prefilter, NULL, 0);
    ID3D11DeviceContext_CSSetShaderResources(ctx, 0, 1, &g_ao.ndc_srv);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 5, g_ao.vz_uav, NULL);
    ID3D11DeviceContext_Dispatch(ctx, (ww + 15) / 16, (wh + 15) / 16, 1);
    cs_unbind(ctx);
    gpu_mark(ctx, k, 1);

    /* 3. The occlusion and the edges. */
    {
        ID3D11UnorderedAccessView *u[2] = { g_ao.aot_uav, g_ao.edges_uav };
        ID3D11DeviceContext_CSSetShader(ctx, p->mode == TWOC_AO_SSAO ? g_ao.ssao : g_ao.gtao, NULL, 0);
        ID3D11DeviceContext_CSSetShaderResources(ctx, 0, 1, &g_ao.vz_srv);
        ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 2, u, NULL);
        ID3D11DeviceContext_Dispatch(ctx, (ww + 7) / 8, (wh + 7) / 8, 1);
        cs_unbind(ctx);
    }
    gpu_mark(ctx, k, 2);

    /* 4. Denoised, in passes between the working term and the other texture (the last one scales the term
     * back, XeGTAO_Denoise's finalApply). */
    {
        int i, n = p->denoise < 1 ? 1 : p->denoise > 4 ? 4 : p->denoise;
        for (i = 0; i < n; i++) {
            ID3D11ShaderResourceView *s[2] = { i & 1 ? g_ao.fin_srv : g_ao.aot_srv, g_ao.edges_srv };
            ID3D11UnorderedAccessView *u = i & 1 ? g_ao.aot_uav : g_ao.fin_uav;
            ID3D11DeviceContext_CSSetShader(ctx, i == n - 1 ? g_ao.denoise : g_ao.denoise_mid, NULL, 0);
            ID3D11DeviceContext_CSSetShaderResources(ctx, 0, 2, s);
            ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &u, NULL);
            ID3D11DeviceContext_Dispatch(ctx, ((ww + 1) / 2 + 7) / 8, (wh + 7) / 8, 1);
            cs_unbind(ctx);
        }
        ID3D11DeviceContext_CSSetShader(ctx, NULL, NULL, 0);
        fin = n & 1 ? g_ao.fin_srv : g_ao.aot_srv;
    }
    gpu_mark(ctx, k, 3);

    /* 5. Multiplied into the back buffer (RECOMP_AO_DEBUG: written as it is). */
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
    ID3D11DeviceContext_OMSetBlendState(ctx, p->debug ? NULL : g_ao.multiply, NULL, 0xFFFFFFFFu);
    viewport(ctx, w, h);
    srv[0] = depth;
    srv[1] = fin;
    srv[2] = g_ao.vz_srv;
    ID3D11DeviceContext_PSSetShaderResources(ctx, 8, 3, srv);
    ID3D11DeviceContext_PSSetShader(ctx, g_ao.apply[ms][half], NULL, 0);
    ID3D11DeviceContext_Draw(ctx, 3, 0);
    ID3D11DeviceContext_PSSetShaderResources(ctx, 8, 3, none);
    gpu_mark(ctx, k, 4);
    gpu_end(ctx, k);
    state_restore(ctx, &old);
    g_ao.st.runs++;
    return 0;
}

size_t ao_mem_bytes(void)
{
    size_t px = (size_t)g_ao.ww * g_ao.wh;

    return g_ao.fin ? px * 4 + px * 4 * 4 / 3 + px * 3 : 0;
}

void ao_release(void)
{
    int i, j;
    unsigned k;

    size_release();
    RELEASE(ID3D11ComputeShader, g_ao.prefilter);
    RELEASE(ID3D11ComputeShader, g_ao.gtao);
    RELEASE(ID3D11ComputeShader, g_ao.ssao);
    RELEASE(ID3D11ComputeShader, g_ao.denoise);
    RELEASE(ID3D11ComputeShader, g_ao.denoise_mid);
    RELEASE(ID3D11VertexShader, g_ao.vs);
    for (i = 0; i < 2; i++)
        for (j = 0; j < 2; j++) {
            RELEASE(ID3D11PixelShader, g_ao.prep[i][j]);
            RELEASE(ID3D11PixelShader, g_ao.apply[i][j]);
        }
    RELEASE(ID3D11SamplerState, g_ao.point);
    RELEASE(ID3D11Buffer, g_ao.gtao_cb);
    RELEASE(ID3D11Buffer, g_ao.ps_cb);
    RELEASE(ID3D11BlendState, g_ao.multiply);
    RELEASE(ID3D11DepthStencilState, g_ao.no_depth);
    for (k = 0; k < AO_QSETS; k++) {
        RELEASE(ID3D11Query, g_ao.q[k].dis);
        for (i = 0; i <= AO_PASSES; i++)
            RELEASE(ID3D11Query, g_ao.q[k].ts[i]);
        g_ao.q[k].pending = 0;
    }
    g_ao.cs_state = 0;
}
