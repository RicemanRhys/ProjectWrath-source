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
