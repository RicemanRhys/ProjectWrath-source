/**
 * Anti-aliasing on the host (step 58, notes 18.61): FXAA and SMAA on the
 * finished frame, and the copy that fills a multisampled surface from its
 * single-sample texture (d3d_host.c's MSAA). See aa.c.
 *
 * Called only from d3d_host.c, with its lock held, on xbox_d3d8's D3D11
 * device. Each call saves the D3D11 state it sets and puts it back, since
 * xbox_d3d8 keeps its own bindings.
 */
#ifndef TWOC_AA_H
#define TWOC_AA_H

#include <d3d11.h>

/* The frame `src` (R8G8B8A8, single-sampled) through FXAA or SMAA
 * (TWOC_AA_FXAA, TWOC_AA_SMAA) into a texture of aa.c's, the same size and
 * format. Returns 0 and *out referenced, or -1 (show `src` as it is). */
int aa_post(int mode, ID3D11Texture2D *src, ID3D11Texture2D **out);

/* Every pixel of `src` into `dst` (w x h, any sample count) as it is, alpha
 * too. Returns 0, or -1 if the copy's shaders aren't available. */
int aa_copy(ID3D11RenderTargetView *dst, ID3D11ShaderResourceView *src, unsigned w, unsigned h);

#endif
