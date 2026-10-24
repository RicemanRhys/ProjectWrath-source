/* xbox_runtime.h -- xboxrecomp's headers the exe uses (step 108, notes 18.111).
 *
 * xboxrecomp's umbrella header, <xbox/xboxrecomp.h>, less its "apu.h": the emulated APU (from xemu, LGPL) is
 * xemu_apu.dll now, and the exe reaches it only through apu_dll.h / apu_dll/xemu_apu_api.h, so nothing of the
 * APU's own headers is compiled into the exe. Otherwise the same headers in the same order. */
#ifndef TWOC_XBOX_RUNTIME_H
#define TWOC_XBOX_RUNTIME_H

#include "kernel.h"             /* Xbox kernel types and functions */
#include "xbox_memory_layout.h" /* Memory layout API */
#include "d3d8_xbox.h"          /* D3D8 compatibility interface */
#include "dsound_xbox.h"        /* DirectSound compatibility */
#include "xinput_xbox.h"        /* XInput compatibility */

/* NV2A headers */
#include "nv2a_state.h"         /* NV2A GPU state */
#include "nv2a_regs.h"          /* NV2A register definitions */
#include "qemu_shim.h"          /* QEMU type abstraction */

#endif /* TWOC_XBOX_RUNTIME_H */
