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
#include "about.h"

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
    double interp_fps;                  /* step 61/77: frames a second when interpolating (0: can't here) */
    double interp_per;                  /* ... and frames each game frame (2.4 at 144 Hz) */
    int interp_on;                      /* step 61: interpolating now */
    const char *interp_why;             /* step 63: why interp_fps is 0 */
    double interp_now;                  /* step 74/77: frames a second now (fewer than interp_fps while late) */
    const char *interp_note;            /* step 77: the plan: the rate it shows and whether evenly, and why */
    int rt_on;                          /* step 79: the render thread draws the frames now */
    unsigned rt_cores, rt_auto_cores;   /* ... this PC's processor cores, and the most Auto needs */
    int meas_ok;                        /* step 82: the rates below measured (over the last second) */
    double game_ups, shown_fps;         /* ... the game's updates (guest swaps) and the frames presented a second */
    unsigned pad;                       /* step 82: port 0's pad (OVL_PAD_*, pads.h; 0: none), for navigation */
    short pad_lx, pad_ly;               /* ... its left stick */
    unsigned view_w, view_h;            /* step 90: the size it is shown at (the render size); host_w x host_h
                                         * is that times the render scale */
    int fast_title_now;                 /* step 90: this run's title animation sped up */
    int ao;                             /* step 92: the ambient occlusion in effect (TWOC_AO_*) */
    const twoc_about_t *about;          /* step 93: the About tab's lines (collected at the opening) */
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
 * (returns 1) instead of closing the overlay; step 86: F1 ends it and closes
 * the overlay. Window thread. */
int ovl_cancel_capture(void);
/* One frame into `rtv` (w x h pixels, UI scaled by `scale`), editing `s`.
 * Returns OVL_CHANGED when the player changed a setting, OVL_CLOSE when
 * they asked to resume. */
int ovl_frame(ID3D11RenderTargetView *rtv, unsigned w, unsigned h, float scale, float dt, twoc_settings_t *s,
              const ovl_info_t *info);
/* Step 82: the frame-rate counter ("Show frame rate"), `lines` in the top
 * left corner of `rtv`, over the game's picture, with the overlay closed.
 * Step 86: `lines` NULL for no counter; `msg` (NULL: none) the startup
 * message near the bottom of the picture at pic[0..3] (x, y, w, h), at
 * opacity `msg_alpha`, scaled by pic[4]. */
void ovl_counter(ID3D11RenderTargetView *rtv, unsigned w, unsigned h, float scale, const char *lines,
                 const char *msg, float msg_alpha, const float *pic);
/* Step 82: the measured rates as the overlay and the counter word them. */
void ovl_rates_text(int ok, double game_ups, double shown_fps, int brief, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
