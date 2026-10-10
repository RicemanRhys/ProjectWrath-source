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
    unsigned rt_released, rt_evicted;                                             /* step 52 */
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
    unsigned rhw_full, rhw_pillar;  /* 18.56: pre-transformed draws on a scaled screen, stretched / pillarboxed */
    unsigned copy_unscaled;         /* 18.56: copies into a target the host couldn't scale them to */
    unsigned aa_mode, ms_samples;   /* step 58: TWOC_AA_*, the back buffer's samples (0: single) */
    unsigned ms_targets, ms_fails, ms_resolves, ms_fills;   /* step 58: MSAA render targets and their traffic */
    unsigned post_aa, post_aa_fails;                        /* step 58: frames through FXAA or SMAA */
    double draw_ms;                 /* step 60: time drawing on the host (its draws: `draws`) */
    unsigned replay_frames, replay_cmds, replay_pins, rec_draws;    /* step 60: record and replay */
    double replay_ms, rec_ms, replay_bytes;
    unsigned replay_checked, replay_check_diff;     /* step 60: RECOMP_D3D_BRIDGE_REPLAY_CHECK */
    /* Step 61: frame interpolation. Frames drawn as sub-frames (`interp_k`
     * each, the last), and those held instead: a camera step or turn over the
     * limit, too few draws matched, no frame before, or another reason (the
     * overlay, a cost check). Draws redrawn per sub-frame by kind. */
    unsigned interp_k, interp_frames, interp_subs;
    unsigned interp_cut_step, interp_cut_turn, interp_cut_match, interp_cut_first, interp_off;
    unsigned interp_vp, interp_ff, interp_kept, interp_cpos;
    unsigned interp_other_cam;          /* step 65: 3D draws kept as recorded: another camera than the world's */
    /* Step 62: objects in interpolated frames (paired with the frame
     * before's, none of their key, or snapped: stepped over the limit), and
     * per sub-frame the program and fixed-function draws moved and the
     * skinned ones whose bones moved. */
    unsigned interp_obj, interp_obj_new, interp_obj_snap, interp_obj_vp, interp_obj_ff, interp_bones, interp_bone_snap;
    unsigned interp_obj_swap;           /* step 63: in the place of another kind of object (kept as drawn) */
    double interp_matched;              /* sum of each interpolated frame's fraction of draws matched */
    /* Step 65: the game's overlay pass: passes, its draws, those under the
     * world's camera (the HUD counters and title cards). */
    unsigned overlay_passes, overlay_draws, overlay_world_cam;
    /* ... the HUD's draws and, with the HUD at the edges, its elements
     * moved left, moved right and left in the middle. */
    unsigned hud_draws, hud_left, hud_right, hud_mid, hud_passes;
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
    int pillar;                         /* 18.56: a 3D draw with a 4:3 projection: its viewport pillarboxed */
    /* Step 61: the game's camera as the draw was made: its world matrix
     * (0x242620, rows right, up, forward, position) then its projection
     * (0x242398), 32 floats; NULL: unknown (not interpolated). */
    const float *camera;
    /* Step 62: the guest mesh the draw takes its vertices from (a hash of
     * its index and vertex addresses and count), 0 unknown: draws of one
     * object are matched between frames by it. */
    uint32_t mesh;
    /* Step 65: drawn in the game's overlay pass (the HUD counters, title
     * cards, the pause menu: d3d_bridge.c d3db_list_flush), and whether
     * under the camera the world pass had this frame (the HUD counters and
     * title cards; the pause menu has a camera of its own). `hud`: queued
     * by the panel's in-game part (the HUD counters and title cards, never
     * the pause menu or a hub pad's panel). */
    uint8_t overlay, world_cam, hud;
    /* Set by the host (step 65, HUD at the screen edges): the clip-space x
     * offset, in units of w, that moves the draw's element to its edge. */
    float hud_dx;
} d3dh_draw_t;

/* S5: the registers a program reads (bit n = vn), 0 if it reads none or
 * can't be parsed. */
uint16_t d3dh_vs_inputs(const d3dh_vprog_t *p);

/* Step 60: the constant registers the program reads directly (bit n of
 * mask[n / 32] = cn); returns the lowest base it reads relative to a0.x
 * (some register from there on), or -1 if none. */
int d3dh_vs_consts(const d3dh_vprog_t *p, uint32_t mask[6]);

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

/* Step 65: the game's overlay pass begins (1) or ends (0); its draws come
 * in between, with `overlay` set. */
void d3dh_overlay_pass(int begin);
/* Step 65: the HUD at the screen edges (1) or in the middle 4:3 (0, as the
 * game draws it) in a picture wider than 4:3, `margin` percent of the
 * picture's width in from the edge (0-10). From the next overlay pass. */
void d3dh_set_hud(int edges, int margin);

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
/* Step 52: the guest freed contiguous memory at physical [data, data + bytes):
 * release the render targets whose Data was in it. */
void d3dh_memory_freed(uint32_t data, uint32_t bytes);
/* Dump the back buffer at each guest swap in [from, to] (needs a dump prefix). */
void d3dh_dump_range(unsigned from, unsigned to);

/* Create the window (on its own thread) and the host device. `width` and
 * `height` are the guest's back buffer; `host_w` and `host_h` the host's
 * (18.56: the guest's at 4:3 unless RECOMP_WIDESCREEN or RECOMP_RENDER_SIZE
 * asks for more), and `wide` the host picture's width over height when it is
 * wider than the guest's (0: not). With `dpi_aware` the window is sized in
 * real pixels (a render size was asked for); otherwise Windows scales it, as
 * before. `interval` is the DXGI sync interval for every present;
 * `frame_hz`, if above 0, is the frame limiter's rate (S7): each present
 * waits for its slot on that clock. Returns 0 on success. */
int  d3dh_create(unsigned width, unsigned height, unsigned host_w, unsigned host_h, double wide, int dpi_aware,
                 unsigned interval, double frame_hz, const char *dump_prefix);
int  d3dh_ready(void);

/* Clear with XDK flags: TARGET 0xF0 (all four channel bits), ZBUFFER 1,
 * STENCIL 2. A partial colour mask isn't mirrored (counted instead). */
void d3dh_clear(const d3dh_target_t *target, uint32_t xdk_flags, uint32_t color, float z, uint32_t stencil);

/* End of a guest frame: dump the back buffer when one is due, then present. */
void d3dh_present(double t_s, unsigned swap_no);
double d3dh_display_hz(void);           /* the display's refresh rate, 0 if unknown (18.45) */

void d3dh_stats(d3dh_stats_t *out);

/* ── Step 54 (notes 18.57): the settings overlay and live settings ──
 *
 * Esc on the host window opens the overlay at the next present: that present
 * holds the game until it closes. Settings the host can't apply itself (the
 * aspect, the render size, the pacing: d3d_bridge.c decides them) go to the
 * callback set here, on the presenting thread, without the host's lock. */
#include "settings.h"
typedef void (*d3dh_apply_fn)(const twoc_settings_t *s);
void d3dh_set_apply(d3dh_apply_fn fn);
/* The host picture at a new size and aspect (`wide` as for d3dh_create).
 * From the first change on, a host render target of that size stands in for
 * xbox_d3d8's back buffer, whose size is fixed; screen-sized render targets
 * are made again at their next use. */
void d3dh_resize(unsigned host_w, unsigned host_h, double wide);
/* New pacing, as d3dh_create's `interval` and `frame_hz`. */
void d3dh_set_pacing(unsigned interval, double frame_hz);
/* Step 63: why the pacing about to be set (d3dh_create, d3dh_set_pacing)
 * isn't the display's vsync, for the log and the overlay when frame
 * interpolation can't run; "" or NULL when it is. */
void d3dh_pacing_note(const char *why);
/* The window's monitor's nominal refresh rate (its mode), 0 if unknown. */
double d3dh_monitor_hz(void);
/* Step 61: "Frame rate: display": interpolated sub-frames at each present
 * (on 1, off 0), from the next frame. */
void d3dh_set_framerate(int display);
/* For the controller poll (recomp_manual.c): 1 while the overlay is open;
 * *closes counts the times it has closed. Callable with the bridge off. */
int d3dh_overlay_input(unsigned *closes);

#endif
