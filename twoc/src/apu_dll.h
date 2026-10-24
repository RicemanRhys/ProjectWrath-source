/* apu_dll.h -- the game's audio library, xemu_apu.dll (step 108, notes 18.111).
 *
 * The emulated Xbox audio processor (xboxrecomp's src/apu, from xemu, LGPL) is a DLL of its own beside the exe,
 * so a player can rebuild it from its source and swap it in (apu_dll/xemu_apu_api.h has the interface). The exe
 * loads it at the start, from the exe's own folder by full path, and checks its interface version. The rest of
 * the exe keeps calling the APU by the names it always did (mcpx_apu_init_standalone, apu_hook_handle_mmio,
 * mcpx_apu_set_voice_gain, mcpx_apu_set_hrtf, mcpx_apu_get_hrtf, mcpx_apu_output_channels): apu_dll.c defines
 * them, each passing the call on to the DLL. */
#ifndef TWOC_APU_DLL_H
#define TWOC_APU_DLL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Load xemu_apu.dll from the exe's folder and check it. 1: ready. 0: missing, unloadable or another interface
 * version: a message box has said what is wrong, where the file should be and what to do (and the log too);
 * the caller ends the process. */
int twoc_apu_dll_load(void);

#ifdef __cplusplus
}
#endif

#endif /* TWOC_APU_DLL_H */
