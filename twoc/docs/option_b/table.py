import d3dlib as L, json, re, collections
E = json.load(open("entries.json")); F = L.load_funcs(); A = L.parse_asm("D3D")
E["0x101f00"] = dict(sites=["0x3ae30"], callers=["0x3ae30"], sections={".text": 1})  # tail-jmp entry
A_ = {0xfd6e0}
B_ = {0x103ad0, 0x100dd0, 0x102440, 0x103a90}
DBIG = [0x100c40, 0x101bc0, 0x1019c0, 0x101b20, 0x101ec0, 0x101705, 0xfec50, 0xfd7a0, 0xfd7d0, 0xfef20]

def tier(a):
    if a in A_: return "A"
    if a in B_: return "B"
    if a in DBIG: return "D-big"
    if a == 0x101f00: return "new"
    c = [int(x, 16) for x in F[a]['calls_to']]
    if c == [0x103740]: return "C"
    return "D-small"

# dispositions: N = native only, state scraped at draw; W = wrap (native first, then mirror to xbox_d3d8);
#               Q = native only, host unaffected (query/bookkeeping)
ID = {
0xfd680: ("Direct3D_* query returning 1 (likely GetAdapterModeCount-style)", "Q", "-"),
0xfd690: ("Direct3D_GetDeviceCaps(Adapter,DevType,pCaps)", "Q", "-"),
0xfd6c0: ("Direct3D_SetPushBufferSize(Size,KickOff)", "Q", "-"),
0xfd6e0: ("Direct3D_CreateDevice", "W", "xbox_Direct3DCreate8 + CreateDevice; host device stays host-side"),
0xfd7a0: ("SetRenderState_MultiSampleMode", "N", "none (re-applies SetRenderTarget natively)"),
0xfd7d0: ("SetRenderState_MultiSampleRenderTargetMode", "N", "none (as above)"),
0xfd800: ("SetRenderState_PSTextureModes", "N", "RS[117] -> D3DRS_PSTEXTUREMODES"),
0xfd830: ("SetRenderState_Simple(method,value) __fastcall", "N", "value also stored in RS[State] by game dispatcher 0x3A830"),
0xfda00: ("SetRenderState_EdgeAntialias", "N", "RS -> D3DRS_EDGEANTIALIAS (no-op in D3D11)"),
0xfda40: ("SetRenderState_ShadowFunc", "N", "no host equivalent; log if used"),
0xfda80: ("SetRenderState_FogColor", "N", "RS -> D3DRS_FOGCOLOR"),
0xfdad0: ("SetRenderState_CullMode", "N", "RS -> D3DRS_CULLMODE (0/0x900/0x901 -> 1/2/3)"),
0xfdb40: ("SetRenderState_FrontFace", "N", "folded into CULLMODE"),
0xfdb80: ("SetRenderState_NormalizeNormals", "N", "RS -> D3DRS_NORMALIZENORMALS"),
0xfdbc0: ("SetRenderState_TextureFactor", "N", "RS -> D3DRS_TEXTUREFACTOR"),
0xfdc20: ("SetRenderState_LineWidth", "N", "ignored (D3D11 lines are 1 px)"),
0xfdc90: ("SetRenderState_Dxt1NoiseEnable", "N", "ignored"),
0xfdd10: ("SetRenderState_ZBias", "N", "RS -> rasterizer DepthBias (xbox_d3d8 gap)"),
0xfdd90: ("SetRenderState_LogicOp", "N", "ignored unless nonzero (log)"),
0xfddf0: ("SetRenderState_FillMode", "N", "RS -> D3DRS_FILLMODE (0x1B00-2 -> 1-3)"),
0xfde40: ("SetRenderState_BackFillMode", "N", "ignored"),
0xfdea0: ("SetRenderState_TwoSidedLighting", "N", "ignored at first"),
0xfdf10: ("SetRenderState_VertexBlend", "N", "RS -> D3DRS_VERTEXBLEND"),
0xfdf60: ("SetTextureState_TexCoordIndex(Stage,Value)", "N", "TSS[s][28] -> D3DTSS_TEXCOORDINDEX"),
0xfe080: ("SetTextureState_BumpEnv(Stage,Type,Value)", "N", "TSS[s][22..27] -> D3DTSS_BUMPENVMAT*"),
0xfe0f0: ("SetTextureState_BorderColor(Stage,Value)", "N", "TSS[s][29] -> D3DTSS_BORDERCOLOR"),
0xfe130: ("SetTextureState_ColorKeyColor(Stage,Value)", "N", "TSS[s][30]; no host equivalent"),
0xfe5c0: ("SetRenderState_ZEnable", "N", "RS -> D3DRS_ZENABLE"),
0xfe660: ("SetRenderState_StencilEnable", "N", "RS -> D3DRS_STENCILENABLE"),
0xfe6f0: ("SetRenderState_StencilFail", "N", "RS -> D3DRS_STENCILFAIL (GL op -> D3DSTENCILOP)"),
0xfe760: ("SetRenderState_YuvEnable", "N", "ignored unless set (log)"),
0xfe790: ("SetRenderState_OcclusionCullEnable", "N", "ignored (NV2A z-cull)"),
0xfe800: ("SetRenderState_StencilCullEnable", "N", "ignored (NV2A z-cull)"),
0xfe870: ("SetRenderState_RopZCmpAlwaysRead", "N", "ignored"),
0xfe890: ("SetRenderState_RopZRead", "N", "ignored"),
0xfe8b0: ("SetRenderState_DoNotCullUncompressed", "N", "ignored"),
0xfe8d0: ("SetRenderState_MultiSampleAntiAlias", "N", "ignored"),
0xfe930: ("SetRenderState_MultiSampleMask", "N", "ignored"),
0xfe990: ("D3DDevice_GetDirect3D (writes 1)", "Q", "-"),
0xfe9a0: ("D3DDevice_GetDeviceCaps (copies 0xD4 bytes)", "Q", "-"),
0xfe9c0: ("D3DDevice_CreateTexture", "N", "host texture made lazily from the header at first bind"),
0xfe9f0: ("D3DDevice_CreateCubeTexture", "N", "lazy -> CreateCubeTexture"),
0xfea20: ("D3DDevice_SetTransform(State,pMatrix)", "N", "device+0x750+State*64 -> SetTransform (index remap)"),
0xfeb30: ("D3DDevice_GetViewport", "Q", "-"),
0xfeb50: ("D3DDevice_SetMaterial", "N", "device+0x9F0 -> SetMaterial"),
0xfec00: ("push 3 dwords __fastcall (game SetTexture fast path)", "W", "decode TEXTURE_OFFSET(stage): stage texture = (Data,Format)"),
0xfec30: ("D3DDevice_AddRef", "Q", "-"),
0xfec50: ("D3DDevice_Release", "N", "host teardown only at the real last ref"),
0xfec90: ("SetVerticalBlankCallback or SetSwapCallback", "Q", "-"),
0xfed90: ("D3DDevice_InsertFence", "Q", "-"),
0xfee80: ("D3DDevice_GetDisplayMode", "Q", "-"),
0xfef20: ("D3DDevice_SetRenderTarget(pRT,pZ)", "N", "device+0x2070/0x2074 -> host RT"),
0xff450: ("D3DDevice_GetBackBuffer", "Q", "-"),
0xff580: ("D3DDevice_CopyRects", "W", "host copy (xbox_d3d8 gap)"),
0xff830: ("D3DDevice_GetDepthStencilSurface", "Q", "-"),
0xff860: ("D3DDevice_SetViewport", "N", "device+0x9D0 -> SetViewport"),
0xff9c0: ("D3DDevice_SetLight", "N", "device+0x390 array -> SetLight"),
0xffba0: ("D3DDevice_LightEnable", "N", "light +0x68 flag -> LightEnable"),
0xffc90: ("D3DDevice_SetTexture(Stage,pTex)", "N", "device+0xA78[stage] -> host SetTexture"),
0xffe10: ("D3DDevice_SetPalette(Stage,pPal)", "N", "device+0xA88[stage] -> host SetPalette"),
0xffea0: ("D3DDevice_SetIndices(pIB,Base)", "N", "[0x10EC0C], device+0x1C read at draw"),
0x100c40: ("D3DDevice_Swap(Flags)", "W", "host Swap after native"),
0x100d00: ("D3DDevice_CreateIndexBuffer", "Q", "no host IB (indices via ...UP)"),
0x100d40: ("D3DPalette_Lock", "N", "palette re-uploaded on content change"),
0x100d70: ("D3DDevice_CreateVertexBuffer", "Q", "no host VB (vertices copied per draw)"),
0x100dd0: ("D3DVertexBuffer_Lock", "Q", "-"),
0x100e30: ("D3DDevice_CreatePalette", "Q", "-"),
0x100ea0: ("D3DDevice_Clear", "W", "host Clear"),
0x1012cc: ("D3DDevice_BeginStateBlock (probable)", "N", "-"),
0x101705: ("D3DDevice_ApplyStateBlock", "N", "callees update native shadows"),
0x10188d: ("D3DDevice_DeleteStateBlock", "Q", "-"),
0x10191e: ("D3DDevice_EndStateBlock (probable)", "N", "-"),
0x1019c0: ("D3DDevice_DrawVerticesUP(Prim,Count,pData,Stride)", "W", "scrape + DrawPrimitiveUP"),
0x101b20: ("D3DDevice_DrawVertices(Prim,Start,Count)", "W", "scrape + stream fetch + DrawPrimitiveUP"),
0x101bc0: ("D3DDevice_DrawIndexedVertices(Prim,Count,pIdx)", "W", "scrape + fetch + DrawIndexedPrimitiveUP"),
0x101e20: ("D3DDevice_SetVertexData2f(Reg,a,b)", "W", "immediate-mode accumulator"),
0x101e60: ("D3DDevice_SetVertexData4f(Reg,a,b,c,d)", "W", "immediate-mode accumulator"),
0x101ec0: ("D3DDevice_Begin(Prim)", "W", "immediate-mode start"),
0x101f00: ("D3DDevice_End (tail jmp from 0x3AE30)", "W", "scrape + DrawPrimitiveUP"),
0x102440: ("D3DDevice_CreateVertexShader", "N", "host shader built lazily from native object"),
0x102580: ("D3DDevice_SetStreamSource", "N", "table 0x10F280 read at draw"),
0x1026f0: ("D3DDevice_SetShaderConstantMode", "N", "device+0x2018 read at draw"),
0x102830: ("D3DDevice_DeleteVertexShader", "N", "host cache entry dropped lazily"),
0x102940: ("D3DDevice_SetVertexShader", "N", "device+0x380/0x384 read at draw"),
0x102aa0: ("D3DDevice_SetVertexShaderConstant", "W", "host SetVertexShaderConstant"),
0x102b40: ("D3DDevice_CreatePixelShader", "Q", "-"),
0x102b90: ("D3DDevice_DeletePixelShader", "Q", "-"),
0x102bb0: ("D3DDevice_SetPixelShader", "N", "def copied into RS[0..56] -> host RS 200-253"),
0x102d80: ("D3DDevice_SetPixelShaderConstant", "N", "lands in RS PSCONSTANT*"),
0x1038d0: ("D3DResource_GetType", "Q", "-"),
0x103a90: ("D3DResource_AddRef", "Q", "-"),
0x103ad0: ("D3DResource_Release", "Q", "host cache validated by signature"),
0x103b20: ("D3DResource_IsBusy", "Q", "-"),
0x103c20: ("D3DTexture_GetLevelDesc", "Q", "-"),
0x103c30: ("D3DTexture_GetSurfaceLevel", "Q", "-"),
0x103c80: ("D3DTexture_LockRect", "W", "mark host texture dirty"),
0x103cb0: ("D3DCubeTexture_GetCubeMapSurface", "Q", "-"),
0x103dd0: ("D3DBaseTexture_GetLevelCount", "Q", "-"),
0x103de0: ("D3DSurface_GetDesc", "Q", "-"),
0x103e00: ("D3DSurface_LockRect", "W", "mark parent dirty; readback if host RT"),
}

rows = []
for k in sorted(E, key=lambda s: int(s, 16)):
    a = int(k, 16); v = E[k]
    meth = []
    for va, t in A.get(a, []):
        for h in re.findall(r"0x[0-9a-f]+", t):
            d = L.decode_hdr(int(h, 16))
            if d and "mov" in t and "dword ptr [e" in t:
                meth.append(L.method_name(d[1]).replace("NV097_", ""))
    n, disp, tgt = ID.get(a, ("?", "?", "?"))
    rows.append(dict(addr=a, tier=tier(a), sites=len(v['sites']), name=n, disp=disp, target=tgt,
                     methods=sorted(set(meth)), callers=v['callers'][:4]))
json.dump(rows, open("table.json", "w"), indent=1)
print(collections.Counter(r['tier'] for r in rows))
print(collections.Counter(r['disp'] for r in rows))
print("unnamed:", [hex(r['addr']) for r in rows if r['name'] == '?'])
print("tierC:", len([r for r in rows if r['tier'] == 'C']), [hex(r['addr']) for r in rows if r['tier'] == 'C'])
print("sites by disp:", {d: sum(r['sites'] for r in rows if r['disp'] == d) for d in "NWQ"})
print("W list:", [(hex(r['addr']), r['sites']) for r in rows if r['disp'] == 'W'])
