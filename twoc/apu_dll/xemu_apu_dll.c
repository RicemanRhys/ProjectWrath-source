/*
 * xemu_apu_dll.c -- xemu_apu.dll's one export (step 108, notes 18.111).
 *
 * This project's own file (MIT, twoc/LICENSE). The DLL is xboxrecomp's src/apu built on its own, unchanged:
 * the MCPX APU emulation extracted from xemu (apu_core.c, apu_vp.c, apu_dsp.c and their headers; GNU LGPL 2.1
 * or later), with xboxrecomp's own XAudio2 output (apu_xaudio2.c) and register-access decoder (apu_mmio_hook.c),
 * both MIT. This file only hands their functions to the exe as the table in xemu_apu_api.h.
 */
#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

#include "apu.h"
#include "xemu_apu_api.h"

/* apu_mmio_hook.c: the APU the decoder dispatches to, and the decoder itself. */
extern MCPXAPUState *g_apu_state;
extern bool apu_hook_handle_mmio(PCONTEXT ctx, uintptr_t fault_addr, uint32_t fault_xbox_va, int is_write);

static void *api_init(uint8_t *guest_memory)
{
    g_apu_state = mcpx_apu_init_standalone(guest_memory);
    return g_apu_state;
}

static void api_shutdown(void)
{
    MCPXAPUState *d = g_apu_state;

    g_apu_state = NULL;
    mcpx_apu_shutdown(d);
}

static int api_handle_mmio(struct _CONTEXT *ctx, uintptr_t fault_addr, uint32_t guest_va, int is_write)
{
    return apu_hook_handle_mmio(ctx, fault_addr, guest_va, is_write);
}

static uint64_t api_mmio_read(uint64_t offset, unsigned size)
{
    return mcpx_apu_mmio_read(g_apu_state, offset, size);
}

static void api_mmio_write(uint64_t offset, uint64_t value, unsigned size)
{
    mcpx_apu_mmio_write(g_apu_state, offset, value, size);
}

static const XemuApuApi g_api = {
    sizeof(XemuApuApi),
    XEMU_APU_INTERFACE_VERSION,
    api_init,
    api_shutdown,
    api_handle_mmio,
    api_mmio_read,
    api_mmio_write,
    mcpx_apu_set_voice_gain,
    mcpx_apu_set_hrtf,
    mcpx_apu_get_hrtf,
    mcpx_apu_output_channels,
};

__declspec(dllexport) const XemuApuApi *xemu_apu_get_api(uint32_t version, uint32_t *dll_version)
{
    if (dll_version)
        *dll_version = XEMU_APU_INTERFACE_VERSION;
    return version == XEMU_APU_INTERFACE_VERSION ? &g_api : NULL;
}
