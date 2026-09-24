# Step 29: Option B planning pass (bridging the game's D3D to `xbox_d3d8`)

Planning only. No code, no re-lift, no rebuild, no run. Sources: `D3D.asm`/`text.asm`
from `xboxrecomp/tools/disasm/output/`, the xref database (`xrefs.json`,
`functions.json`), `xboxrecomp/src/d3d/*`, `xboxrecomp/src/nv2a/nv2a_regs.h`,
`xboxrecomp/tools/recomp/manual_scan.py`, `twoc/src/recomp_manual.c`, `twoc/src/main.c`.
Scripts and raw dumps are next to this report (`d3d99_asm.txt`, `table.json`,
`tables.md`, `d3dlib.py`, `entries.py`, `dump.py`, `table.py`).

## Summary

1. **All 99 entries are identified by XDK name, and there is a 100th.** `D3DDevice_End`
   (`0x101F00`) is reached by a tail `jmp` from the game's `0x3AE30`, so 18.21's
   call-edge count missed it. Full table at the end of this report.
2. **The all-or-nothing constraint from 18.20 only applies to *replacing* native D3D.**
   18.20 wrote a host pointer into the guest device slots, and unbridged code then read
   it as a native struct. A **mirror bridge** does not do that: native D3D keeps running
   exactly as today, and a wrapper renders the same call through `xbox_d3d8` from host-side
   state. The bridge never writes guest memory, so there is nothing for unbridged code to
   trip on. That makes Option B **incremental**: each stage can be tested on its own, with
   the default run unchanged.
3. **The lifter already supports native-first wrappers.** In `recomp_manual.c`, declare
   `extern void sub_X_gen(void);` and define `void sub_X(void)`. The lifter then emits the
   original body as `sub_X_gen` (`manual_scan.py`, `_WRAP_RE`), and every direct and
   indirect caller reaches the wrapper. No toolkit change is needed.
4. **Tier B is resolved: no new object model.** Guest memory keeps the XDK's own layout
   (static device struct, 12-24-byte resource headers, native refcounts) as the **only**
   representation. Host objects are a cache keyed by guest header address and checked
   against a signature. None of the four Tier B entries needs a bridge. Two of step 19's
   four labels were wrong: `0x100DD0` is `D3DVertexBuffer_Lock` (not a cursor primitive),
   and `0x102440` is `D3DDevice_CreateVertexShader`.
5. **Only 15 of the 100 entries need wrappers** (29 call sites): CreateDevice, Clear,
   Swap, the 4 draw paths plus immediate mode (7 entries), CopyRects,
   SetVertexShaderConstant, 2 LockRects and the game's texture fast-path push. Another 57
   are left native: their effect already lands in D3D's own shadow state (`D3D__RenderState`,
   `D3D__TextureState`, device fields), and the bridge reads it at draw time. The last 28
   are queries or bookkeeping the host never needs.
6. **The 10 Tier D-big entries** are Swap, DrawIndexedVertices, DrawVerticesUP,
   DrawVertices, Begin, ApplyStateBlock, the device's Release, the two MultiSample*Mode
   setters and SetRenderTarget. Step 19's guesses of Reset, resource creation and shader
   were wrong. Their large closures are D3D's own NV2A state compiler
   (`0x108F40`/`0x108EC0`), and the mirror never has to trace it. Five of the 10 need
   wrappers; the other five stay native.
7. **Revised size: about 8 sessions.** The cost is no longer in tracing. It is in a
   draw-time state translator (XDK state numbering and GL-style values to PC D3D8), texture
   and vertex fetch from guest memory, and a known list of `xbox_d3d8` gaps (section 6).
8. **Three decisions are yours** (section 8): accept the mirror design; how to handle
   `xbox_d3d8` gaps under the "stays in `twoc/`" rule; and where 3D reference frames come
   from, since the splash is the only known-good output today.

## 1. What the statically linked D3D actually is (facts from the disassembly)

**Device.** A static `CDevice` at `0x10C110` (`0xAB8` dwords = `0x2AE0` bytes; CreateDevice
zeroes exactly that on failure, and so does the device's Release on teardown).
`[0x10EBF0]` and `[0x10EBF4]` both hold `&device`. Fields used below:

| Offset | Meaning | Evidence |
|---|---|---|
| +0x00 / +0x04 | push-buffer put / threshold | every setter: `mov eax,[esi]; cmp eax,[esi+4]` |
| +0x08 | device flags (0x800 in-draw, 0x20 recording a state block, 0x10 skip VS-constant shadow) | Begin/Draw*, 0x1012CC, 0x102AA0 |
| +0x1C | BaseVertexIndex | SetIndices `0xFFEA0` |
| +0x30 / +0x34 | software PUT / pointer to GET (18.1 fence mirror) | `main.c` |
| +0x370 | current pixel shader | SetPixelShader `0x102BB0` |
| +0x380 / +0x384 | current vertex-shader object / handle | SetVertexShader `0x102940` |
| +0x38C | current index buffer | SetIndices |
| +0x390 / +0x394 | light array (0x90 bytes each, enable flags at +0x68) / count | SetLight `0xFF9C0`, LightEnable `0xFFBA0` |
| +0x3EC | pixel-shader constants | SetPixelShaderConstant `0x102D80` |
| +0x440 | device refcount | `0xFEC30` / `0xFEC50` |
| +0x750 | transforms, 64 bytes each, XDK index | SetTransform `0xFEA20` |
| +0x9D0 | viewport (X, Y, W, H, MinZ, MaxZ) | SetViewport `0xFF860`, GetViewport `0xFEB30` |
| +0x9F0 | material | SetMaterial `0xFEB50` |
| +0xA78 / +0xA88 | textures[4] / palettes[4] | SetTexture `0xFFC90`, SetPalette `0xFFE10` |
| +0xB98 | vertex-shader constants, slot (reg + 96) * 16 | SetVertexShaderConstant `0x102AA0` |
| +0x2018 | shader constant mode | `0x1026F0` |
| +0x2070 / +0x2074 | render target / depth surface | SetRenderTarget `0xFEF20` |
| +0x207C / +0x2080 / +0x2084 | back / front / third buffer surfaces | GetBackBuffer `0xFF450` |
| +0x2430 | callback pointer (the game passes `0x39910`) | `0xFEC90` |
| +0x2AC4 | swap counter | Swap `0x100C40` |

**Globals.** `D3D__DirtyFlags` `0x10EC10`; current index data `0x10EC0C`;
`D3D__TextureState` `0x10EC18` (4 stages x 32 dwords); `D3D__RenderState` `0x10EE18`
(146 dwords: 0-56 pixel-shader states, 57-81 "simple", 82-116 "deferred", 117-145
"complex"); state-block change bytes `0x10F078` (`0x82` dwords, cleared by
BeginStateBlock); stream table `0x10F280` (12 bytes per stream: stride at +0, VB header
at +8); the FVF path's built-in vertex-shader object `0x10F458`; caps `0x1B11E8`.

The complex-state order is pinned by the shadow addresses: `0x10EFEC` = RS[117]
PSTextureModes, then VertexBlend, FogColor, FillMode, BackFillMode, TwoSidedLighting,
NormalizeNormals, ZEnable, StencilEnable, StencilFail, FrontFace, CullMode, TextureFactor,
ZBias, LogicOp, EdgeAntialias, MultiSampleAntiAlias, MultiSampleMask, MultiSampleMode,
MultiSampleRenderTargetMode, ShadowFunc, LineWidth, Dxt1NoiseEnable, YuvEnable,
OcclusionCullEnable, StencilCullEnable, RopZCmpAlwaysRead, RopZRead, and
DoNotCullUncompressed at RS[145] (`0x10F05C`). The game's dispatcher uses the same
numbers (`0x75` PSTextureModes, `0x76` VertexBlend, `0x77` FogColor, ...), so this is the
XDK enum.

**Resource header** (XDK layout, confirmed by AddRef `0x103A90`, Release `0x103AD0`,
IsBusy `0x103B20`, GetType `0x1038D0`, SetTexture and the Create* functions):

| Offset | Field |
|---|---|
| +0x00 | `Common`: refcount `& 0xFFFF`; type `& 0x70000` (VB 0, IB 1, palette 3, texture 4, surface 5); internal refs `& 0x780000` in steps of `0x80000` (SetTexture/SetStreamSource/SetIndices take one); `0x01000000` = created by D3D |
| +0x04 | `Data`: physical address. Create* store `VA & 0x7FFFFFF`; guest VA = `XBOX_CONTIG_BASE \| (Data & 0x0FFFFFFF)`, the same round trip the runtime already uses for DMA_PUT |
| +0x08 | `Lock`: GPU fence value for IsBusy |
| +0x0C | `Format` (texture/surface): format byte at +0xD, log2 dimensions and level count in the nibbles |
| +0x10 | `Size` (linear surfaces only: pitch/width/height, 12 bits each) |
| +0x14 | `Parent` (surface to texture), recursed by AddRef/Release |

Vertex buffers are a 12-byte heap header plus contiguous data. Index buffers are one heap
block of header plus data in system memory; DrawIndexedVertices copies the indices inline
into the push buffer, so there is no GPU index buffer at all. A vertex-shader handle is
`object | 1`; a pixel-shader handle is a pointer to a 0xFC-byte block holding the
`D3DPIXELSHADERDEF`.

**Game side.** The game talks to D3D through a thin wrapper cluster (`0x39810`-`0x3B0C0`
and `0x3CDE0`-`0x3D000`) guarded by critical section `0x4EA440`. Three details matter
for the design:

- **The render-state dispatcher `0x3A830` (21 callers) always stores the value into
  `D3D__RenderState[State]`.** Simple states (< 82) also go through
  `SetRenderState_Simple` (`0xFD830`, fastcall, with the NV2A method from table
  `0x15AD48`). Deferred states (82-116) set dirty bits from table `0x15AB60`. Complex
  states (>= 117) call their setter, and each setter also stores the value into its RS slot.
- **The texture-stage dispatcher `0x3AC60` stores every value into
  `D3D__TextureState[stage][type]`** and calls the four complex setters for types 22-30.
- **The SetTexture wrapper `0x3AAE0` has a fast path.** If the new texture has the same
  description as the stage's previous one, it skips `SetTexture` and pushes
  `TEXTURE_OFFSET`/`FORMAT` directly through `0xFEC00` (fastcall 3-dword push). Then
  `device+0xA78[stage]` is stale; the game's own table `0x1E8CC0 + 20*stage` gets the
  header afterwards.
- **No game code touches the push-buffer cursor or `[0x10EBF0]`.** D3DX reads `[0x10EC0C]`
  (current index data) and nothing else of D3D's.

## 2. Tier B: the object/refcount model

### Options

| | Guest memory holds | Result |
|---|---|---|
| **R: replace** (18.20) | host objects in the native slots | Crashes. Unbridged code reads native offsets (18.20, `sub_00100EA0`). Also, game code reads header fields directly: the SetTexture wrapper reads `tex+4` and `tex+0xC`. |
| **H: handle layer** | tokens resolved by the bridge | Same failure. Nothing routes field reads through an accessor that could resolve a token. |
| **M: mirror** (proposed) | the native structures, unchanged | Native code never sees a host object. The host keeps a cache on the side. |

### Proposed design (M)

1. **Guest memory is unchanged and authoritative.** Native CreateDevice, Create*,
   AddRef/Release, Set* and state blocks keep running as lifted code, and refcounts
   exist only natively. The push buffer is still written; with the executor off, only the
   runtime's fence mirror consumes it (step 1 got to the splash that way).
2. **One host device, host-side only.** The CreateDevice wrapper calls
   `sub_000FD6E0_gen` first. If that succeeds, it creates the `xbox_d3d8` device (window,
   640x480 from the same `D3DPRESENT_PARAMETERS`, which 18.20 already checked field by
   field) and keeps it in a `twoc` static. Nothing is written to `[0x1E3C90]` or
   `[0x10EBF0]`.
3. **Host caches keyed by guest header VA**, each entry holding a signature
   `{Common & 0x70000, Data, Format, Size}`:
   - **Textures** (2D, cube) and palettes. Created at first bind from the header (format
     byte, log2 dimensions, levels) by `CreateTexture` + `LockRect` + copying the raw
     Xbox bytes + `UnlockRect`. `xbox_d3d8` unswizzles at upload (`tex_UnlockRect`).
   - **Render targets.** A surface whose `Data` matches the native back or front buffer
     maps to the host back buffer. A surface with a `Parent` maps to a render-target-capable
     host texture keyed by that parent.
   - **Vertex shaders** (key: native object VA and a hash of its microcode) and **pixel
     shaders** (key: a hash of RS[0..56]).
   - **A second index by `Data`** for the fast path in `0xFEC00`, which only knows
     `Data` and `Format`.
4. **Invalidation.** A signature mismatch at lookup rebuilds the entry. The LockRect
   wrappers mark an entry dirty. `MmFreeContiguousMemory` is intercepted at its kernel
   thunk (the same technique as `vm_NtFreeVirtualMemory`) and drops entries whose `Data`
   is in the freed range, so a reused address can't show a stale texture after a level
   load. A debug switch hashes texture contents on every bind to catch missed dirties.
   Refcounts never drive host lifetime.
5. **No host vertex or index buffers.** Each draw copies the vertex range it uses from
   guest memory into `xbox_d3d8`'s UP ring buffer (`DrawPrimitiveUP` /
   `DrawIndexedPrimitiveUP`). Several streams are interleaved on the CPU into one. That
   removes VB/IB lifetime, Lock tracking and the stream-0-only limit of `xbox_d3d8`'s
   `SetStreamSource` in one move. Index data is already a system-memory pointer
   (the `pIdx` argument of DrawIndexedVertices).
6. **State is scraped at each draw.** Read RS[146], TSS[4][32], transforms, viewport,
   material, lights, the stream table, the current shaders, RT/Z and textures (plus any
   fast-path override) and palettes. Diff against what was last applied, translate XDK
   index and value to PC D3D8, and call the host setters. Vertex-shader constants come
   from the SetVertexShaderConstant wrapper, because the native shadow copy is skipped when
   `device+8 & 0x10`.

### Why M

- **One representation**, so native and bridged code agree by construction, and 18.20's
  crash can't happen: the bridge writes no guest memory.
- **Incremental.** Each wrapper is `sub_X_gen(); if (bridge_on) mirror();`, gated by an
  environment variable, so the default run stays byte-for-byte today's.
- **Nothing in D3D's 256-function closure has to be traced**: the NV2A state compiler,
  z-cull, AA, surface setup.
- **It handles the game's direct writes** to RS/TSS and the texture fast path without
  special cases in the setters.
- **Cost**: native D3D still builds push buffers. That is cheap; the executor was the
  expensive part (18.28), and it gets turned off. Cache invalidation is a correctness risk,
  covered by point 4. CPU reads of rendered surfaces need a readback (section 7).

**Threading.** Host calls happen only inside the wrapped draw, Clear and Swap functions,
on the thread that calls them. The Lock wrappers only set flags. The stage-0 census checks
that one thread issues every draw.

## 3. The setter family and the rest, mapped to `xbox_d3d8`

The full per-entry table (tier, sites, XDK name, NV097 methods emitted, bridge action) is
at the end. How it breaks down:

| Action | Entries | Call sites | Meaning |
|---|---|---|---|
| **W** (wrap: native first, then mirror) | 15 | 29 | CreateDevice, Clear, Swap, DrawVerticesUP, DrawVertices, DrawIndexedVertices, Begin, SetVertexData2f, SetVertexData4f, End, CopyRects, SetVertexShaderConstant, 2 LockRects, the `0xFEC00` fast path |
| **N** (native only, read at draw) | 57 | 91 | all 29 complex RS setters, 4 TSS setters, SetRenderState_Simple, SetTexture/Transform/Viewport/Light/Material/Stream/Indices/Shaders/RenderTarget, state blocks |
| **Q** (native only, host never needs it) | 28 | 127 | caps, AddRef/Release, Create* for VB/IB/palette, Lock of VB, GetDesc/GetSurfaceLevel |

**About "36 Tier C".** Re-deriving step 19's own rule (the only internal call is
`0x103740`) gives **25** entries. The full **setter family** is **37**: 29 complex RS
setters, 4 TSS setters, SetRenderState_Simple, the fast-path push and the two
SetVertexData. The first table below covers all 37, which includes every entry either
definition could mean.

**The NV097 method table.** `nv2a_regs.h` already has 214 `NV097_*` method defines
(xemu's), so no table has to be built. Every header a setter writes was decoded against
it (see the tables). Under design M, the setters need no per-method bridge, because
their values are also in RS/TSS. The method table is needed in exactly two places:
- **`0xFEC00`** (the texture fast path): decode `0x1B00 + 0x40*stage`
  (`SET_TEXTURE_OFFSET`, n=2) to get stage, `Data` and `Format`.
- **An optional consistency check in the census**: compare what the host believes against
  what native pushed.

**The translation table (new `twoc` code; the real work in the state path).** `xbox_d3d8`
uses **PC D3D8 numbering and values** (`D3DRS_ZENABLE = 7`, `D3DCMP_NEVER = 1`,
`D3DTS_WORLD = 256`). The XDK uses its own indices and GL-style values. Confirmed in the
listings: ShadowFunc subtracts `0x200` from a compare value; CullMode compares with
`0x900`/`0x901`; FillMode writes `0x1B00`-`0x1B02` straight to NV2A; TSS TexCoordIndex is
index 28, BorderColor 29, ColorKeyColor 30, BumpEnv 22-27. Needed:

| Map | Size | Notes |
|---|---|---|
| RS index: XDK 0-145 to PC D3DRS | 146 | 0-56 pixel shader to PC 200-253; the ignored ones (z-cull, ROP, MultiSample*, LineWidth, Dxt1, Yuv, LogicOp, ShadowFunc) are logged once if they're ever non-default |
| TSS index: XDK 0-30 to PC D3DTSS | 31 | XDK order: ADDRESSU/V/W, MAG/MIN/MIPFILTER, ... COLOROP = 12, ... TEXCOORDINDEX = 28 |
| Transform index | 10 | XDK VIEW 0, PROJECTION 1, TEXTURE0-3 2-5, WORLD 6.. to PC 2, 3, 16-19, 256.. (PROJECTION = 1 is confirmed by SetTransform's special case) |
| Values | ~10 domains | compare (`0x200+n` to `n+1`), blend factor and op (GL enums), stencil op (GL), cull, fill, shade (`0x1D00`/`0x1D01`), colour-write mask (XDK bit layout differs), primitive type (XDK 9 QUADSTRIP and 10 POLYGON converted on the CPU) |

Cross-check the value domains against a reference before trusting them.
Cxbx-Reloaded's XDK type conversion tables are the obvious one. My recollection of the
GL constants is not a source.

## 4. The 10 Tier D-big entries, identified and ordered

| Order | Addr | XDK function | Step 19's guess | Bridge |
|---|---|---|---|---|
| 1 | `0x100C40` | `D3DDevice_Swap(Flags)` | Reset | **W**: host present after native. Its kernel calls (`AvSetDisplayMode` and so on) are the native first-swap/mode path, which keeps running. |
| 2 | `0x1019C0` | `D3DDevice_DrawVerticesUP(Prim, Count, pData, Stride)` | draw (sibling) | **W**: most likely the splash path, since `INLINE_ARRAY` is the one thing the executor draws correctly (18.19) |
| 3 | `0x0FEF20` | `D3DDevice_SetRenderTarget(pRT, pZ)` | shared resource-release helper | **N**: read `device+0x2070`/`+0x2074` at draw; the release was of the old surfaces |
| 4 | `0x101B20` | `D3DDevice_DrawVertices(Prim, Start, Count)` | shader state | **W**: stream fetch |
| 5 | `0x101BC0` | `D3DDevice_DrawIndexedVertices(Prim, Count, pIdx)` | draw | **W**: the main 3D path (the game's `0x3B010` and D3DX's mesh draw `0x11199E`) |
| 6 | `0x101EC0` | `D3DDevice_Begin(Prim)` (+ `End` `0x101F00`) | shader-related | **W**: immediate-mode accumulator (callers `0x38250`, `0x9F480`, `0x9F870`) |
| 7 | `0x101705` | `D3DDevice_ApplyStateBlock(Token)` | resource creation | **N**: its callees update native shadows |
| 8 | `0x0FD7A0` | `SetRenderState_MultiSampleMode` | Create/Lock pair | **N**: stores RS[135] and re-applies SetRenderTarget natively |
| 9 | `0x0FD7D0` | `SetRenderState_MultiSampleRenderTargetMode` | Create/Lock pair | **N**: same, RS[136] |
| 10 | `0x0FEC50` | `D3DDevice_Release` | teardown | **N**: host teardown only if the native refcount reaches its last reference (not seen in any run) |

The order follows what each test stage needs (section 5), not closure size.

## 5. Testing in stages

Every stage runs with `RECOMP_D3D_BRIDGE=1` (new, off by default), unattended through
`run_twoc.ps1 -FastTitle -AutoStart` and `RECOMP_DEV_INPUT_FILE`. With the variable unset,
no wrapper does anything beyond calling `_gen`.

| Stage | Target | Host work | Reference and pass check |
|---|---|---|---|
| **0: census** | whole run: splash, title, intro, hub | log-only wrappers, no host device. Per-entry call counts per frame; primitive types, FVFs and shader handles, texture formats and sizes, which RS/TSS indices ever change, render targets other than the back buffer, CopyRects/LockRect use, draws by thread | Confirms the identities and the "probable" rows, and tells which draw path the splash uses. Pass = the run is identical to today (same file opens and timings) |
| **1: host frame** | any | CreateDevice mirror, Clear and Swap; host back buffer dumped to BMP on the same cadence as `-DumpDir` | Window shows the clear colour and frames advance with the game. Executor still on, so both views exist |
| **2: splash** (known-good) | splash and logos | DrawVerticesUP (if the census confirms it), 2D textures, state translator v1 (z, blend, alpha test, cull, fill, TSS colour/alpha ops, address, filter) | Executor dumps of the same screen. A new `twoc/tools/diag/fbcompare.py`: pass = at least 99% of pixels within +/-8 per channel. A static splash lets time-based dumps line up; if it animates, dump both sides on the native swap counter (`device+0x2AC4`) instead |
| **3: title and menu** | title animation, main menu | the full RS/TSS/value tables, P8 palettes, the `0xFEC00` fast path, DrawVertices, immediate mode (if the census shows it) | Executor frames it gets right (whatever 2D it gets right), plus eyeballing the menu text |
| **4: intro cutscene** (first 3D) | ~45 s in | transforms, viewport, DrawIndexedVertices, vertex shaders from native objects, VS constants, pixel shaders from RS[0..56], fixed-function lights if used | **No local reference exists**: the executor's 3D is wrong (18.14). Needs xemu captures of the same shots (decision 3) or, at minimum, recognisable geometry and no NaN or off-viewport vertices |
| **5: hub** | walk Crash with `crashpos.py` input | render-to-texture, CopyRects, LockRect readback, cube maps, ZBias, stencil | xemu captures; Crash visible and moving in step with `0x8F5120+0x6C` |
| **6: cut-over** | all | executor off whenever the bridge is on; measure fps (`fpsrun.py`/`fps.py`) | Regression: same file opens and exit 3; host dumps from stages 2-5 kept as golden images |

This is the staged test of what used to be an all-or-nothing swap: the swap no longer
has to be all-or-nothing. If a later stage finds a function that can't be mirrored (for
example the CPU reads back rendered pixels in a way the readback can't serve), only that
function moves to replace mode, and the bridge owns it end to end.

## 6. Known `xbox_d3d8` gaps that matter for Crash

Found by reading `d3d8_device.c`, `d3d8_vsh.c`, `d3d8_combiners.c` and `d3d8_resources.c`:

| Gap | Where | Work-around inside `twoc/` |
|---|---|---|
| `SetStreamSource` supports stream 0 only | `d3d8_device.c:641` | CPU interleave into one UP stream (design point 5) |
| `CreateVertexShader` ignores the declaration; the input layout is the used registers in ascending order, with fixed formats per register | `d3d8_device.c:1287`, `d3d8_vsh.c:1086` | Repack vertices on the CPU into exactly that layout. Needs the shader's `inputs_read` mask: a thin read-only accessor (allowed by 18.9) or recompute it from the microcode |
| No `CreatePixelShader`; combiners read the `D3DRS_PS*` states plus a nonzero token | `d3d8_combiners.c:970` | Copy RS[0..56] into host RS 200-253, token = the guest handle |
| `SetPixelShaderConstant` is a stub | `d3d8_device.c` | Constants arrive through RS PSCONSTANT* anyway |
| No ZBias state, no CopyRects, no immediate mode, no state blocks | device vtable | ZBias: log at first. CopyRects: D3D11 copy through the existing non-static `d3d8_GetD3D11Context()`. Immediate mode and state blocks: handled in `twoc` (native state blocks already work) |
| Fixed-function lighting is "Partial"; Xbox pixel-shader states were "Stubbed" in the README | README | Unknown until stage 4 |
| XDK QUADSTRIP/POLYGON primitive types | enum | Converted on the CPU |

If a translator bug turns up (VSH microcode, combiners), no `twoc` work-around exists, and
that's decision 2.

## 7. Risks and open checks

- **CPU reads of rendered pixels.** With the executor off, guest memory holds no rendered
  image. Anything that locks the back buffer or a render target, or CopyRects from it
  (callers `0x39970` and `0x39E00`, purpose not traced), needs a host readback into guest
  memory. Stage 0 counts these.
- **`xbox_d3d8` has one global device state.** The runtime's video pump
  (`src/video/video_pump.c`, started from `kernel_bridge.c` when a title opens a movie
  file) creates its own device on its own thread. Crash's disc has no movie files and no
  log has a `[VIDEO]` line, so this doesn't happen today. If it ever did, the bridge and
  the pump would both own the device.
- **Stale textures** when memory is reused without Lock or free. Covered by the free hook
  and the signature check; the debug hash switch catches the rest.
- **The vertex-attribute slot layout of the native vertex-shader object** (`+0x10 + 16*i`,
  per register) is read from CreateVertexShader's initialisation code, not traced
  end to end. It has to be confirmed in stage 4. The FVF path builds the same kind of
  object at `0x10F458`, so one reader covers both.
- **Present timing.** Native Swap still waits for vblank, so the host should present
  without vsync to avoid waiting twice.
- **Probable identities** that the census has to confirm: `0xFD680` (returns 1),
  `0xFEC90` (vblank or swap callback), `0x1012CC`/`0x10191E` (Begin/EndStateBlock), and
  `0xFDC90` (Dxt1NoiseEnable by its RS slot, although it writes NV097 `0x1D8C`).

## 8. Decisions needed

1. **Adopt the mirror design (M)** instead of the replace design from 18.20/18.21.
   Recommended: it's incremental, needs no toolkit change for the wrappers, and keeps the
   working game untouched by default.
2. **`xbox_d3d8` changes under the section-18 rule.** Recommended: work around gaps in
   `twoc/` first (section 6; everything listed has a work-around), plus thin read-only
   accessors under the 18.9 refinement. Add a new refinement only when a translator bug
   leaves no work-around: local `xbox_d3d8` commits, never pushed, recorded with hash.
   No path the game runs today uses `xbox_d3d8` (its only other user is the runtime's
   video pump, which never starts for Crash), so a fix there can't change current behaviour.
3. **3D reference frames.** Stages 4-5 have no known-good image. If you have xemu and can
   capture a few shots (the intro's first shots, the hub spawn), those become the reference.
   Without them, the check falls back to "looks right".

## 9. Session plan

Each session is about one of the recent step sizes. Default behaviour stays unchanged in
every session.

| Session | Work | Exit criterion |
|---|---|---|
| **S1 (step 30): wrap plumbing + census** | the 15 `_gen` wrappers, log-only; census report; confirm the probable identities; one run with `RECOMP_PB_EXEC` unset to confirm the fence mirror alone still gets to the menu | identical run with the bridge off; census over splash, title, intro and hub |
| **S2: host device + frame** | CreateDevice mirror (visible window), Clear, Swap, host BMP dumps, `fbcompare.py` | host window shows cleared frames in step with the game |
| **S3: splash** | texture cache (2D swizzled, invalidation), state translator v1, DrawVerticesUP | splash and logos pass the 99%/+-8 compare |
| **S4: title and menu** | complete RS/TSS/value tables (cross-checked), palettes, fast path, DrawVertices, immediate mode | menu readable on the host window; executor-matching frames pass |
| **S5: 3D, part 1** | transforms, viewport, DrawIndexedVertices, vertex-shader mirror with CPU repack, VS constants, pixel shaders, lights | intro cutscene recognisable (or matches xemu captures) |
| **S6: 3D, part 2** | render targets and render-to-texture, CopyRects, LockRect readback, cube maps, stencil, ZBias | hub renders; Crash visible and moving |
| **S7: cut-over + perf** | executor off whenever the bridge is on; fps; golden images | hub fps measured against today's ~10; regression suite |
| **S8: buffer** | translator bugs (decision 2), missed states, polish | the intro and hub look right |

Compared with 18.21's "10-15x CreateDevice, all landing at once", this is about 8
sessions, with something testable at the end of each one.

## Files changed

None in the repo during the analysis. The notes file gets entry 18.31 and an updated
"Start here tomorrow".

---

# Appendix: all 100 entries

Legend: **W** = wrap (native `sub_X_gen` first, then mirror to `xbox_d3d8`). **N** = native
only; the bridge reads the effect from native shadows at draw time. **Q** = native only;
the host never needs it. "Tier19" is step 19's bucket. "new" is the tail-jump entry it
missed. RS/TSS indices are XDK numbering.

### Setter family (the "Tier C" group): 37 entries

| Addr | Tier19 | Sites | XDK function | NV097 methods it emits | Bridge |
|---|---|---|---|---|---|
| `0FD7A0` | D-big | 1 | SetRenderState_MultiSampleMode | none directly (calls SetRenderTarget) | N: none (re-applies SetRenderTarget natively) |
| `0FD7D0` | D-big | 1 | SetRenderState_MultiSampleRenderTargetMode | none directly (calls SetRenderTarget) | N: none (as above) |
| `0FD800` | D-small | 1 | SetRenderState_PSTextureModes | none (dirty flag only) | N: RS[117] -> D3DRS_PSTEXTUREMODES |
| `0FD830` | C | 1 | SetRenderState_Simple(method,value) __fastcall | any (method passed in ECX from table 0x15AD48[State]) | N: value also stored in RS[State] by game dispatcher 0x3A830 |
| `0FDA00` | C | 1 | SetRenderState_EdgeAntialias | SET_LINE_SMOOTH_ENABLE | N: RS -> D3DRS_EDGEANTIALIAS (no-op in D3D11) |
| `0FDA40` | C | 1 | SetRenderState_ShadowFunc | SET_SHADOW_DEPTH_FUNC | N: no host equivalent; log if used |
| `0FDA80` | C | 1 | SetRenderState_FogColor | SET_FOG_COLOR | N: RS -> D3DRS_FOGCOLOR |
| `0FDAD0` | C | 1 | SetRenderState_CullMode | SET_CULL_FACE, SET_CULL_FACE_ENABLE | N: RS -> D3DRS_CULLMODE (0/0x900/0x901 -> 1/2/3) |
| `0FDB40` | C | 1 | SetRenderState_FrontFace | SET_FRONT_FACE | N: folded into CULLMODE |
| `0FDB80` | C | 1 | SetRenderState_NormalizeNormals | SET_NORMALIZATION_ENABLE | N: RS -> D3DRS_NORMALIZENORMALS |
| `0FDBC0` | C | 1 | SetRenderState_TextureFactor | SET_COMBINER_FACTOR0 | N: RS -> D3DRS_TEXTUREFACTOR |
| `0FDC20` | D-small | 1 | SetRenderState_LineWidth | LINE_WIDTH (0x380) | N: ignored (D3D11 lines are 1 px) |
| `0FDC90` | C | 1 | SetRenderState_Dxt1NoiseEnable | WAIT_FOR_IDLE, 0x1D8C, NO_OPERATION | N: ignored |
| `0FDD10` | D-small | 1 | SetRenderState_ZBias | via SetRenderState_Simple: POLYGON_OFFSET_* (RS 77-81) | N: RS -> rasterizer DepthBias (xbox_d3d8 gap) |
| `0FDD90` | C | 1 | SetRenderState_LogicOp | SET_LOGIC_OP_ENABLE | N: ignored unless nonzero (log) |
| `0FDDF0` | C | 2 | SetRenderState_FillMode | SET_FRONT_POLYGON_MODE | N: RS -> D3DRS_FILLMODE (0x1B00-2 -> 1-3) |
| `0FDE40` | C | 1 | SetRenderState_BackFillMode | SET_FRONT_POLYGON_MODE | N: ignored |
| `0FDEA0` | C | 1 | SetRenderState_TwoSidedLighting | SET_FRONT_POLYGON_MODE | N: ignored at first |
| `0FDF10` | C | 1 | SetRenderState_VertexBlend | SET_SKIN_MODE | N: RS -> D3DRS_VERTEXBLEND |
| `0FDF60` | C | 1 | SetTextureState_TexCoordIndex(Stage,Value) | TEXGEN_S/T/R (0x3C0+16*stage, n=3), 0x1964+4*stage | N: TSS[s][28] -> D3DTSS_TEXCOORDINDEX |
| `0FE080` | C | 1 | SetTextureState_BumpEnv(Stage,Type,Value) | TEXTURE_SET_BUMP_ENV_MAT/SCALE/OFFSET (0x1B28..0x1B3C + 0x40*stage) | N: TSS[s][22..27] -> D3DTSS_BUMPENVMAT* |
| `0FE0F0` | C | 1 | SetTextureState_BorderColor(Stage,Value) | TEXTURE_BORDER_COLOR (0x1B24 + 0x40*stage) | N: TSS[s][29] -> D3DTSS_BORDERCOLOR |
| `0FE130` | C | 1 | SetTextureState_ColorKeyColor(Stage,Value) | COLOR_KEY_COLOR (0xAE0 + 4*stage) | N: TSS[s][30]; no host equivalent |
| `0FE5C0` | D-small | 1 | SetRenderState_ZEnable | SET_DEPTH_TEST_ENABLE, SET_ZMIN_MAX_CONTROL | N: RS -> D3DRS_ZENABLE |
| `0FE660` | C | 1 | SetRenderState_StencilEnable | 0x1D84 (no define in nv2a_regs.h; z-cull/occlusion control), SET_STENCIL_TEST_ENABLE | N: RS -> D3DRS_STENCILENABLE |
| `0FE6F0` | C | 1 | SetRenderState_StencilFail | 0x1D84 (no define in nv2a_regs.h; z-cull/occlusion control), SET_STENCIL_OP_FAIL | N: RS -> D3DRS_STENCILFAIL (GL op -> D3DSTENCILOP) |
| `0FE760` | D-small | 1 | SetRenderState_YuvEnable | via helper 0xFE490 (combiner/surface setup) | N: ignored unless set (log) |
| `0FE790` | C | 1 | SetRenderState_OcclusionCullEnable | 0x1D84 (no define in nv2a_regs.h; z-cull/occlusion control) | N: ignored (NV2A z-cull) |
| `0FE800` | C | 1 | SetRenderState_StencilCullEnable | 0x1D84 (no define in nv2a_regs.h; z-cull/occlusion control) | N: ignored (NV2A z-cull) |
| `0FE870` | D-small | 1 | SetRenderState_RopZCmpAlwaysRead | via helper 0xFE4F0 | N: ignored |
| `0FE890` | D-small | 1 | SetRenderState_RopZRead | via helper 0xFE4F0 | N: ignored |
| `0FE8B0` | D-small | 1 | SetRenderState_DoNotCullUncompressed | via helper 0xFE4F0 | N: ignored |
| `0FE8D0` | D-small | 1 | SetRenderState_MultiSampleAntiAlias | SET_ANTI_ALIASING_CONTROL | N: ignored |
| `0FE930` | D-small | 1 | SetRenderState_MultiSampleMask | SET_ANTI_ALIASING_CONTROL | N: ignored |
| `0FEC00` | C | 1 | push 3 dwords __fastcall (game SetTexture fast path) | any (game passes TEXTURE_OFFSET+FORMAT, n=2, 0x1B00+0x40*stage) | W: decode TEXTURE_OFFSET(stage): stage texture = (Data,Format) |
| `101E20` | C | 1 | D3DDevice_SetVertexData2f(Reg,a,b) | VERTEX_DATA2F_M (0x1880 + 8*reg) | W: immediate-mode accumulator |
| `101E60` | C | 1 | D3DDevice_SetVertexData4f(Reg,a,b,c,d) | VERTEX_DATA4F_M (0x1A00 + 16*reg) or VERTEX4F (0x1518) for reg -1 | W: immediate-mode accumulator |

### Everything else: 63 entries

| Addr | Tier19 | Sites | XDK function | NV097 methods (direct) | Bridge |
|---|---|---|---|---|---|
| `0FD680` | D-small | 1 | Direct3D_* query returning 1 (likely GetAdapterModeCount-style) |  | Q: - |
| `0FD690` | D-small | 1 | Direct3D_GetDeviceCaps(Adapter,DevType,pCaps) |  | Q: - |
| `0FD6C0` | D-small | 1 | Direct3D_SetPushBufferSize(Size,KickOff) |  | Q: - |
| `0FD6E0` | A | 1 | Direct3D_CreateDevice |  | W: xbox_Direct3DCreate8 + CreateDevice; host device stays host-side |
| `0FE990` | D-small | 3 | D3DDevice_GetDirect3D (writes 1) |  | Q: - |
| `0FE9A0` | D-small | 4 | D3DDevice_GetDeviceCaps (copies 0xD4 bytes) |  | Q: - |
| `0FE9C0` | D-small | 6 | D3DDevice_CreateTexture |  | N: host texture made lazily from the header at first bind |
| `0FE9F0` | D-small | 1 | D3DDevice_CreateCubeTexture |  | N: lazy -> CreateCubeTexture |
| `0FEA20` | D-small | 1 | D3DDevice_SetTransform(State,pMatrix) |  | N: device+0x750+State*64 -> SetTransform (index remap) |
| `0FEB30` | D-small | 1 | D3DDevice_GetViewport |  | Q: - |
| `0FEB50` | D-small | 1 | D3DDevice_SetMaterial |  | N: device+0x9F0 -> SetMaterial |
| `0FEC30` | D-small | 3 | D3DDevice_AddRef |  | Q: - |
| `0FEC50` | D-big | 2 | D3DDevice_Release |  | N: host teardown only at the real last ref |
| `0FEC90` | D-small | 1 | SetVerticalBlankCallback or SetSwapCallback |  | Q: - |
| `0FED90` | D-small | 1 | D3DDevice_InsertFence |  | Q: - |
| `0FEE80` | D-small | 1 | D3DDevice_GetDisplayMode |  | Q: - |
| `0FEF20` | D-big | 4 | D3DDevice_SetRenderTarget(pRT,pZ) | FLIP_STALL, SET_DEPTH_TEST_ENABLE, SET_STENCIL_TEST_ENABLE, SET_SURFACE_CLIP_HORIZONTAL, SET_SURFACE_COLOR_OFFSET, SET_SURFACE_FORMAT, SET_SURFACE_PITCH, SET_SURFACE_ZETA_OFFSET | N: device+0x2070/0x2074 -> host RT |
| `0FF450` | D-small | 3 | D3DDevice_GetBackBuffer |  | Q: - |
| `0FF580` | D-small | 2 | D3DDevice_CopyRects |  | W: host copy (xbox_d3d8 gap) |
| `0FF830` | D-small | 2 | D3DDevice_GetDepthStencilSurface |  | Q: - |
| `0FF860` | D-small | 2 | D3DDevice_SetViewport |  | N: device+0x9D0 -> SetViewport |
| `0FF9C0` | D-small | 1 | D3DDevice_SetLight |  | N: device+0x390 array -> SetLight |
| `0FFBA0` | D-small | 1 | D3DDevice_LightEnable |  | N: light +0x68 flag -> LightEnable |
| `0FFC90` | D-small | 3 | D3DDevice_SetTexture(Stage,pTex) |  | N: device+0xA78[stage] -> host SetTexture |
| `0FFE10` | D-small | 1 | D3DDevice_SetPalette(Stage,pPal) |  | N: device+0xA88[stage] -> host SetPalette |
| `0FFEA0` | D-small | 2 | D3DDevice_SetIndices(pIB,Base) |  | N: [0x10EC0C], device+0x1C read at draw |
| `100C40` | D-big | 1 | D3DDevice_Swap(Flags) |  | W: host Swap after native |
| `100D00` | D-small | 2 | D3DDevice_CreateIndexBuffer |  | Q: no host IB (indices via ...UP) |
| `100D40` | D-small | 2 | D3DPalette_Lock |  | N: palette re-uploaded on content change |
| `100D70` | D-small | 4 | D3DDevice_CreateVertexBuffer |  | Q: no host VB (vertices copied per draw) |
| `100DD0` | B | 24 | D3DVertexBuffer_Lock | 0x1710 (BREAK_VERTEX_BUFFER_CACHE; no define in nv2a_regs.h), only without D3DLOCK_NOFLUSH | Q: - |
| `100E30` | D-small | 2 | D3DDevice_CreatePalette |  | Q: - |
| `100EA0` | D-small | 5 | D3DDevice_Clear | SET_SURFACE_FORMAT | W: host Clear |
| `1012CC` | D-small | 1 | D3DDevice_BeginStateBlock (probable) |  | N: - |
| `101705` | D-big | 1 | D3DDevice_ApplyStateBlock |  | N: callees update native shadows |
| `10188D` | D-small | 1 | D3DDevice_DeleteStateBlock |  | Q: - |
| `10191E` | D-small | 1 | D3DDevice_EndStateBlock (probable) |  | N: - |
| `1019C0` | D-big | 2 | D3DDevice_DrawVerticesUP(Prim,Count,pData,Stride) | SET_BEGIN_END, INLINE_ARRAY (0x1818, computed) | W: scrape + DrawPrimitiveUP |
| `101B20` | D-big | 1 | D3DDevice_DrawVertices(Prim,Start,Count) | SET_BEGIN_END, DRAW_ARRAYS (0x1810, computed) | W: scrape + stream fetch + DrawPrimitiveUP |
| `101BC0` | D-big | 3 | D3DDevice_DrawIndexedVertices(Prim,Count,pIdx) | SET_BEGIN_END, ARRAY_ELEMENT16 (0x1800, non-incrementing, computed), ARRAY_ELEMENT32 for an odd last index | W: scrape + fetch + DrawIndexedPrimitiveUP |
| `101EC0` | D-big | 1 | D3DDevice_Begin(Prim) | SET_BEGIN_END | W: immediate-mode start |
| `101F00` | new | 1 | D3DDevice_End (tail jmp from 0x3AE30) | SET_BEGIN_END | W: scrape + DrawPrimitiveUP |
| `102440` | B | 12 | D3DDevice_CreateVertexShader |  | N: host shader built lazily from native object |
| `102580` | D-small | 6 | D3DDevice_SetStreamSource |  | N: table 0x10F280 read at draw |
| `1026F0` | D-small | 1 | D3DDevice_SetShaderConstantMode | SET_FOG_PLANE, SET_TRANSFORM_CONSTANT, SET_TRANSFORM_CONSTANT_LOAD | N: device+0x2018 read at draw |
| `102830` | D-small | 1 | D3DDevice_DeleteVertexShader |  | N: host cache entry dropped lazily |
| `102940` | D-small | 4 | D3DDevice_SetVertexShader | SET_TRANSFORM_EXECUTION_MODE, SET_TRANSFORM_PROGRAM_START, VERTEX_DATA4UB defaults 0x194C-0x1960 (diffuse/specular) | N: device+0x380/0x384 read at draw |
| `102AA0` | D-small | 1 | D3DDevice_SetVertexShaderConstant | SET_TRANSFORM_CONSTANT, SET_TRANSFORM_CONSTANT_LOAD | W: host SetVertexShaderConstant |
| `102B40` | D-small | 2 | D3DDevice_CreatePixelShader |  | Q: - |
| `102B90` | D-small | 1 | D3DDevice_DeletePixelShader |  | Q: - |
| `102BB0` | D-small | 1 | D3DDevice_SetPixelShader | SET_COMBINER_ALPHA_ICW, SET_COMBINER_COLOR_OCW, SET_COMBINER_FACTOR0, SET_COMBINER_SPECULAR_FOG_CW0, SET_SHADER_CLIP_PLANE_MODE, SET_SHADER_OTHER_STAGE_INPUT, SET_SHADER_STAGE_PROGRAM+0x4, SET_SPECULAR_FOG_FACTOR | N: def copied into RS[0..56] -> host RS 200-253 |
| `102D80` | D-small | 1 | D3DDevice_SetPixelShaderConstant | SET_EYE_VECTOR | N: lands in RS PSCONSTANT* |
| `1038D0` | D-small | 1 | D3DResource_GetType |  | Q: - |
| `103A90` | B | 11 | D3DResource_AddRef |  | Q: - |
| `103AD0` | B | 34 | D3DResource_Release |  | Q: host cache validated by signature |
| `103B20` | D-small | 2 | D3DResource_IsBusy |  | Q: - |
| `103C20` | D-small | 4 | D3DTexture_GetLevelDesc |  | Q: - |
| `103C30` | D-small | 6 | D3DTexture_GetSurfaceLevel |  | Q: - |
| `103C80` | D-small | 4 | D3DTexture_LockRect |  | W: mark host texture dirty |
| `103CB0` | D-small | 4 | D3DCubeTexture_GetCubeMapSurface |  | Q: - |
| `103DD0` | D-small | 3 | D3DBaseTexture_GetLevelCount |  | Q: - |
| `103DE0` | D-small | 4 | D3DSurface_GetDesc |  | Q: - |
| `103E00` | D-small | 4 | D3DSurface_LockRect |  | W: mark parent dirty; readback if host RT |
