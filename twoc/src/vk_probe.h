/**
 * Vulkan, stage 1 of a native backend (step 86, notes 18.89): a probe behind
 * RECOMP_VULKAN=probe, off by default. It loads the Vulkan loader at run time
 * (volk: no import, so the exe's DLL list is unchanged), makes an instance,
 * picks the GPU the D3D11 device is on (by its LUID), makes a device, a
 * window of its own with a swap chain, and presents cleared frames there on
 * a thread of its own for the rest of the run, timing them with
 * VK_KHR_present_wait where the driver has it. The game's window, its D3D11
 * device and everything it draws are untouched. Unset: nothing here runs
 * and the Vulkan loader is never loaded.
 *
 * Lines: [VK] the loader, the instance, each GPU (name, driver, API version,
 * LUID), the extensions a backend would use (present, descriptor, dynamic
 * state, interop), the surface's formats and present modes, the swap chain,
 * and every 10 s the frames presented and, with present_wait, the gaps
 * between frames reaching the screen.
 */
#ifndef TWOC_VK_PROBE_H
#define TWOC_VK_PROBE_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Called once the D3D11 device is up (d3d_host.c), with its adapter's LUID
 * (NULL: not known). Starts the probe if RECOMP_VULKAN=probe; returns at
 * once either way. */
void vkp_start(const LUID *d3d_luid);

#ifdef __cplusplus
}
#endif

#endif
