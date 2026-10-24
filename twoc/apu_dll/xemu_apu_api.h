/*
 * xemu_apu_api.h -- the interface between crash_twoc.exe and xemu_apu.dll (step 108, notes 18.111).
 *
 * This project's own file (MIT, twoc/LICENSE). xemu_apu.dll is the Xbox audio processor (MCPX APU) emulation
 * that xboxrecomp extracted from xemu, under the GNU LGPL 2.1 or later; it is built on its own so that anyone can
 * rebuild it from its source (the package's Source folder) and put their copy in place of it. This header is all
 * the two sides share: one exported function hands the exe a table of plain C functions.
 *
 * The exe loads the DLL from its own folder by full path (never the current folder or PATH), asks for
 * XEMU_APU_INTERFACE_VERSION and stops with a message if the DLL offers another. A change to any function's
 * meaning or to the table's layout takes a new version number; adding functions at the end does too.
 *
 * What crosses:
 *   - init: the base of the guest's memory (the Xbox's RAM at 0, the contiguous window at +0x80000000), which
 *     the APU reads voices, scatter-gather lists and sample buffers from and writes voice state into;
 *   - handle_mmio: an access the exe's vectored exception handler caught in the APU's 512 KB of registers
 *     (0xFE800000-0xFE87FFFF in guest addresses, kept unmapped): the thread's CONTEXT, the faulting host
 *     address and its guest address; the DLL decodes the instruction, does the register read or write and steps
 *     RIP past it (nonzero: handled);
 *   - mmio_read / mmio_write: the same registers by offset, for a caller that decodes for itself;
 *   - the player's settings: a gain per kind of voice, the HRTF, and the output's channel count.
 * Nothing calls back into the exe. The APU runs its own thread, paced by its output device (XAudio2) and the
 * clock; the game's DirectSound code polls the APU's interrupt status through handle_mmio like any register.
 */
#ifndef XEMU_APU_API_H
#define XEMU_APU_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef XEMU_APU_INTERFACE_VERSION      /* a test build of the DLL may set another, to see the exe refuse it */
#define XEMU_APU_INTERFACE_VERSION 1
#endif
#define XEMU_APU_DLL_NAME          "xemu_apu.dll"
#define XEMU_APU_GET_API_NAME      "xemu_apu_get_api"

struct _CONTEXT;

typedef struct XemuApuApi {
    uint32_t size;          /* sizeof(XemuApuApi) as the DLL was built */
    uint32_t version;       /* XEMU_APU_INTERFACE_VERSION as the DLL was built */

    /* Start the APU (its state, its output, its thread) over the guest's memory. NULL: it failed (the log says
     * why). Once per process. */
    void *(*init)(uint8_t *guest_memory);
    /* Stop the thread and the output and free the state. */
    void (*shutdown)(void);

    /* An access violation inside the APU's registers: decode the instruction at ctx->Rip, do the access, step
     * past it. Nonzero: handled (continue execution); 0: not an access it knows (the exe treats it as a crash).
     * guest_va is the faulting guest address (0xFE800000 + the register's offset). */
    int (*handle_mmio)(struct _CONTEXT *ctx, uintptr_t fault_addr, uint32_t guest_va, int is_write);
    /* A register by its offset from 0xFE800000, size 1, 2 or 4 bytes. */
    uint64_t (*mmio_read)(uint64_t offset, unsigned size);
    void (*mmio_write)(uint64_t offset, uint64_t value, unsigned size);

    /* A gain (0.0-1.0) on every stream voice (stream != 0: music, ambience, speech) or every buffer voice
     * (stream == 0: sound effects). 1.0, the default, leaves the output as it is. Any thread, any time. */
    void (*set_voice_gain)(int stream, float gain);
    /* The voice processor's HRTF on 3D voices (on by default, as on the Xbox). Any thread, any time. */
    void (*set_hrtf)(int on);
    int (*get_hrtf)(void);
    /* The output's channels: 2 or 6 (5.1), 0 before init. *device_channels (may be NULL) gets the default
     * device's channel count when 5.1 was asked for (RECOMP_SPEAKERS=5.1), else 0. */
    int (*output_channels)(int *device_channels);
} XemuApuApi;

/* The DLL's one export. Returns the table for `version`, or NULL if the DLL doesn't offer that version; either
 * way *dll_version (if not NULL) gets the version it does offer. */
typedef const XemuApuApi *(*XemuApuGetApiFn)(uint32_t version, uint32_t *dll_version);

#ifdef __cplusplus
}
#endif

#endif /* XEMU_APU_API_H */
