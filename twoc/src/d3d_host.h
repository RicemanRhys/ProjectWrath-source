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
    unsigned tex_snaps, tex_at_replay;  /* step 78: recorded uploads (guest bytes copied); recorded draws' textures
                                         * fetched at the replay from guest memory (not a render target there) */
    double tex_snap_bytes;
    unsigned tex_at_copy;               /* step 79: ... with the render thread: drawn from their record-time copy */
    /* Step 79: the render thread: on now, frames it replayed, the game
     * thread's time waiting in its Swap (for its next slot, the overlay),
     * the render thread's time waiting for a frame (ms). */
    unsigned rt_on, rt_frames;
    double rt_wait_ms, rt_idle_ms;
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
    unsigned display_off;           /* step 74: of `displayed`, shown at another gap than its present's interval */
    unsigned disp_ms_n, disp_ms_even;   /* step 77: under the limiter, shown frames timed, and those shown */
    double disp_ms_dev;                 /* within 1 ms of the gap presented; the sum of |shown - presented| ms */
    unsigned rhw_full, rhw_pillar;  /* 18.56: pre-transformed draws on a scaled screen, stretched / pillarboxed */
    unsigned copy_unscaled;         /* 18.56: copies into a target the host couldn't scale them to */
    unsigned aa_mode, ms_samples;   /* step 58: TWOC_AA_*, the back buffer's samples (0: single) */
    unsigned ms_targets, ms_fails, ms_resolves, ms_fills;   /* step 58: MSAA render targets and their traffic */
    unsigned post_aa, post_aa_fails;                        /* step 58: frames through FXAA or SMAA */
    double draw_ms;                 /* step 60: time drawing on the host (its draws: `draws`) */
    unsigned replay_frames, replay_cmds, replay_pins, rec_draws;    /* step 60: record and replay */
    double replay_ms, rec_ms, replay_bytes;
    double pair_ms;                 /* step 68: time pairing objects (ic_pair) */
    unsigned replay_checked, replay_check_diff;     /* step 60: RECOMP_D3D_BRIDGE_REPLAY_CHECK */
    unsigned bake_captured, baked_draws, bake_plain;    /* step 87: first passes' captures, draws drawn from */
                                                        /* them, later passes' draws drawn the ordinary way */
    unsigned bake_again, bake_again_found;  /* step 95 (cut 3): first passes' draws drawn from the frame before's */
                                            /* capture; those whose state the frame before had (some refused) */
    unsigned bake_geom;                 /* first passes' draws drawn with the frame before's vertices and indices */
                                        /* where the rings held them (cut 3) */
    /* Step 95: the draws by kind (TWOC_DK_*): all of them, those whose D3D11
     * context calls were timed (one draw in 32), the calls and their ms in
     * those, and the untimed ones' draw-call ms; the presents' ms in
     * IDXGISwapChain1::Present; one QueryPerformanceCounter call's cost (ns,
     * taken off each timed call). */
#define TWOC_DK 5
    unsigned dk_draws[TWOC_DK], dk_timed[TWOC_DK], dk_calls[TWOC_DK];
    double dk_ctx_ms[TWOC_DK], dk_ms[TWOC_DK];
    double present_ms, ctx_qpc_ns;
    /* Step 95: capturing frames' first-pass draws looked up against the game
     * frame before's (one pair of frames in 64): frames, draws, those whose
     * state repeats, those that repeat whole (state, vertices, indices,
     * constants, textures). */
    unsigned rpt_frames, rpt_draws, rpt_state, rpt_all;
    /* Step 95 (cut 2): program draws recorded, and the blocks of 16
     * registers they copied (the rest shared with the program draw before). */
    unsigned k_draws, k_copied;
    /* Step 61: frame interpolation. Frames drawn as sub-frames (`interp_k`
     * each, the last), and those held instead: a camera step or turn over the
     * limit, too few draws matched, no frame before, or another reason (the
     * overlay, a cost check). Draws redrawn per sub-frame by kind. */
    double interp_per;                  /* step 77: frames each game frame at the plan's rate (was interp_k) */
    unsigned interp_frames, interp_subs;
    unsigned interp_cut_step, interp_cut_turn, interp_cut_match, interp_cut_first, interp_off;
    unsigned interp_vp, interp_ff, interp_kept, interp_cpos;
    unsigned interp_decal;              /* step 102: 4-vertex program draws drawn nearer in in-between frames */
    unsigned interp_other_cam;          /* step 65: 3D draws kept as recorded: another camera than the world's */
    /* Step 62: objects in interpolated frames (paired with the frame
     * before's, none of their key, or snapped: stepped over the limit), and
     * per sub-frame the program and fixed-function draws moved and the
     * skinned ones whose bones moved. */
    unsigned interp_obj, interp_obj_new, interp_obj_snap, interp_obj_vp, interp_obj_ff, interp_bones, interp_bone_snap;
    unsigned interp_obj_swap;           /* step 63: in the place of another kind of object (kept as drawn) */
    unsigned interp_obj_loose;          /* step 68: paired across a texture change (a flipbook's next frame) */
    unsigned interp_obj_motion;         /* step 68: stepped over the snap, but on with its own motion */
    unsigned interp_skel, interp_skel_bones;    /* step 66: skeletons a frame; bones blended in one */
    double interp_matched;              /* sum of each interpolated frame's fraction of draws matched */
    /* Step 96: the frames shown as they are for too few draws matched: the
     * sum of their fractions matched and of their draws. */
    double interp_cut_frac, interp_cut_draws;
    /* Step 74: adaptive sub-frames. Game frames by the step allowed then
     * (step 77: [0] the plan's rate, then each lower one: interp_step_fps),
     * changes of it, and the measured frames' sum of time between presents
     * and of it outside the pacing wait (ms). */
    unsigned interp_at[8], interp_adapt_changes, interp_adapt_frames;
    double interp_adapt_dt, interp_adapt_busy;
    double interp_adapt_game, interp_adapt_replay;  /* step 78: of it, the game side's time and the replay side's */
    double interp_step_fps[8];          /* step 77: each step's frames a second (0: none) */
    /* Step 65: the game's overlay pass: passes, its draws, those under the
     * world's camera (the HUD counters and title cards). */
    unsigned overlay_passes, overlay_draws, overlay_world_cam;
    /* ... the HUD's draws and, with the HUD at the edges, its elements
     * moved left, moved right and left in the middle. */
    unsigned hud_draws, hud_left, hud_right, hud_mid, hud_passes;
    /* Step 90: the visible swap chain's presentation mode (GetFrameStatisticsMedia), read 4 times a second:
     * composed, overlay, none, composition failure, not read; and the last one read. */
#define TWOC_PMODES 5
    unsigned pmode[TWOC_PMODES], pmode_now;
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
 * program reads, in ascending order (d3dh_vs_inputs). `fresh` (step 67):
 * the constants SetVertexShaderConstant wrote since the program draw
 * before, bit n = cn; a skinned draw's own bones (the rest of its palette
 * holds earlier draws' leftovers). Step 95 (cut 2): the same registers as
 * 12 blocks of 16, blk[b] holding c16b-c16b+15: the bridge points them into
 * `consts`; a recording keeps a block of its own only where it differs from
 * the program draw before's in the frame and shares that draw's otherwise,
 * and has `consts` NULL once recorded (the replay side reads blk). */
#define D3DH_KBLOCKS 12
typedef struct {
    uint32_t key, n_insns;
    const uint32_t *code;
    const float *consts;
    uint32_t fresh[6];
    const float *blk[D3DH_KBLOCKS];
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
     * title cards; the pause menu has a camera of its own). */
    uint8_t overlay, world_cam;
    /* Step 66: drawn in one of the game's display-list flushes other than
     * the overlay pass (the world's, its effects'): a pre-transformed draw
     * there is 3D the game projected itself (the warp portal's lightning),
     * with the widened projection: stretched with the 3D, not pillarboxed. */
    uint8_t world_list;
    /* Queued by the panel's in-game part (the HUD counters and title cards,
     * never the pause menu or a hub pad's panel): the panel's call site that
     * queued it (one per counter's icon, its digits, a card's line), else 0. */
    uint32_t hud;
    /* Set by the host (step 65, HUD at the screen edges): the clip-space x
     * offset, in units of w, that moves the draw's element to its edge. */
    float hud_dx;
    /* Step 84: `verts` lie in this block (the bridge's vertex cache): a
     * recording keeps them there with a reference instead of a copy. NULL:
     * copied as before. */
    struct d3dh_hold *hold;
} d3dh_draw_t;

/* Step 84: a reference-counted block of memory the bridge shares with the
 * recordings (a cached program draw's vertices, after this header). Made by
 * the bridge with malloc and refs 1; freed by d3dh_hold_release at the last
 * reference, on whichever thread drops it. */
typedef struct d3dh_hold {
    volatile long refs;
    uint32_t pad[3];
} d3dh_hold_t;
void d3dh_hold_release(d3dh_hold_t *h);

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

/* Step 84: `n` bytes (16-aligned) in the frame being recorded, for data the
 * bridge builds for its next draw (a program draw's constants, vertices)
 * so that the recording keeps them there instead of copying them; NULL
 * when that draw won't be recorded (the bridge's own memory then). Game
 * side; for the next draw only. */
void *d3dh_rec_alloc(size_t n);

/* Step 65: the game's overlay pass begins (1) or ends (0); its draws come
 * in between, with `overlay` set. */
void d3dh_overlay_pass(int begin);
/* Step 92: the game's world pass begins (1, with the world's projection,
 * 0x242398, row-major) or ends (0); ambient occlusion goes in inside it. */
void d3dh_world_pass(int begin, const float *proj);
/* Step 65: the HUD at the screen edges (1) or in the middle 4:3 (0, as the
 * game draws it) in a picture wider than 4:3, `margin` percent of the
 * picture's width in from the edge (0-10). From the next overlay pass. */
void d3dh_set_hud(int edges);

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
 * F1 on the host window (step 86; Esc before) opens the overlay at the next present: that present
 * holds the game until it closes. Settings the host can't apply itself (the
 * aspect, the render size, the pacing: d3d_bridge.c decides them) go to the
 * callbacks set here, without the host's lock.
 *
 * Step 78 (notes 18.81): a live change is queued and each side applies it
 * at its own frame boundary, the way a render thread will need: `game` (the
 * aspect the game thread's translation reads) on the game
 * thread at its Swap, `host` (the host picture's size, the pacing) on the
 * presenting side between frames. */
#include "settings.h"
typedef void (*d3dh_apply_fn)(const twoc_settings_t *s);
void d3dh_set_apply(d3dh_apply_fn game, d3dh_apply_fn host);
/* Step 78: the picture's width over height (0: 4:3) as the game side
 * records frames under it (the HUD at the screen edges); the bridge's game
 * part sets it with the aspect. */
void d3dh_record_wide(double wide);
/* The host picture at a new size and aspect (`wide` as for d3dh_create).
 * From the first change on, a host render target of that size stands in for
 * xbox_d3d8's back buffer, whose size is fixed; screen-sized render targets
 * are made again at their next use. Step 90: the size shown (the render
 * size); the picture is drawn at the render scale's share of it. */
void d3dh_resize(unsigned host_w, unsigned host_h, double wide);
/* Step 90: the render scale, 25-200 percent of the render size, at once. */
void d3dh_set_render_scale(int pct);
/* Step 90: "Pause when in the background" (RECOMP_PAUSE_UNFOCUSED), at once. */
void d3dh_set_pause_unfocused(int on);
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
/* Step 77: the frame-rate plan for the setting `fr` (0: none, 1: the
 * display's rate) under vsync pacing, for a game presenting at `game_hz`:
 * sets *interval and *hz for d3dh_create / d3dh_set_pacing and returns 1;
 * 0: no plan (the caller paces as before). */
int d3dh_frame_plan(int fr, double game_hz, unsigned *interval, double *hz);
/* Step 75: "Adaptive frame rate": on, fewer sub-frames while game frames
 * run late (notes 18.77); off, always the display's count. At the next
 * present. */
void d3dh_set_interp_adaptive(int on);
/* For the controller poll (recomp_manual.c): 1 while the overlay is open;
 * *closes counts the times it has closed. Callable with the bridge off. */
int d3dh_overlay_input(unsigned *closes);
/* Step 79: "Render thread" (TWOC_RT_OFF, _ON, _AUTO): taken at the game's
 * next Swap. */
void d3dh_set_render_thread(int mode);

#endif
