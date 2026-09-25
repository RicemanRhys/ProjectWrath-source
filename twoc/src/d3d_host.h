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
} d3dh_tex_t;

/* One draw from client memory, translated by d3d_bridge.c. `prim` is
 * xbox_d3d8's primitive type and `prim_count` its primitive count. */
typedef struct {
    uint32_t prim, prim_count, stride, fvf;
    const void *verts;
    const d3dh_state_t *rs;
    unsigned n_rs;
    const d3dh_state_t *tss[4];
    unsigned n_tss[4];
    const d3dh_tex_t *tex[4];           /* NULL: no texture on that stage */
    uint32_t vp_x, vp_y, vp_w, vp_h;
    float vp_minz, vp_maxz;
} d3dh_draw_t;

/* Bytes a texture takes in guest memory, the XDK way (0: format not handled). */
uint32_t d3dh_texture_bytes(uint32_t format, uint32_t width, uint32_t height, uint32_t levels, uint32_t pitch);

/* Whether the host can upload this XDK format (no palette formats yet). */
int d3dh_format_supported(uint32_t format);

/* Draw. Returns 0 on success. */
int d3dh_draw_up(const d3dh_draw_t *d);

/* A guest lock at physical address `data`: drop cached textures that hold it. */
void d3dh_texture_touch(uint32_t data);

/* Create the window (on its own thread) and the host device. `interval` is
 * the DXGI sync interval for every present. Returns 0 on success. */
int  d3dh_create(unsigned width, unsigned height, unsigned interval, const char *dump_prefix);
int  d3dh_ready(void);

/* Clear with XDK flags: TARGET 0xF0 (all four channel bits), ZBUFFER 1,
 * STENCIL 2. A partial colour mask isn't mirrored (counted instead). */
void d3dh_clear(uint32_t xdk_flags, uint32_t color, float z, uint32_t stencil);

/* End of a guest frame: dump the back buffer when one is due, then present. */
void d3dh_present(double t_s, unsigned swap_no);

void d3dh_stats(d3dh_stats_t *out);

#endif
