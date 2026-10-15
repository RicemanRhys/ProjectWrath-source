/**
 * The settings overlay (step 54, notes 18.57): Dear ImGui on the bridge's
 * D3D11 device, drawn by d3d_host.c over the paused game's last frame, or
 * (step 57, pause off) over each frame of the running game.
 *
 * Threads: ovl_input runs on the host window's thread (its messages), the
 * rest on the thread that presents. Both take the overlay's own lock and
 * never the host's g_lock, so a present waiting on the window thread can't
 * deadlock against them.
 */
#ifndef TWOC_OVERLAY_H
#define TWOC_OVERLAY_H

#include <windows.h>
#include <d3d11.h>

#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned host_w, host_h;            /* the picture the host renders now */
    unsigned guest_w, guest_h;
    double display_hz;                  /* 0: unknown */
    unsigned interval;                  /* DXGI sync interval of the game's presents */
    double frame_hz;                    /* the frame limiter's rate, 0: off */
    int executor_paces;                 /* -Bridge with the NV2A executor: the host doesn't pace */
    int dpi_aware;                      /* the window is in real pixels (a render size at the start) */
    const char *save_note;              /* the last save's outcome, or NULL */
    int running;                        /* step 57: the game runs behind the overlay (pause off) */
    int aa;                             /* step 58: the anti-aliasing in effect (TWOC_AA_*) */
    unsigned ms_samples;                /* step 58: MSAA's samples on the back buffer, 0 without */
    unsigned interp_k;                  /* step 61: frames shown a game frame when interpolating (0: can't here) */
    int interp_on;                      /* step 61: interpolating now */
    const char *interp_why;             /* step 63: why interp_k is 0 */
    unsigned interp_now;                /* step 74: frames shown a game frame now (fewer than interp_k while late) */
} ovl_info_t;

enum { OVL_CHANGED = 1, OVL_CLOSE = 2 };

/* Once, on the presenting thread. Returns 0 on success. */
int ovl_init(ID3D11Device *dev, ID3D11DeviceContext *ctx);
/* A window message while the overlay is open; `sx`, `sy` scale the client's
 * coordinates to the swap chain's pixels. Window thread. */
void ovl_input(UINT msg, WPARAM wp, LPARAM lp, float sx, float sy);
/* The overlay is opening: no keys or buttons held over from before. */
void ovl_opened(void);
/* Step 57: Esc while a binding waits for its key or button ends the wait
 * (returns 1) instead of closing the overlay. Window thread. */
int ovl_cancel_capture(void);
/* One frame into `rtv` (w x h pixels, UI scaled by `scale`), editing `s`.
 * Returns OVL_CHANGED when the player changed a setting, OVL_CLOSE when
 * they asked to resume. */
int ovl_frame(ID3D11RenderTargetView *rtv, unsigned w, unsigned h, float scale, float dt, twoc_settings_t *s,
              const ovl_info_t *info);

#ifdef __cplusplus
}
#endif

#endif
