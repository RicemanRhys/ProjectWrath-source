/**
 * Ambient occlusion on the host (step 92, notes 18.95): SSAO or GTAO (Intel's
 * XeGTAO, third_party/xegtao) from the back buffer's depth, multiplied into
 * the back buffer after the world's opaque draws. See ao.c.
 *
 * Called only from d3d_host.c, with its lock held, on xbox_d3d8's D3D11
 * device. Each call saves the D3D11 state it sets and puts it back, since
 * xbox_d3d8 keeps its own bindings.
 */
#ifndef TWOC_AO_H
#define TWOC_AO_H

#include <d3d11.h>

/* What one pass is given: the mode (TWOC_AO_SSAO or TWOC_AO_GTAO), the
 * strength (0-1 of the effect), the world's projection as the game set it
 * (row-major D3D, 0x242398), the fog the world's draws had (its
 * FOGTABLEMODE, D3DFOG_*: 3 linear in view depth from fog_start to fog_end,
 * 1 and 2 exp and exp2 of fog_density; 0 none), the effect's radius in the
 * game's units and whether to work at half the resolution. */
typedef struct {
    int mode;
    float strength;
    float proj[16];
    int fog_mode;
    float fog_start, fog_end, fog_density;
    float radius;
    int half;
    int denoise;                        /* XeGTAO_Denoise passes, 1-4 */
    int debug;                          /* RECOMP_AO_DEBUG: 1 the occlusion alone (grey), not multiplied */
} ao_params_t;

/* Occlusion from `depth` (the back buffer's depth, R24 read as UNORM, with
 * `samples` samples a pixel) multiplied into `rtv` (w x h, the same samples).
 * Returns 0, or -1 if the shaders or textures aren't available (nothing
 * drawn). */
int ao_apply(const ao_params_t *p, ID3D11ShaderResourceView *depth, unsigned samples, ID3D11RenderTargetView *rtv,
             unsigned w, unsigned h);

/* Step 92 item 5: the GPU's time for each pass (RECOMP_AO_GPU=1: D3D11
 * timestamps, read back a few frames later without waiting), averaged
 * since the last call, and the passes counted; then started over. */
#define AO_PASSES 5                     /* depth, prefilter, occlusion, denoise, apply */
typedef struct {
    unsigned runs, timed;
    double ms[AO_PASSES], total_ms;
} ao_stats_t;
void ao_stats_take(ao_stats_t *s);

/* Its textures and shaders given back (the device is going away). */
void ao_release(void);

#endif
