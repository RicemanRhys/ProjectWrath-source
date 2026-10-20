/**
 * The F1 menu's About tab (step 93, notes 18.96): what this build is and
 * what it runs on, in lines the player can read out or copy into a report.
 *
 *   - the build: the commit the exe's sources come from and its date
 *     (build/generated/build_info.h, made at every build by
 *     tools/build_info.cmake), "with changes not committed" when the tree
 *     had them, xboxrecomp's commit, and when the exe was linked (its PE
 *     header's time stamp);
 *   - link-time code generation on or off (TWOC_LTCG);
 *   - SDL's version (the one running, or the one built in);
 *   - the graphics card (DXGI's adapter of the bridge's D3D11 device), its
 *     memory, and the driver's version (the user-mode driver's, as Device
 *     Manager shows it; NVIDIA's own number beside it);
 *   - step 97: how that card was chosen (Automatic or the Display tab's
 *     Graphics card, gpu.h) and the card the window's display is on;
 *   - step 97: the processor (its cores, threads, and performance and
 *     efficiency cores when it has both) and the process's power
 *     throttling (cpu_info.h);
 *   - the display mode of the window's monitor and its refresh rate;
 *   - Windows' edition, version and build (RtlGetVersion, the registry's
 *     DisplayVersion and UBR);
 *   - the game files' check at the start (gamefiles.c).
 *
 * Collected when the overlay opens (never per frame), on the thread that
 * draws it.
 */
#ifndef TWOC_ABOUT_H
#define TWOC_ABOUT_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char build[256];
    char ltcg[64];
    char sdl[96];
    char gpu[192];
    char driver[96];
    char gpu_choice[192];               /* step 97: Automatic, or chosen */
    char gpu_display[192];              /* step 97: the display's card */
    char cpu[256];                      /* step 97: the processor, its cores, hybrid or not (cpu_info.h) */
    char cpu_power[192];                /* step 97: the process's power throttling */
    char display[160];
    char windows[160];
    char files[600];
} twoc_about_t;

/* `dev` is the bridge's ID3D11Device (NULL: no graphics card lines),
 * `hwnd` its window, `display_hz` the refresh rate the bridge measures
 * (0: unknown). Logs one [ABOUT] block the first time. */
void twoc_about_collect(twoc_about_t *a, void *dev, HWND hwnd, double display_hz);

#ifdef __cplusplus
}
#endif

#endif
