/**
 * D3D8 Compatibility Layer - Internal Header
 *
 * Shared types and declarations for the D3D8→D3D11 implementation.
 * Not part of the public API - only included by d3d8_*.c files.
 */

#ifndef BURNOUT3_D3D8_INTERNAL_H
#define BURNOUT3_D3D8_INTERNAL_H

#define COBJMACROS
#include "d3d8_xbox.h"

/* Portable: D3D8 device accessor used by recompiled code on both backends
 * (d3d8_device.c on Windows, d3d8_gl.c on POSIX). */
IDirect3DDevice8 *d3d8_GetDevice(void);

#if defined(_WIN32)
/* === Everything below is the D3D11 backend. The POSIX d3d8_compat
 * library (d3d8_gl.c) implements its own internal state and does not
 * need any of these declarations. === */

#include <d3d11.h>
#include <dxgi.h>

/* ================================================================
 * The time spent inside the D3D11 context's calls (twoc notes 18.98)
 *
 * Local measurement aid: while d3d8_ctx_timing is nonzero, each context
 * call made through the COM macros below (those the draws, uploads,
 * copies and binds use) is timed with QueryPerformanceCounter and adds its
 * ticks to d3d8_ctx_ticks and one to d3d8_ctx_calls; otherwise it costs a
 * load and a branch. The caller turns it on around the work it measures
 * (twoc: one draw in 32) and reads the sums; the calls themselves are made
 * exactly as before, in the same order, on the same context.
 * ================================================================ */

extern int    d3d8_ctx_timing;
extern LONG64 d3d8_ctx_ticks;
extern UINT   d3d8_ctx_calls;

static __inline LONG64 d3d8_ctx_t0(void)
{
    LARGE_INTEGER q;
    if (!d3d8_ctx_timing)
        return 0;
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}

static __inline void d3d8_ctx_t1(LONG64 t0)
{
    LARGE_INTEGER q;
    if (!t0)
        return;
    QueryPerformanceCounter(&q);
    d3d8_ctx_ticks += q.QuadPart - t0;
    d3d8_ctx_calls++;
}

#ifndef __cplusplus
#define D3D8T_VOID(name, params, args) \
    static __inline void d3d8t_##name params { LONG64 t0_ = d3d8_ctx_t0(); c->lpVtbl->name args; d3d8_ctx_t1(t0_); }
D3D8T_VOID(VSSetConstantBuffers, (ID3D11DeviceContext *c, UINT a, UINT n, ID3D11Buffer *const *b), (c, a, n, b))
D3D8T_VOID(PSSetShaderResources, (ID3D11DeviceContext *c, UINT a, UINT n, ID3D11ShaderResourceView *const *v),
           (c, a, n, v))
D3D8T_VOID(PSSetShader, (ID3D11DeviceContext *c, ID3D11PixelShader *s, ID3D11ClassInstance *const *ci, UINT n),
           (c, s, ci, n))
D3D8T_VOID(PSSetSamplers, (ID3D11DeviceContext *c, UINT a, UINT n, ID3D11SamplerState *const *s), (c, a, n, s))
D3D8T_VOID(VSSetShader, (ID3D11DeviceContext *c, ID3D11VertexShader *s, ID3D11ClassInstance *const *ci, UINT n),
           (c, s, ci, n))
D3D8T_VOID(DrawIndexed, (ID3D11DeviceContext *c, UINT n, UINT a, INT b), (c, n, a, b))
D3D8T_VOID(Draw, (ID3D11DeviceContext *c, UINT n, UINT a), (c, n, a))
D3D8T_VOID(Unmap, (ID3D11DeviceContext *c, ID3D11Resource *r, UINT s), (c, r, s))
D3D8T_VOID(PSSetConstantBuffers, (ID3D11DeviceContext *c, UINT a, UINT n, ID3D11Buffer *const *b), (c, a, n, b))
D3D8T_VOID(IASetInputLayout, (ID3D11DeviceContext *c, ID3D11InputLayout *l), (c, l))
D3D8T_VOID(IASetVertexBuffers, (ID3D11DeviceContext *c, UINT a, UINT n, ID3D11Buffer *const *b, const UINT *st,
                                const UINT *o), (c, a, n, b, st, o))
D3D8T_VOID(IASetIndexBuffer, (ID3D11DeviceContext *c, ID3D11Buffer *b, DXGI_FORMAT f, UINT o), (c, b, f, o))
D3D8T_VOID(IASetPrimitiveTopology, (ID3D11DeviceContext *c, D3D11_PRIMITIVE_TOPOLOGY t), (c, t))
D3D8T_VOID(OMSetRenderTargets, (ID3D11DeviceContext *c, UINT n, ID3D11RenderTargetView *const *v,
                                ID3D11DepthStencilView *d), (c, n, v, d))
D3D8T_VOID(OMSetBlendState, (ID3D11DeviceContext *c, ID3D11BlendState *b, const FLOAT f[4], UINT m), (c, b, f, m))
D3D8T_VOID(OMSetDepthStencilState, (ID3D11DeviceContext *c, ID3D11DepthStencilState *d, UINT r), (c, d, r))
D3D8T_VOID(RSSetState, (ID3D11DeviceContext *c, ID3D11RasterizerState *r), (c, r))
D3D8T_VOID(RSSetViewports, (ID3D11DeviceContext *c, UINT n, const D3D11_VIEWPORT *v), (c, n, v))
D3D8T_VOID(CopySubresourceRegion, (ID3D11DeviceContext *c, ID3D11Resource *d, UINT ds, UINT x, UINT y, UINT z,
                                   ID3D11Resource *s, UINT ss, const D3D11_BOX *b), (c, d, ds, x, y, z, s, ss, b))
D3D8T_VOID(CopyResource, (ID3D11DeviceContext *c, ID3D11Resource *d, ID3D11Resource *s), (c, d, s))
D3D8T_VOID(UpdateSubresource, (ID3D11DeviceContext *c, ID3D11Resource *d, UINT ds, const D3D11_BOX *b,
                               const void *p, UINT rp, UINT dp), (c, d, ds, b, p, rp, dp))
D3D8T_VOID(ClearRenderTargetView, (ID3D11DeviceContext *c, ID3D11RenderTargetView *v, const FLOAT k[4]), (c, v, k))
D3D8T_VOID(ClearDepthStencilView, (ID3D11DeviceContext *c, ID3D11DepthStencilView *v, UINT f, FLOAT d, UINT8 s),
           (c, v, f, d, s))
D3D8T_VOID(ResolveSubresource, (ID3D11DeviceContext *c, ID3D11Resource *d, UINT ds, ID3D11Resource *s, UINT ss,
                                DXGI_FORMAT f), (c, d, ds, s, ss, f))
#undef D3D8T_VOID

static __inline HRESULT d3d8t_Map(ID3D11DeviceContext *c, ID3D11Resource *r, UINT s, D3D11_MAP t, UINT f,
                                  D3D11_MAPPED_SUBRESOURCE *m)
{
    LONG64 t0_ = d3d8_ctx_t0();
    HRESULT hr = c->lpVtbl->Map(c, r, s, t, f, m);
    d3d8_ctx_t1(t0_);
    return hr;
}

#undef ID3D11DeviceContext_VSSetConstantBuffers
#undef ID3D11DeviceContext_PSSetShaderResources
#undef ID3D11DeviceContext_PSSetShader
#undef ID3D11DeviceContext_PSSetSamplers
#undef ID3D11DeviceContext_VSSetShader
#undef ID3D11DeviceContext_DrawIndexed
#undef ID3D11DeviceContext_Draw
#undef ID3D11DeviceContext_Map
#undef ID3D11DeviceContext_Unmap
#undef ID3D11DeviceContext_PSSetConstantBuffers
#undef ID3D11DeviceContext_IASetInputLayout
#undef ID3D11DeviceContext_IASetVertexBuffers
#undef ID3D11DeviceContext_IASetIndexBuffer
#undef ID3D11DeviceContext_IASetPrimitiveTopology
#undef ID3D11DeviceContext_OMSetRenderTargets
#undef ID3D11DeviceContext_OMSetBlendState
#undef ID3D11DeviceContext_OMSetDepthStencilState
#undef ID3D11DeviceContext_RSSetState
#undef ID3D11DeviceContext_RSSetViewports
#undef ID3D11DeviceContext_CopySubresourceRegion
#undef ID3D11DeviceContext_CopyResource
#undef ID3D11DeviceContext_UpdateSubresource
#undef ID3D11DeviceContext_ClearRenderTargetView
#undef ID3D11DeviceContext_ClearDepthStencilView
#undef ID3D11DeviceContext_ResolveSubresource
#define ID3D11DeviceContext_VSSetConstantBuffers(c, a, n, b) d3d8t_VSSetConstantBuffers(c, a, n, b)
#define ID3D11DeviceContext_PSSetShaderResources(c, a, n, v) d3d8t_PSSetShaderResources(c, a, n, v)
#define ID3D11DeviceContext_PSSetShader(c, s, ci, n) d3d8t_PSSetShader(c, s, ci, n)
#define ID3D11DeviceContext_PSSetSamplers(c, a, n, s) d3d8t_PSSetSamplers(c, a, n, s)
#define ID3D11DeviceContext_VSSetShader(c, s, ci, n) d3d8t_VSSetShader(c, s, ci, n)
#define ID3D11DeviceContext_DrawIndexed(c, n, a, b) d3d8t_DrawIndexed(c, n, a, b)
#define ID3D11DeviceContext_Draw(c, n, a) d3d8t_Draw(c, n, a)
#define ID3D11DeviceContext_Map(c, r, s, t, f, m) d3d8t_Map(c, r, s, t, f, m)
#define ID3D11DeviceContext_Unmap(c, r, s) d3d8t_Unmap(c, r, s)
#define ID3D11DeviceContext_PSSetConstantBuffers(c, a, n, b) d3d8t_PSSetConstantBuffers(c, a, n, b)
#define ID3D11DeviceContext_IASetInputLayout(c, l) d3d8t_IASetInputLayout(c, l)
#define ID3D11DeviceContext_IASetVertexBuffers(c, a, n, b, st, o) d3d8t_IASetVertexBuffers(c, a, n, b, st, o)
#define ID3D11DeviceContext_IASetIndexBuffer(c, b, f, o) d3d8t_IASetIndexBuffer(c, b, f, o)
#define ID3D11DeviceContext_IASetPrimitiveTopology(c, t) d3d8t_IASetPrimitiveTopology(c, t)
#define ID3D11DeviceContext_OMSetRenderTargets(c, n, v, d) d3d8t_OMSetRenderTargets(c, n, v, d)
#define ID3D11DeviceContext_OMSetBlendState(c, b, f, m) d3d8t_OMSetBlendState(c, b, f, m)
#define ID3D11DeviceContext_OMSetDepthStencilState(c, d, r) d3d8t_OMSetDepthStencilState(c, d, r)
#define ID3D11DeviceContext_RSSetState(c, r) d3d8t_RSSetState(c, r)
#define ID3D11DeviceContext_RSSetViewports(c, n, v) d3d8t_RSSetViewports(c, n, v)
#define ID3D11DeviceContext_CopySubresourceRegion(c, d, ds, x, y, z, s, ss, b) \
    d3d8t_CopySubresourceRegion(c, d, ds, x, y, z, s, ss, b)
#define ID3D11DeviceContext_CopyResource(c, d, s) d3d8t_CopyResource(c, d, s)
#define ID3D11DeviceContext_UpdateSubresource(c, d, ds, b, p, rp, dp) d3d8t_UpdateSubresource(c, d, ds, b, p, rp, dp)
#define ID3D11DeviceContext_ClearRenderTargetView(c, v, k) d3d8t_ClearRenderTargetView(c, v, k)
#define ID3D11DeviceContext_ClearDepthStencilView(c, v, f, d, s) d3d8t_ClearDepthStencilView(c, v, f, d, s)
#define ID3D11DeviceContext_ResolveSubresource(c, d, ds, s, ss, f) d3d8t_ResolveSubresource(c, d, ds, s, ss, f)
#endif /* !__cplusplus */

/* ================================================================
 * D3D11 device accessors (implemented in d3d8_device.c)
 * ================================================================ */

IDirect3DDevice8    *d3d8_GetDevice(void);
ID3D11Device        *d3d8_GetD3D11Device(void);
ID3D11DeviceContext *d3d8_GetD3D11Context(void);
IDXGISwapChain      *d3d8_GetSwapChain(void);
ID3D11RenderTargetView *d3d8_GetDefaultRTV(void);
HWND                 d3d8_GetHWND(void);
UINT                 d3d8_GetBackbufferWidth(void);
UINT                 d3d8_GetBackbufferHeight(void);
/* Local (twoc step 97): the adapter the next CreateDevice is made on (kept
 * with a reference); NULL, also when never called, is D3D11's default
 * adapter as before. */
void                 d3d8_SetPreferredAdapter(IDXGIAdapter *adapter);

/* Presentation-only gamma. End restores the saved backbuffer even if Present fails. */
void d3d8_gamma_set(const D3DGAMMARAMP *ramp);
void d3d8_gamma_get(D3DGAMMARAMP *ramp);
HRESULT d3d8_gamma_begin(ID3D11RenderTargetView *target);
void d3d8_gamma_end(ID3D11RenderTargetView *target);
void d3d8_gamma_shutdown(void);

/* Current render state array accessor */
const DWORD         *d3d8_GetRenderStates(void);
const DWORD         *d3d8_GetTSS(DWORD stage);

/* Base texture currently bound to a texture stage (2D/cube/volume),
 * or NULL if nothing is bound. */
IDirect3DBaseTexture8 *d3d8_GetStageTexture(DWORD stage);

/* Transform accessors */
const D3DMATRIX     *d3d8_GetTransform(D3DTRANSFORMSTATETYPE type);

/* Lighting accessors (d3d8_device.c) */
const D3DLIGHT8     *d3d8_GetLight(DWORD index);
BOOL                 d3d8_GetLightEnable(DWORD index);
const D3DMATERIAL8  *d3d8_GetMaterial(void);
UINT                 d3d8_GetNumLights(void);
DWORD                d3d8_GetCurrentFVF(void);

/* ================================================================
 * Resource wrapper structures
 * ================================================================ */

typedef struct D3D8VertexBuffer {
    IDirect3DVertexBuffer8  iface;      /* COM interface (must be first) */
    LONG                    ref_count;
    ID3D11Buffer           *d3d11_buffer;
    UINT                    size;
    DWORD                   fvf;
    DWORD                   usage;
    BYTE                   *sys_mem;    /* System memory for Lock */
    BOOL                    locked;
    BOOL                    dirty;
} D3D8VertexBuffer;

typedef struct D3D8IndexBuffer {
    IDirect3DIndexBuffer8   iface;
    LONG                    ref_count;
    ID3D11Buffer           *d3d11_buffer;
    UINT                    size;
    D3DFORMAT               format;     /* INDEX16 or INDEX32 */
    DWORD                   usage;
    BYTE                   *sys_mem;
    BOOL                    locked;
    BOOL                    dirty;
} D3D8IndexBuffer;

typedef struct D3D8Texture {
    IDirect3DTexture8       iface;
    LONG                    ref_count;
    ID3D11Texture2D        *d3d11_texture;
    ID3D11ShaderResourceView *srv;
    UINT                    width;
    UINT                    height;
    UINT                    levels;
    D3DFORMAT               d3d8_format;
    DXGI_FORMAT             dxgi_format;
    DWORD                   usage;
    BYTE                   *sys_mem;    /* All mip levels, back to back (level 0 first) */
    UINT                    pitch;      /* Row pitch of level 0 */
    BOOL                    locked;
    BOOL                    dirty;
    UINT                    palette;    /* Palette index (texture stage) this P8 texture bakes */
} D3D8Texture;

typedef struct D3D8Surface {
    IDirect3DSurface8       iface;
    LONG                    ref_count;
    ID3D11Texture2D        *d3d11_texture;
    ID3D11RenderTargetView *rtv;
    ID3D11DepthStencilView *dsv;
    UINT                    width;
    UINT                    height;
    D3DFORMAT               format;
    D3DPOOL                 pool;
    DWORD                   usage;

    /* D3D11 subresource this surface aliases (array*MipLevels+mip).
     * 0 for a standalone offscreen surface. */
    UINT                    subresource;

    /* Multisample state (requested Xbox type + resolved D3D11 count). */
    D3DMULTISAMPLE_TYPE     multsample_type;
    UINT                    sample_count;

    /* Surface LockRect readback (staging round-trip). */
    ID3D11Texture2D        *staging;
    BYTE                   *locked_bits;
    INT                     locked_pitch;
    UINT                    lock_x;
    UINT                    lock_y;
    BOOL                    locked;
    BOOL                    lock_readonly;

    /* P8 (palettized) surfaces: raw index data lives in the parent
     * texture's sys_mem. LockRect returns the indices directly instead
     * of the palette-expanded BGRA that the D3D11 texture holds. */
    BOOL                    palettized;
    const BYTE             *palette_sys;  /* raw level data (1 byte/texel) */
    UINT                    palette_pitch;
    UINT                    palette_index; /* stage palette baked into it */
} D3D8Surface;

/* 2D texture (D3D11 Texture2D). See tex_* implementation.
 *
 * D3D8CubeTexture/D3D8VolumeTexture intentionally mirror the field
 * layout of D3D8Texture up to and including the `srv` member (a
 * "layout overlay"), so dev_SetTexture can fetch the SRV of any
 * bound base texture through the D3D8Texture offset. */
typedef struct D3D8CubeTexture {
    IDirect3DCubeTexture8   iface;
    LONG                    ref_count;
    ID3D11Texture2D        *d3d11_texture;
    ID3D11ShaderResourceView *srv;
    UINT                    width;      /* edge length */
    UINT                    height;     /* == width (cube faces are square) */
    UINT                    levels;
    D3DFORMAT               d3d8_format;
    DXGI_FORMAT             dxgi_format;
    DWORD                   usage;
    BYTE                   *sys_mem;    /* 6 faces * mip chain per face */
    UINT                    pitch;      /* row pitch of face level 0 */
    BOOL                    locked;
    BOOL                    dirty;
    UINT                    palette;    /* Palette index (texture stage) this P8 texture bakes */
} D3D8CubeTexture;

/* 3D texture (D3D11 Texture3D). The leading fields mirror D3D8CubeTexture
 * up to `srv` (layout overlay) when read through the D3D8Texture prefix.
 * Note `depth` is placed AFTER `levels` so width/height/levels stay at the
 * same offsets as the 2D types. */
typedef struct D3D8VolumeTexture {
    IDirect3DVolumeTexture8 iface;
    LONG                    ref_count;
    ID3D11Texture3D        *d3d11_texture;
    ID3D11ShaderResourceView *srv;
    UINT                    width;
    UINT                    height;
    UINT                    levels;
    UINT                    depth;
    D3DFORMAT               d3d8_format;
    DXGI_FORMAT             dxgi_format;
    DWORD                   usage;
    BYTE                   *sys_mem;    /* all levels, back to back */
    UINT                    pitch;      /* row pitch of level 0 */
    BOOL                    locked;
    BOOL                    dirty;
    UINT                    palette;    /* Palette index this P8 texture bakes */
} D3D8VolumeTexture;

/* A single volume level handed out by GetVolumeLevel(). */
typedef struct D3D8Volume {
    IDirect3DVolume8        iface;
    LONG                    ref_count;
    IDirect3DVolumeTexture8 *parent;    /* owning texture (AddRef'd) */
    UINT                    level;
} D3D8Volume;

/* ================================================================
 * Format conversion (d3d8_resources.c)
 * ================================================================ */

DXGI_FORMAT d3d8_to_dxgi_format(D3DFORMAT fmt);
UINT        d3d8_format_bpp(D3DFORMAT fmt);
BOOL        d3d8_format_is_compressed(D3DFORMAT fmt);
UINT        d3d8_row_pitch(D3DFORMAT fmt, UINT width);

/* Is the Xbox format a depth/stencil format? */
BOOL d3d8_format_is_depth(D3DFORMAT fmt);

/* bpp of the data actually uploaded to D3D11 (post-conversion). */
UINT d3d8_upload_bpp(D3DFORMAT fmt);

/* Does this format need software conversion at upload time? */
BOOL d3d8_format_has_conversion(D3DFORMAT fmt);

/* Is this a palettized (P8) format? */
BOOL d3d8_format_is_palettized(D3DFORMAT fmt);

/* Convert one linear (unswizzled) row of pixels from the Xbox layout
 * to the layout expected by the D3D11 texture. dst holds
 * d3d8_upload_bpp()/8 * width bytes per row. src is the raw
 * d3d8_format_bpp()/8 * width pitch. `palette` is the palette index
 * (texture stage) P8 texels are expanded through. */
void d3d8_convert_linear_pixels(D3DFORMAT fmt, UINT width, UINT height,
                                const BYTE *src, BYTE *dst, UINT palette);

/* Surface implementation (d3d8_resources.c) */
IDirect3DSurface8 *d3d8_surface_create(ID3D11Texture2D *texture,
                                       UINT mip_slice,
                                       UINT array_slice,
                                       UINT width, UINT height,
                                       D3DFORMAT fmt, D3DPOOL pool,
                                       DWORD usage,
                                       D3DMULTISAMPLE_TYPE multsample_type,
                                       const BYTE *raw_level_data,
                                       UINT palette_index);

/* Wrap a raw D3D11 texture (back buffer, etc.) as a surface. */
IDirect3DSurface8 *xbox_d3d8_surface_wrap(ID3D11Texture2D *texture,
                                          UINT width, UINT height,
                                          D3DFORMAT fmt);

HRESULT d3d8_CreateImageSurfaceImpl(UINT Width, UINT Height, D3DFORMAT Format,
                                    IDirect3DSurface8 **ppSurface);

/* Fetch the pixel-shader SRV of any bound base texture (2D, cube or
 * volume). All three implementations keep the SRV at the same offset
 * as D3D8Texture. */
ID3D11ShaderResourceView *d3d8_base_srv(IDirect3DBaseTexture8 *texture);

/* Read the D3DFORMAT of any base texture. */
D3DFORMAT d3d8_base_format(IDirect3DBaseTexture8 *texture);
void      d3d8_base_set_palette(IDirect3DBaseTexture8 *texture, UINT palette);

/* Re-upload every level of a P8 (palettized) texture through its
 * current stage palette. No-op for non-palettized textures. */
void d3d8_refresh_palette(IDirect3DBaseTexture8 *texture);

/* Cube/volume texture implementations (d3d8_resources.c) */
HRESULT d3d8_CreateCubeTextureImpl(UINT EdgeLength, UINT Levels, DWORD Usage,
                                   D3DFORMAT Format, IDirect3DCubeTexture8 **ppTex);
HRESULT d3d8_CreateVolumeTextureImpl(UINT Width, UINT Height, UINT Depth,
                                     UINT Levels, DWORD Usage, D3DFORMAT Format,
                                     IDirect3DVolumeTexture8 **ppTex);

/* ================================================================
 * Palette management (d3d8_device.c)
 * ================================================================ */

#define D3D8_MAX_PALETTES 4
void            d3d8_SetPalette(DWORD stage, const DWORD *entries);
const DWORD    *d3d8_GetPalette(DWORD stage);

/* Resource creation (d3d8_resources.c) */
HRESULT d3d8_CreateVertexBufferImpl(UINT Length, DWORD Usage, DWORD FVF, IDirect3DVertexBuffer8 **ppVB);
HRESULT d3d8_CreateIndexBufferImpl(UINT Length, DWORD Usage, D3DFORMAT Format, IDirect3DIndexBuffer8 **ppIB);
HRESULT d3d8_CreateTextureImpl(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, IDirect3DTexture8 **ppTex);

/* ================================================================
 * Shader management (d3d8_shaders.c)
 * ================================================================ */

HRESULT d3d8_shaders_init(void);
void    d3d8_shaders_shutdown(void);

/* Select programmable/FVF vertex state and refresh fixed-function pixel state. */
void    d3d8_shaders_prepare_draw(DWORD handle);

/* ================================================================
 * NV2A Register Combiner pixel shaders (d3d8_combiners.c)
 * ================================================================ */

#include "d3d8_combiners.h"

/* ================================================================
 * NV2A Programmable Vertex Shaders (d3d8_vsh.c)
 * ================================================================ */

#include "d3d8_vsh.h"

/* ================================================================
 * Render state management (d3d8_states.c)
 * ================================================================ */

HRESULT d3d8_states_init(void);
void    d3d8_states_shutdown(void);

/* Apply current D3D8 render states as D3D11 state objects */
void    d3d8_states_apply(void);

/* Create sampler state from TSS and apply to slot */
void    d3d8_states_apply_sampler(DWORD stage);

/* ================================================================
 * What the draws last bound on the D3D11 context (d3d8_states.c)
 *
 * Local perf change (twoc notes 18.79): a draw that would bind the object
 * already bound skips the call. Code outside xbox_d3d8 that binds on the
 * same context puts back what it found (twoc's present passes, Dear ImGui's
 * backend), so the record stays true; d3d8_bound_reset forgets it (every
 * next bind is made).
 * ================================================================ */

typedef struct {
    ID3D11VertexShader      *vs;
    ID3D11PixelShader       *ps;
    ID3D11InputLayout       *il;
    ID3D11BlendState        *bs;
    ID3D11DepthStencilState *dss;
    UINT                     stencil_ref;
    ID3D11RasterizerState   *rs;
    ID3D11SamplerState      *samp[4];
    ID3D11Buffer            *vs_cb[2];
    UINT                     vs_cb1_first;  /* slot 1's first constant (twoc notes 18.90: the constants ring) */
    ID3D11Buffer            *ps_cb;
    ID3D11Buffer            *vb;        /* IA slot 0 */
    UINT                     vb_stride, vb_offset;
    ID3D11Buffer            *ib;
    DXGI_FORMAT              ib_format;
    UINT                     ib_offset;
    D3D11_PRIMITIVE_TOPOLOGY topology;
} D3D8Bound;

extern D3D8Bound g_d3d8_bound;
void d3d8_bound_reset(void);

static __inline void d3d8_bind_vs(ID3D11DeviceContext *ctx, ID3D11VertexShader *vs)
{
    if (g_d3d8_bound.vs != vs) {
        g_d3d8_bound.vs = vs;
        ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
    }
}

static __inline void d3d8_bind_ps(ID3D11DeviceContext *ctx, ID3D11PixelShader *ps)
{
    if (g_d3d8_bound.ps != ps) {
        g_d3d8_bound.ps = ps;
        ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    }
}

static __inline void d3d8_bind_layout(ID3D11DeviceContext *ctx, ID3D11InputLayout *il)
{
    if (g_d3d8_bound.il != il) {
        g_d3d8_bound.il = il;
        ID3D11DeviceContext_IASetInputLayout(ctx, il);
    }
}

static __inline void d3d8_bind_vs_cb(ID3D11DeviceContext *ctx, UINT slot, ID3D11Buffer *cb)
{
    if (g_d3d8_bound.vs_cb[slot] != cb || (slot == 1 && g_d3d8_bound.vs_cb1_first)) {
        g_d3d8_bound.vs_cb[slot] = cb;
        if (slot == 1)
            g_d3d8_bound.vs_cb1_first = 0;
        ID3D11DeviceContext_VSSetConstantBuffers(ctx, slot, 1, &cb);
    }
}

static __inline void d3d8_bind_ps_cb(ID3D11DeviceContext *ctx, ID3D11Buffer *cb)
{
    if (g_d3d8_bound.ps_cb != cb) {
        g_d3d8_bound.ps_cb = cb;
        ID3D11DeviceContext_PSSetConstantBuffers(ctx, 0, 1, &cb);
    }
}

static __inline void d3d8_bind_vb(ID3D11DeviceContext *ctx, ID3D11Buffer *vb, UINT stride, UINT offset)
{
    if (g_d3d8_bound.vb != vb || g_d3d8_bound.vb_stride != stride || g_d3d8_bound.vb_offset != offset) {
        g_d3d8_bound.vb = vb;
        g_d3d8_bound.vb_stride = stride;
        g_d3d8_bound.vb_offset = offset;
        ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vb, &stride, &offset);
    }
}

static __inline void d3d8_bind_ib(ID3D11DeviceContext *ctx, ID3D11Buffer *ib, DXGI_FORMAT format, UINT offset)
{
    if (g_d3d8_bound.ib != ib || g_d3d8_bound.ib_format != format || g_d3d8_bound.ib_offset != offset) {
        g_d3d8_bound.ib = ib;
        g_d3d8_bound.ib_format = format;
        g_d3d8_bound.ib_offset = offset;
        ID3D11DeviceContext_IASetIndexBuffer(ctx, ib, format, offset);
    }
}

static __inline void d3d8_bind_topology(ID3D11DeviceContext *ctx, D3D11_PRIMITIVE_TOPOLOGY topology)
{
    if (g_d3d8_bound.topology != topology) {
        g_d3d8_bound.topology = topology;
        ID3D11DeviceContext_IASetPrimitiveTopology(ctx, topology);
    }
}

/* ================================================================
 * UP draws drawn again (d3d8_device.c)
 *
 * Local perf change (twoc notes 18.79): DrawPrimitiveUP and
 * DrawIndexedPrimitiveUP, for a caller that draws the same draw several
 * times with its data unchanged. `kept` starts zeroed; the first draw
 * uploads into the rings and notes where, and later draws use that while
 * the rings still hold it (until either starts over).
 * ================================================================ */

typedef struct {
    UINT gen;                           /* the rings' generation then, 0 none */
    UINT vb_offset, ib_offset;
} D3D8UpKept;

HRESULT d3d8_DrawPrimitiveUPKept(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount,
                                 const void *pVertexData, UINT VertexStreamZeroStride, D3D8UpKept *kept);
HRESULT d3d8_DrawIndexedPrimitiveUPKept(D3DPRIMITIVETYPE PrimitiveType, UINT NumVertices, UINT PrimitiveCount,
                                        const void *pIndexData, D3DFORMAT IndexDataFormat,
                                        const void *pVertexData, UINT VertexStreamZeroStride, D3D8UpKept *kept);

/* ================================================================
 * Baked draws (d3d8_device.c)
 *
 * Local perf change (twoc notes 18.90): a caller that draws the same UP
 * draw several times (twoc's sub-frames) has the first draw captured: what
 * it bound on the D3D11 context once its render states, stage states,
 * shaders and constants were translated (the pipeline objects, samplers,
 * viewport, the fixed-function and pixel constants as uploaded, where the
 * rings hold its vertices). d3d8_DrawBaked draws it again from that, binding
 * what differs from what is bound, with only the vertex program's
 * constants (or the fixed-function matrices) given again; nothing is
 * translated and xbox_d3d8's own D3D8 state is left as it was. With D3D11.1's
 * constant-buffer offsets a captured program's registers go into a ring
 * (d3d8_vsh.c), where a baked draw with them unchanged binds them again; and
 * d3d8_BakeReset starts the rings over when the frame may not fit. The objects
 * a capture points to stay alive while d3d8_bake_gen is unchanged: every
 * cache that releases one bumps it (and a bake from before is refused).
 * ================================================================ */

typedef struct {
    UINT gen;                           /* d3d8_bake_gen when captured, 0: not captured */
    UINT ring_gen;                      /* the rings' generation then */
    ID3D11VertexShader      *vs;
    ID3D11InputLayout       *il;
    ID3D11PixelShader       *ps;
    ID3D11BlendState        *bs;
    ID3D11DepthStencilState *dss;
    ID3D11RasterizerState   *rs;
    ID3D11SamplerState      *samp[4];
    ID3D11Buffer            *vs_cb[2], *ps_cb;
    UINT                     stencil_ref;
    D3D11_VIEWPORT           vp;
    D3D11_PRIMITIVE_TOPOLOGY topology;
    UINT                     stride;
    DXGI_FORMAT              ib_format;         /* DXGI_FORMAT_UNKNOWN: not indexed */
    UINT                     count, start;      /* vertices or indices, the first */
    INT                      base;              /* the base vertex (indexed) */
    BOOL                     vsh;               /* a vertex program (its constants in b1) */
    UINT                     k_gen, k_first;    /* where the constants ring holds them (k_gen 0: not there) */
    const void              *ff_xf, *ff_lt;     /* fixed-function: its transform and lighting constants */
    const void              *ps_consts;         /* the pixel constants, as in ps_cb */
    UINT                     ps_bytes;
} D3D8Baked;

extern UINT d3d8_bake_gen;

/* The captures of a new frame begin: the memory the last frame's held is
 * reused. */
void d3d8_BakeReset(void);
/* The next UP draw is captured into `b` (gen set when it is); NULL: none. */
void d3d8_BakeCapture(D3D8Baked *b);
/* `b` drawn again with stage s sampling srv[s]. A vertex program's
 * constants are `vs_blk` (192 float4s as 12 blocks of 16, twoc step 95:
 * vs_blk[i] holds c16i-c16i+15); `fresh` says they are not the blocks given
 * at the last baked draw unchanged (or `ff_m` world, view and projection
 * replace a fixed-function draw's matrices, NULL: as captured). Returns
 * S_FALSE without drawing when the capture no longer holds (draw it the
 * ordinary way). */
HRESULT d3d8_DrawBaked(const D3D8Baked *b, ID3D11ShaderResourceView *const srv[4], const float *const *vs_blk,
                       BOOL fresh, const D3DMATRIX *const ff_m[3]);

/* Twoc step 95 (cut 3): `old`, a capture made in the game frame before (the
 * bake's memory holds the last two frames'), of a draw whose translation
 * the caller knows to be this one's (the same render and stage states,
 * shaders, lights and material, viewport, target, primitive and stride),
 * drawn again with this draw's own vertices and indices (put in the rings
 * as DrawPrimitiveUP / DrawIndexedPrimitiveUP put them, `kept` as theirs:
 * `idx` 16-bit, NULL not indexed; `nverts` the vertices at `verts`), its
 * program constants as SetVertexShaderConstant left them (the caller sets
 * them first, as for an ordinary draw) or its fixed-function matrices
 * (`ff_m`), and stage s sampling srv[s]; and captured into `now` for this
 * frame's later passes. Returns S_FALSE without drawing when `old` no
 * longer holds (draw it the ordinary way). */
HRESULT d3d8_DrawBakedAgain(const D3D8Baked *old, D3D8Baked *now, ID3D11ShaderResourceView *const srv[4],
                            const D3DMATRIX *const ff_m[3], D3DPRIMITIVETYPE pt, UINT prim_count, UINT nverts,
                            const void *idx, const void *verts, UINT stride, D3D8UpKept *kept);

/* The modules' parts (d3d8_shaders.c, d3d8_combiners.c, d3d8_vsh.c) */
void *d3d8_bake_alloc(size_t n);
int d3d8_shaders_bake_again(D3D8Baked *b, const D3DMATRIX *const ff_m[3]);
const void *d3d8_combiners_bake_again(const void *old);
void d3d8_vsh_bake_again(D3D8Baked *b);
void d3d8_shaders_bake(D3D8Baked *b);
void d3d8_shaders_baked_upload(const D3D8Baked *b, const D3DMATRIX *const ff_m[3]);
void d3d8_shaders_bake_reset(void);
const void *d3d8_combiners_bake(UINT *bytes);
void d3d8_combiners_baked_upload(const void *consts);
void d3d8_combiners_bake_reset(void);
void d3d8_vsh_baked_consts(const D3D8Baked *b, const float *const *blk, BOOL fresh);
void d3d8_vsh_bake_reset(void);
void d3d8_vsh_bake(D3D8Baked *b);
BOOL d3d8_bake_capturing(void);

#endif /* _WIN32 -- end of D3D11 backend section */

#endif /* BURNOUT3_D3D8_INTERNAL_H */
