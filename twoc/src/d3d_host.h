/**
 * Host side of the D3D mirror bridge: the xbox_d3d8 device, its window, and
 * the one lock around every host call. See d3d_host.c. Called only from
 * d3d_bridge.c. No guest address is used here: d3d_bridge.c resolves guest
 * memory (vertices, texture bytes) to host pointers and passes those, and
 * nothing is ever written through them.
 */
#ifndef TWOC_D3D_HOST_H
#define TWOC_D3D_HOST_H

#include <stdint.h>

typedef struct {
    unsigned presents, present_fails, present_status;   /* status: succeeded but not S_OK */
    unsigned clears, clears_colour, clears_depth, clears_partial_colour;
    unsigned dumps;
    unsigned draws, draw_fails;
    unsigned tex_uploads, tex_upload_fails, tex_evictions;
    unsigned rt_created, rt_fails, rt_draws, rt_clears, rt_sampled, rt_feedback;  /* S4 */
    unsigned p8_uploads, ps_draws, ps_probes;                                     /* S4 */
    unsigned cube_uploads;                                                        /* S5 part 2 */
    unsigned indexed_draws, ff_draws, lit_draws;                                  /* fixed-function 3D */
    unsigned copies, copy_fails;                                                  /* CopyRects (step 35) */
    unsigned vp_draws, vs_created, vs_fails, vs_const_uploads, vs_probes;         /* vertex programs (S5) */
    unsigned paced, paced_late;                                                   /* frame limiter (S7) */
    double paced_wait_ms;
    unsigned shown[9];      /* DWM compositions between presents: [0]..[7], [8] more (18.45) */
    unsigned displayed[9];  /* refreshes between consecutive displayed frames: [0]..[7], [8] more (18.48) */
    unsigned display_unmeasured;    /* frame statistics unavailable, or more than one frame shown between two reads */
} d3dh_stats_t;

/* A state already in xbox_d3d8's (PC D3D8) numbering and values. */
typedef struct {
    uint32_t state, value;
} d3dh_state_t;

/* A guest texture, described by d3d_bridge.c: XDK format code, size, levels,
 * and a host pointer to its bytes laid out the XDK way (levels back to back;
 * `pitch` is the row pitch of a linear format, 0 for a packed one). `key` is
 * the guest header address, `data` its physical Data, and `sig` a content
 * signature: the host cache re-uploads when any of them changes. */
typedef struct {
    uint32_t key, data, format, width, height, levels, pitch;
    const uint8_t *bits;
    uint32_t bytes, sig;
    /* S4, P8 (format 0x0B) only: the stage's guest palette as 32-bit ARGB
     * entries, and its content signature (part of the cache key). The host
     * expands the indices through it to A8R8G8B8 at upload. */
    const uint32_t *palette;
    uint32_t pal_entries, pal_sig;
    /* S5 part 2: a cube map. Its six faces follow one another in `bits`,
     * each one a full mip chain `face_bytes` apart (padded to 128 bytes, as
     * the NV2A and xemu lay them out); `width` is the edge. */
    uint32_t cube, face_bytes;
} d3dh_tex_t;

/* S4: where a draw or clear goes. `data` 0 is the back buffer (with the
 * host's default depth buffer); otherwise the guest render target's physical
 * Data, which names one host render-target texture of that size and format
 * (level 0 only). A texture the guest binds with the same Data samples that
 * host texture instead of guest memory. */
typedef struct {
    uint32_t data, width, height, format;
} d3dh_target_t;

/* S4: a pixel shader already re-encoded into xbox_d3d8's combiner layout
 * (its D3DRS_PS* numbering 200-253 and its own field order, see
 * d3d_bridge.c translate_ps) and the token for its SetPixelShader. */
typedef struct {
    uint32_t token;                     /* 0: no pixel shader (fixed function) */
    d3dh_state_t rs[56];
    unsigned n_rs;
} d3dh_ps_t;

/* Fixed-function transform and lighting state (FVF draws with untransformed
 * positions), as D3D keeps it: row-major D3DMATRIX, D3DLIGHT8 (26 dwords)
 * and D3DMATERIAL8 (17 dwords), the same layouts in the XDK and PC D3D8. The
 * lights are the enabled ones, packed. */
#define D3DH_MAX_LIGHTS 8
typedef struct {
    float world[16], view[16], proj[16];
    uint32_t n_lights;
    uint32_t light[D3DH_MAX_LIGHTS][26];
    uint32_t material[17];
} d3dh_ff_t;

/* S5: a vertex program. `code` is the NV2A microcode (4 dwords an
 * instruction, `n_insns` of them) and `key` its hash, which names one host
 * shader. `consts` is the NV2A's constant memory as the draw sees it,
 * float4[192] by the microcode's own indices. A draw with one has its
 * vertices in the host's layout for it: a float4 for each register the
 * program reads, in ascending order (d3dh_vs_inputs). */
typedef struct {
    uint32_t key, n_insns;
    const uint32_t *code;
    const float *consts;
} d3dh_vprog_t;

/* One draw from client memory, translated by d3d_bridge.c. `prim` is
 * xbox_d3d8's primitive type and `prim_count` its primitive count. */
typedef struct {
    uint32_t prim, prim_count, stride, fvf;
    const void *verts;
    uint32_t vert_count;                /* vertices at `verts` */
    const uint16_t *indices;            /* NULL: not indexed; else prim_count's worth, into `verts` */
    uint32_t index_count;
    const d3dh_ff_t *ff;                /* NULL: pre-transformed vertices (XYZRHW) */
    const d3dh_state_t *rs;
    unsigned n_rs;
    const d3dh_state_t *tss[4];
    unsigned n_tss[4];
    const d3dh_tex_t *tex[4];           /* NULL: no texture on that stage */
    uint32_t vp_x, vp_y, vp_w, vp_h;
    float vp_minz, vp_maxz;
    d3dh_target_t target;
    const d3dh_ps_t *ps;                /* NULL: fixed function */
    const d3dh_vprog_t *vprog;          /* S5, NULL: the FVF `fvf` */
} d3dh_draw_t;

/* S5: the registers a program reads (bit n = vn), 0 if it reads none or
 * can't be parsed. */
uint16_t d3dh_vs_inputs(const d3dh_vprog_t *p);

/* S5, RECOMP_D3D_BRIDGE_VSPROBE: draw one vertex (in the program's layout,
 * `stride` bytes) as a point with this program, the draw's viewport and no
 * texture, into a host render target of the viewport's extent cleared to a
 * sentinel, and find the pixel it lit and its colour (oD0). Returns the
 * number of pixels lit (0: none, -1: failed); `px`, `py`, `argb` are the
 * first one's. */
int d3dh_vs_probe(const d3dh_vprog_t *p, const void *vert, uint32_t stride, const d3dh_draw_t *view, int *px,
                  int *py, uint32_t *argb);

/* Bytes a texture takes in guest memory, the XDK way (0: format not handled).
 * For a cube map this is one face; the faces are that rounded up to 128. */
uint32_t d3dh_texture_bytes(uint32_t format, uint32_t width, uint32_t height, uint32_t levels, uint32_t pitch);

/* Whether the host can upload this XDK format (P8 through its palette, S4). */
int d3dh_format_supported(uint32_t format);

/* Draw: DrawPrimitiveUP, or DrawIndexedPrimitiveUP with 16-bit indices when
 * `indices` is set. Returns 0 on success. */
int d3dh_draw_up(const d3dh_draw_t *d);

/* S4, RECOMP_D3D_BRIDGE_PSPROBE: draw one pixel with this pixel shader and
 * fixed inputs (the vertex colours and one 1x1 texture per stage given
 * here, point sampled, no blending, alpha test or fog) into a small host
 * render target and read it back as ARGB. Returns 0 on success. */
int d3dh_ps_probe(const d3dh_ps_t *ps, uint32_t diffuse, uint32_t specular, const uint32_t tex_argb[4],
                  uint32_t *out_argb);

/* S4, RECOMP_D3D_BRIDGE_TEXCHECK: upload a guest texture as a draw would
 * (through the cache, P8 through its palette) without drawing, and write its
 * level 0, alpha kept, as a 32-bit BMP at `path`. Returns 0 on success. */
int d3dh_texture_check(const d3dh_tex_t *t, const char *path);

/* Step 35, CopyRects from the back buffer to a texture level: the host's
 * back buffer is copied into a host render-target texture for `dst` (its
 * guest Data and size), which the guest texture with that Data then samples.
 * `rects` holds `n` guest RECTs (left, top, right, bottom) and `points` as
 * many destination POINTs (NULL: the rects' own corners); `n` 0 copies the
 * whole surface. Returns 0 on success. */
int d3dh_copy_back(const d3dh_target_t *dst, const int32_t *rects, const int32_t *points, unsigned n);

/* A guest lock at physical address `data`: drop cached textures that hold it. */
void d3dh_texture_touch(uint32_t data);
/* Dump the back buffer at each guest swap in [from, to] (needs a dump prefix). */
void d3dh_dump_range(unsigned from, unsigned to);

/* Create the window (on its own thread) and the host device. `interval` is
 * the DXGI sync interval for every present; `frame_hz`, if above 0, is the
 * frame limiter's rate (S7): each present waits for its slot on that clock.
 * Returns 0 on success. */
int  d3dh_create(unsigned width, unsigned height, unsigned interval, double frame_hz, const char *dump_prefix);
int  d3dh_ready(void);

/* Clear with XDK flags: TARGET 0xF0 (all four channel bits), ZBUFFER 1,
 * STENCIL 2. A partial colour mask isn't mirrored (counted instead). */
void d3dh_clear(const d3dh_target_t *target, uint32_t xdk_flags, uint32_t color, float z, uint32_t stencil);

/* End of a guest frame: dump the back buffer when one is due, then present. */
void d3dh_present(double t_s, unsigned swap_no);
double d3dh_display_hz(void);           /* the display's refresh rate, 0 if unknown (18.45) */

void d3dh_stats(d3dh_stats_t *out);

#endif
