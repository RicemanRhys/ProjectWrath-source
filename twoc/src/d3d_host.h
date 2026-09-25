/**
 * Host side of the D3D mirror bridge: the xbox_d3d8 device, its window, and
 * the one lock around every host call. See d3d_host.c. Called only from
 * d3d_bridge.c; no guest memory is read or written here.
 */
#ifndef TWOC_D3D_HOST_H
#define TWOC_D3D_HOST_H

#include <stdint.h>

typedef struct {
    unsigned presents, present_fails, present_status;   /* status: succeeded but not S_OK */
    unsigned clears, clears_colour, clears_depth, clears_partial_colour;
    unsigned dumps;
} d3dh_stats_t;

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
