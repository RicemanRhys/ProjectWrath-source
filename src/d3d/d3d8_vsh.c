/**
 * NV2A Vertex Shader Microcode to HLSL Translator - Implementation
 *
 * Translates NV2A 128-bit vertex shader microcode instructions into
 * HLSL vertex shader source, compiles them, and caches the results.
 *
 * The translation pipeline is:
 *   1. Parse: 128-bit instruction words -> NV2AVshInstruction structs
 *   2. Analyze: determine which input registers (v0-v15) are read
 *   3. Generate HLSL: emit HLSL code mapping NV2A ops to HLSL intrinsics
 *   4. Compile: D3DCompile -> ID3D11VertexShader
 *   5. Cache: hash microcode -> reuse compiled shader on subsequent draws
 *
 * The generated HLSL uses:
 *   - cbuffer at b1: 192 float4 constants (c0-c191), indexed as the
 *     microcode indexes them (an XDK title's c0 is slot 96)
 *   - Input semantics: ATTR0-ATTR15 mapped to v0-v15, each a float4
 *   - Outputs: the pixel shaders' input struct, with oPos taken from the
 *     NV2A's screen space back to clip space through c58/c59
 *
 * Local fix (twoc notes 18.38): the field layout, the parallel MAC/ILU
 * semantics, the output struct, the screen-space position and the input
 * layout were rewritten after checking against a title's programs.
 */

#include "d3d8_internal.h"
#include <d3d11_1.h>
#include "d3d8_vsh.h"
#include <d3dcompiler.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <stdlib.h>

#pragma comment(lib, "d3dcompiler.lib")

/* ================================================================
 * NV2A Instruction Fields
 *
 * Each instruction is 128 bits stored as 4 DWORDs, as a title uploads it
 * with NV097_SET_TRANSFORM_PROGRAM. DWORD 0 is unused. Field positions
 * (DWORD, first bit, width) are xemu's (hw/xbox/nv2a vsh.c):
 *
 *   DWORD 1: ILU op 25/3, MAC op 21/4, constant index 13/8, input index 9/4,
 *            A negate 8/1, A swizzle x 6/2, y 4/2, z 2/2, w 0/2
 *   DWORD 2: A temp 28/4, A mux 26/2; B negate 25/1, B swizzle 23, 21, 19,
 *            17 (2 each), B temp 13/4, B mux 11/2; C negate 10/1, C swizzle
 *            8, 6, 4, 2 (2 each), C temp high bits 0/2
 *   DWORD 3: C temp low bits 30/2, C mux 28/2; MAC temp mask 24/4, temp
 *            index 20/4, ILU temp mask 16/4, output mask 12/4, output bank
 *            11/1 (0 constant, 1 output register), output address 3/8,
 *            output mux 2/1 (0 MAC, 1 ILU), relative addressing 1/1, final 0/1
 *
 * Mux: 1 temp, 2 input (the instruction's input index), 3 constant (the
 * instruction's constant index, plus a0.x when relative). Masks: bit 3 = x
 * .. bit 0 = w. When both units run, the ILU's temp destination is R1.
 * Checked against the programs of Crash Bandicoot: The Wrath of Cortex
 * (twoc notes 18.38): each decodes to a transform, lighting and skinning
 * program ending in the XDK's screen-space tail.
 *
 * (Local fix, twoc notes 18.38: the positions used before were not the
 * NV2A's, and several fields overlapped.)
 * ================================================================ */

#define VSH_F(insn, word, start, n) ((uint32_t)(((insn)[word] >> (start)) & ((1u << (n)) - 1)))

#define VSH_ILU(i)          VSH_F(i, 1, 25, 3)
#define VSH_MAC(i)          VSH_F(i, 1, 21, 4)
#define VSH_CONST(i)        VSH_F(i, 1, 13, 8)
#define VSH_V(i)            VSH_F(i, 1, 9, 4)
#define VSH_OUT_MAC_MASK(i) VSH_F(i, 3, 24, 4)
#define VSH_OUT_R(i)        VSH_F(i, 3, 20, 4)
#define VSH_OUT_ILU_MASK(i) VSH_F(i, 3, 16, 4)
#define VSH_OUT_O_MASK(i)   VSH_F(i, 3, 12, 4)
#define VSH_OUT_ORB(i)      VSH_F(i, 3, 11, 1)
#define VSH_OUT_ADDRESS(i)  VSH_F(i, 3, 3, 8)
#define VSH_OUT_MUX(i)      VSH_F(i, 3, 2, 1)
#define VSH_A0X(i)          VSH_F(i, 3, 1, 1)
#define VSH_FINAL(i)        VSH_F(i, 3, 0, 1)

#define VSH_MUX_TEMP        1
#define VSH_MUX_INPUT       2
#define VSH_MUX_CONST       3

/* ================================================================
 * Module State
 * ================================================================ */

/* Stored shader programs */
static NV2AVshSlot g_vsh_slots[NV2A_VS_MAX_SLOTS];
static uint32_t g_vsh_slot_hash[NV2A_VS_MAX_SLOTS];    /* each slot's microcode hash, set with it */
static int g_vsh_slot_count = 0;

/* Constant registers (192 float4) */
static NV2AVSConstants g_vsh_constants;
static BOOL g_vsh_constants_dirty = TRUE;

/* D3D11 constant buffer for VS constants */
static ID3D11Buffer *g_vsh_cb = NULL;

/* The block a baked draw uploaded into it last, while it still holds it
 * (twoc notes 18.90; NULL: something else since). Twoc step 95: a program's
 * 192 registers come as 12 blocks of 16, so the blocks ([0] NULL: none). */
static const float *g_vsh_baked_last[12];

/* The constants ring (local perf change, twoc notes 18.90): with D3D11.1's
 * constant-buffer offsets, a frame's captured draws (and the baked draws
 * whose constants moved) put their 192 registers into one large buffer,
 * each at its own offset, bound at it; a baked draw whose constants are as
 * captured binds the capture's block and copies nothing. The ring starts
 * over (DISCARD) when full, and every block in it goes (g_kring_gen); one
 * that may not hold the next frame starts over at the frame's first capture.
 * Without D3D11.1's offsets (or a buffer): g_vsh_cb as before. */
#define KRING_SIZE (32u << 20)
#define VSH_REGS_BYTES ((UINT)sizeof(NV2AVSConstants))
static ID3D11DeviceContext1 *g_ctx1;
static ID3D11Buffer *g_kring;
static int g_kring_state;               /* 0 not tried, 1 ready, -1 not there */
static UINT g_kring_off = KRING_SIZE, g_kring_gen = 1, g_kring_frame_bytes;
static UINT g_kring_peak, g_kring_last; /* twoc step 99: the most bytes a frame put in it, the last frame's */
static BOOL g_kring_cur_ok;             /* g_vsh_constants is at g_kring_cur in the ring now */
static UINT g_kring_cur, g_kring_cur_gen;

/* Shader cache: maps microcode hash to compiled shader + input layout */
typedef struct {
    uint32_t            hash;
    int                 in_use;
    ID3D11VertexShader *vs;
    ID3DBlob           *vs_blob;      /* Bytecode for input layout creation */
    ID3D11InputLayout  *layouts[16];  /* Cached layouts per input mask subset */
    uint32_t            layout_masks[16];  /* inputs_read | (texcoord-field<<16) */
    int                 layout_count;
    uint16_t            inputs_read;  /* Which v registers are read */
} VshCacheEntry;

static VshCacheEntry g_vsh_cache[NV2A_VS_CACHE_SIZE];

/* ================================================================
 * Hash Function (FNV-1a)
 * ================================================================ */

static uint32_t fnv1a_hash(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 0x811c9dc5;
    size_t i;
    for (i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x01000193;
    }
    return h;
}

/* ================================================================
 * Microcode Parser
 * ================================================================ */

/* One source operand: A (0), B (1) or C (2). */
static void parse_source(const DWORD *insn, int which, const NV2AVshInstruction *inst, NV2AVshSrcOperand *src)
{
    uint32_t neg, mux, r, sx, sy, sz, sw;

    switch (which) {
    case 0:
        neg = VSH_F(insn, 1, 8, 1);
        sx = VSH_F(insn, 1, 6, 2); sy = VSH_F(insn, 1, 4, 2); sz = VSH_F(insn, 1, 2, 2); sw = VSH_F(insn, 1, 0, 2);
        r = VSH_F(insn, 2, 28, 4);
        mux = VSH_F(insn, 2, 26, 2);
        break;
    case 1:
        neg = VSH_F(insn, 2, 25, 1);
        sx = VSH_F(insn, 2, 23, 2); sy = VSH_F(insn, 2, 21, 2); sz = VSH_F(insn, 2, 19, 2); sw = VSH_F(insn, 2, 17, 2);
        r = VSH_F(insn, 2, 13, 4);
        mux = VSH_F(insn, 2, 11, 2);
        break;
    default:
        neg = VSH_F(insn, 2, 10, 1);
        sx = VSH_F(insn, 2, 8, 2); sy = VSH_F(insn, 2, 6, 2); sz = VSH_F(insn, 2, 4, 2); sw = VSH_F(insn, 2, 2, 2);
        r = (VSH_F(insn, 2, 0, 2) << 2) | VSH_F(insn, 3, 30, 2);
        mux = VSH_F(insn, 3, 28, 2);
        break;
    }
    src->negate = (int)neg;
    src->swizzle.x = (uint8_t)sx;
    src->swizzle.y = (uint8_t)sy;
    src->swizzle.z = (uint8_t)sz;
    src->swizzle.w = (uint8_t)sw;
    src->rel_addr = 0;
    switch (mux) {
    case VSH_MUX_TEMP:
        src->reg_type = NV2A_VSH_REG_TEMP;
        src->reg_index = (int)r;                /* 12 reads oPos */
        break;
    case VSH_MUX_INPUT:
        src->reg_type = NV2A_VSH_REG_INPUT;
        src->reg_index = inst->input_index;
        break;
    case VSH_MUX_CONST:
        src->reg_type = NV2A_VSH_REG_CONST;
        src->reg_index = inst->const_index;
        src->rel_addr = (int)VSH_A0X(insn);
        break;
    default:                                    /* 0: unused operand */
        src->reg_type = NV2A_VSH_REG_COUNT;
        src->reg_index = 0;
        break;
    }
}

/* Which operands each MAC op reads (bit 0 A, 1 B, 2 C). */
static unsigned mac_operands(NV2AVshMacOp op)
{
    switch (op) {
    case NV2A_VSH_MAC_MOV: case NV2A_VSH_MAC_ARL:  return 1;
    case NV2A_VSH_MAC_ADD:                         return 5;
    case NV2A_VSH_MAC_MAD:                         return 7;
    case NV2A_VSH_MAC_NOP:                         return 0;
    default:                                       return 3;
    }
}

void d3d8_vsh_parse(const DWORD *microcode, int num_insns,
                     NV2AVshProgram *program)
{
    int i, s;

    memset(program, 0, sizeof(*program));
    if (num_insns > NV2A_VS_MAX_INSTRUCTIONS)
        num_insns = NV2A_VS_MAX_INSTRUCTIONS;

    for (i = 0; i < num_insns; i++) {
        const DWORD *insn = &microcode[i * 4];
        NV2AVshInstruction *inst = &program->insns[i];
        unsigned used;

        inst->mac_op = (NV2AVshMacOp)VSH_MAC(insn);
        inst->ilu_op = (NV2AVshIluOp)VSH_ILU(insn);
        if (inst->mac_op >= NV2A_VSH_MAC_COUNT)
            inst->mac_op = NV2A_VSH_MAC_NOP;
        inst->const_index = (int)VSH_CONST(insn);
        inst->input_index = (int)VSH_V(insn);
        for (s = 0; s < 3; s++)
            parse_source(insn, s, inst, &inst->mac_src[s]);
        inst->ilu_src = inst->mac_src[2];       /* the ILU reads C */

        /* Temp destinations: both units share the index, except that a
         * paired ILU op writes R1. */
        inst->mac_dst.temp_reg = inst->mac_op != NV2A_VSH_MAC_NOP && inst->mac_op != NV2A_VSH_MAC_ARL &&
                                 VSH_OUT_MAC_MASK(insn) ? (int)VSH_OUT_R(insn) : -1;
        inst->mac_dst.write_mask = inst->mac_dst.temp_reg >= 0 ? (uint8_t)VSH_OUT_MAC_MASK(insn) : 0;
        inst->mac_dst.output_reg = NV2A_VSH_OUT_NONE;
        inst->ilu_dst.temp_reg = inst->ilu_op != NV2A_VSH_ILU_NOP && VSH_OUT_ILU_MASK(insn) ?
                                 (inst->mac_op != NV2A_VSH_MAC_NOP ? 1 : (int)VSH_OUT_R(insn)) : -1;
        inst->ilu_dst.write_mask = inst->ilu_dst.temp_reg >= 0 ? (uint8_t)VSH_OUT_ILU_MASK(insn) : 0;
        inst->ilu_dst.output_reg = NV2A_VSH_OUT_NONE;

        /* The output write: an output register or a constant, from either unit. */
        inst->out_mask = (uint8_t)VSH_OUT_O_MASK(insn);
        inst->out_from_ilu = (int)VSH_OUT_MUX(insn);
        inst->out_is_const = VSH_OUT_ORB(insn) == 0;
        inst->out_index = (int)VSH_OUT_ADDRESS(insn);
        inst->out_rel = (int)VSH_A0X(insn);
        if (inst->out_mask && (inst->out_from_ilu ? inst->ilu_op == NV2A_VSH_ILU_NOP
                                                  : inst->mac_op == NV2A_VSH_MAC_NOP))
            inst->out_mask = 0;                 /* nothing to write */
        if (inst->out_mask && !inst->out_is_const) {
            NV2AVshOutputReg o = (NV2AVshOutputReg)(inst->out_index & 0xF);
            inst->out_index = (o == NV2A_VSH_OUT_POS || (o >= NV2A_VSH_OUT_D0 && o <= NV2A_VSH_OUT_T3)) ? (int)o : -1;
            if (inst->out_index < 0)
                inst->out_mask = 0;
        }
        if (inst->out_mask && inst->out_is_const)
            program->writes_constants = 1;

        inst->is_final = (int)VSH_FINAL(insn);

        used = mac_operands(inst->mac_op) | (inst->ilu_op != NV2A_VSH_ILU_NOP ? 4u : 0u);
        for (s = 0; s < 3; s++)
            if ((used & (1u << s)) && inst->mac_src[s].reg_type == NV2A_VSH_REG_INPUT)
                program->inputs_read |= (uint16_t)(1u << inst->mac_src[s].reg_index);

        program->length = i + 1;
        if (inst->is_final)
            break;
    }
}

/* ================================================================
 * HLSL Code Generator
 * ================================================================ */

/* String buffer helper */
typedef struct {
    char *buf;
    int   pos;
    int   size;
} StrBuf;

static void sb_init(StrBuf *sb, char *buf, int size)
{
    sb->buf  = buf;
    sb->pos  = 0;
    sb->size = size;
    if (size > 0) buf[0] = '\0';
}

static void sb_append(StrBuf *sb, const char *fmt, ...)
{
    va_list ap;
    int remaining;
    if (sb->pos >= sb->size - 1) return;
    remaining = sb->size - sb->pos;
    va_start(ap, fmt);
    int n = vsnprintf(sb->buf + sb->pos, remaining, fmt, ap);
    va_end(ap);
    if (n > 0 && n < remaining)
        sb->pos += n;
    else if (n >= remaining)
        sb->pos = sb->size - 1;
}

static const char g_comp_names[] = "xyzw";

/* A source operand: register, swizzle (always all four), negation. R12 is
 * oPos (the generated code defines R12 as oPos). */
static void emit_source(StrBuf *sb, const NV2AVshSrcOperand *src)
{
    const NV2AVshSwizzle *swz = &src->swizzle;

    sb_append(sb, src->negate ? "(-" : "(");
    switch (src->reg_type) {
    case NV2A_VSH_REG_TEMP:
        sb_append(sb, "R%d", src->reg_index);
        break;
    case NV2A_VSH_REG_INPUT:
        sb_append(sb, "v%d", src->reg_index);
        break;
    case NV2A_VSH_REG_CONST:
        if (src->rel_addr)
            sb_append(sb, "c[a0 + %d]", src->reg_index);
        else if (src->reg_index < NV2A_VS_MAX_CONSTANTS)
            sb_append(sb, "c[%d]", src->reg_index);
        else
            sb_append(sb, "float4(0,0,0,0)");
        break;
    default:
        sb_append(sb, "float4(0,0,0,0)");
        break;
    }
    sb_append(sb, ".%c%c%c%c)", g_comp_names[swz->x & 3], g_comp_names[swz->y & 3], g_comp_names[swz->z & 3],
              g_comp_names[swz->w & 3]);
}

/* A write mask suffix: bit 3 = x .. bit 0 = w. */
static void emit_write_mask(StrBuf *sb, uint8_t mask)
{
    sb_append(sb, ".");
    if (mask & 0x8) sb_append(sb, "x");
    if (mask & 0x4) sb_append(sb, "y");
    if (mask & 0x2) sb_append(sb, "z");
    if (mask & 0x1) sb_append(sb, "w");
}

static const char *output_reg_name(int reg)
{
    switch (reg) {
    case NV2A_VSH_OUT_POS:  return "oPos";
    case NV2A_VSH_OUT_D0:   return "oD0";
    case NV2A_VSH_OUT_D1:   return "oD1";
    case NV2A_VSH_OUT_FOG:  return "oFog";
    case NV2A_VSH_OUT_PTS:  return "oPts";
    case NV2A_VSH_OUT_B0:   return "oB0";
    case NV2A_VSH_OUT_B1:   return "oB1";
    case NV2A_VSH_OUT_T0:   return "oT0";
    case NV2A_VSH_OUT_T1:   return "oT1";
    case NV2A_VSH_OUT_T2:   return "oT2";
    case NV2A_VSH_OUT_T3:   return "oT3";
    default:                return NULL;
    }
}

/* The MAC's result as an expression into `m` (ARL instead sets a0). */
static void emit_mac_op(StrBuf *sb, const NV2AVshInstruction *inst)
{
    const NV2AVshSrcOperand *A = &inst->mac_src[0], *B = &inst->mac_src[1], *C = &inst->mac_src[2];

    switch (inst->mac_op) {
    case NV2A_VSH_MAC_MOV:
        sb_append(sb, "        m = "); emit_source(sb, A); break;
    case NV2A_VSH_MAC_MUL:
        sb_append(sb, "        m = nv_mul("); emit_source(sb, A); sb_append(sb, ", "); emit_source(sb, B);
        sb_append(sb, ")"); break;
    case NV2A_VSH_MAC_ADD:
        sb_append(sb, "        m = "); emit_source(sb, A); sb_append(sb, " + "); emit_source(sb, C); break;
    case NV2A_VSH_MAC_MAD:
        sb_append(sb, "        m = nv_mul("); emit_source(sb, A); sb_append(sb, ", "); emit_source(sb, B);
        sb_append(sb, ") + "); emit_source(sb, C); break;
    case NV2A_VSH_MAC_DP3:
        sb_append(sb, "        m = dot("); emit_source(sb, A); sb_append(sb, ".xyz, "); emit_source(sb, B);
        sb_append(sb, ".xyz).xxxx"); break;
    case NV2A_VSH_MAC_DPH:
        sb_append(sb, "        m = (dot("); emit_source(sb, A); sb_append(sb, ".xyz, "); emit_source(sb, B);
        sb_append(sb, ".xyz) + "); emit_source(sb, B); sb_append(sb, ".w).xxxx"); break;
    case NV2A_VSH_MAC_DP4:
        sb_append(sb, "        m = dot("); emit_source(sb, A); sb_append(sb, ", "); emit_source(sb, B);
        sb_append(sb, ").xxxx"); break;
    case NV2A_VSH_MAC_DST:
        sb_append(sb, "        m = float4(1.0, "); emit_source(sb, A); sb_append(sb, ".y * "); emit_source(sb, B);
        sb_append(sb, ".y, "); emit_source(sb, A); sb_append(sb, ".z, "); emit_source(sb, B); sb_append(sb, ".w)");
        break;
    case NV2A_VSH_MAC_MIN:
        sb_append(sb, "        m = min("); emit_source(sb, A); sb_append(sb, ", "); emit_source(sb, B);
        sb_append(sb, ")"); break;
    case NV2A_VSH_MAC_MAX:
        sb_append(sb, "        m = max("); emit_source(sb, A); sb_append(sb, ", "); emit_source(sb, B);
        sb_append(sb, ")"); break;
    case NV2A_VSH_MAC_SLT:
        sb_append(sb, "        m = (float4)("); emit_source(sb, A); sb_append(sb, " < "); emit_source(sb, B);
        sb_append(sb, ")"); break;
    case NV2A_VSH_MAC_SGE:
        sb_append(sb, "        m = (float4)("); emit_source(sb, A); sb_append(sb, " >= "); emit_source(sb, B);
        sb_append(sb, ")"); break;
    case NV2A_VSH_MAC_ARL:
        sb_append(sb, "        a0n = (int)floor("); emit_source(sb, A); sb_append(sb, ".x + 0.001)"); break;
    default:
        return;
    }
    sb_append(sb, ";\n");
}

/* The ILU's result into `u`: a function of C (its x, or all of it for LIT). */
static void emit_ilu_op(StrBuf *sb, const NV2AVshInstruction *inst)
{
    const NV2AVshSrcOperand *C = &inst->ilu_src;

    switch (inst->ilu_op) {
    case NV2A_VSH_ILU_MOV: sb_append(sb, "        u = "); emit_source(sb, C); break;
    case NV2A_VSH_ILU_RCP: sb_append(sb, "        u = (1.0 / "); emit_source(sb, C); sb_append(sb, ".x).xxxx"); break;
    case NV2A_VSH_ILU_RCC: sb_append(sb, "        u = nv_rcc("); emit_source(sb, C); sb_append(sb, ".x)"); break;
    case NV2A_VSH_ILU_RSQ: sb_append(sb, "        u = rsqrt(abs("); emit_source(sb, C); sb_append(sb, ".x)).xxxx"); break;
    case NV2A_VSH_ILU_EXP: sb_append(sb, "        u = nv_exp("); emit_source(sb, C); sb_append(sb, ".x)"); break;
    case NV2A_VSH_ILU_LOG: sb_append(sb, "        u = nv_log("); emit_source(sb, C); sb_append(sb, ".x)"); break;
    case NV2A_VSH_ILU_LIT: sb_append(sb, "        u = nv_lit("); emit_source(sb, C); sb_append(sb, ")"); break;
    default: return;
    }
    sb_append(sb, ";\n");
}

static void emit_assign(StrBuf *sb, const char *dst, uint8_t mask, char val)
{
    sb_append(sb, "        %s", dst);
    emit_write_mask(sb, mask);
    sb_append(sb, " = %c", val);
    emit_write_mask(sb, mask);
    sb_append(sb, ";\n");
}

int d3d8_vsh_generate_hlsl(const NV2AVshProgram *program,
                            char *buf, int bufsize)
{
    StrBuf sb;
    int i;
    uint16_t inputs = program->inputs_read;

    sb_init(&sb, buf, bufsize);

    sb_append(&sb,
        "/* Auto-generated NV2A vertex shader */\n"
        "cbuffer VSH_Constants : register(b1) {\n"
        "    float4 c[%d];\n"
        "};\n"
        "\n"
        /* MUL and MAD as the NV2A multiplies (and xemu's _MUL): anything times
         * 0 is 0, inf and NaN included. A program can rely on it: Crash's
         * morph program scales stream-1 deltas by c[122].x = 0 for meshes
         * without morph data, whose stream 1 is then whatever lies there. */
        "float4 nv_mul(float4 a, float4 b) {\n"
        "    float4 r = a * b;\n"
        "    r.x = (a.x == 0.0 || b.x == 0.0) ? 0.0 : r.x;\n"
        "    r.y = (a.y == 0.0 || b.y == 0.0) ? 0.0 : r.y;\n"
        "    r.z = (a.z == 0.0 || b.z == 0.0) ? 0.0 : r.z;\n"
        "    r.w = (a.w == 0.0 || b.w == 0.0) ? 0.0 : r.w;\n"
        "    return r;\n"
        "}\n"
        "float4 nv_rcc(float x) {\n"
        "    float r = 1.0 / x;\n"
        "    r = r >= 0.0 ? clamp(r, 5.42101e-20, 1.884467e+19) : clamp(r, -1.884467e+19, -5.42101e-20);\n"
        "    return r.xxxx;\n"
        "}\n"
        "float4 nv_exp(float x) { float f = floor(x); return float4(exp2(f), x - f, exp2(x), 1.0); }\n"
        "float4 nv_log(float x) {\n"
        "    float t = abs(x);\n"
        "    if (t == 0.0) return float4(-3.402823e+38, 1.0, -3.402823e+38, 1.0);\n"
        "    float e = floor(log2(t));\n"
        "    return float4(e, t / exp2(e), log2(t), 1.0);\n"
        "}\n"
        "float4 nv_lit(float4 s) {\n"
        "    float4 t = float4(1.0, 0.0, 0.0, 1.0);\n"
        "    if (s.x > 0.0) { t.y = s.x; if (s.y > 0.0) t.z = pow(s.y, s.w); }\n"
        "    return t;\n"
        "}\n"
        "\n", NV2A_VS_MAX_CONSTANTS);

    /* Inputs: every register read, as a float4 (the caller expands each
     * attribute the NV2A way). */
    sb_append(&sb, "struct VS_IN {\n");
    for (i = 0; i < NV2A_VS_MAX_INPUTS; i++)
        if (inputs & (1u << i))
            sb_append(&sb, "    float4 v%d : ATTR%d;\n", i, i);
    if (!inputs)
        sb_append(&sb, "    uint vid : SV_VertexID;\n");
    sb_append(&sb, "};\n\n");

    /* Outputs: the pixel shaders' input (d3d8_shaders.c PS_IN; the
     * combiner shader reads a prefix of it). */
    sb_append(&sb,
        "struct VS_OUT {\n"
        "    float4 pos      : SV_POSITION;\n"
        "    float4 diffuse  : COLOR0;\n"
        "    float4 specular : COLOR1;\n"
        "    float4 tex0     : TEXCOORD0;\n"
        "    float4 tex1     : TEXCOORD1;\n"
        "    float4 tex2     : TEXCOORD2;\n"
        "    float4 tex3     : TEXCOORD3;\n"
        "    float  fog      : TEXCOORD4;\n"
        "    float4 viewpos  : TEXCOORD5;\n"
        "};\n\n"
        "VS_OUT main(VS_IN input) {\n");

    for (i = 0; i < 12; i++)
        sb_append(&sb, "    float4 R%d = float4(0,0,0,0);\n", i);
    for (i = 0; i < NV2A_VS_MAX_INPUTS; i++)
        if (inputs & (1u << i))
            sb_append(&sb, "    float4 v%d = input.v%d;\n", i, i);
    sb_append(&sb,
        "    int a0 = 0, a0n = 0;\n"
        "    float4 m = float4(0,0,0,0), u = float4(0,0,0,0);\n"
        "    float4 oPos = float4(0,0,0,1), oD0 = float4(0,0,0,1), oD1 = float4(0,0,0,1);\n"
        "    float4 oFog = float4(0,0,0,1), oPts = float4(0,0,0,1), oB0 = float4(0,0,0,1), oB1 = float4(0,0,0,1);\n"
        "    float4 oT0 = float4(0,0,0,1), oT1 = float4(0,0,0,1), oT2 = float4(0,0,0,1), oT3 = float4(0,0,0,1);\n"
        "    #define R12 oPos\n");

    /* Each instruction: both units read, then write (they run in parallel). */
    for (i = 0; i < program->length; i++) {
        const NV2AVshInstruction *inst = &program->insns[i];
        int mac = inst->mac_op != NV2A_VSH_MAC_NOP, ilu = inst->ilu_op != NV2A_VSH_ILU_NOP;
        char name[16];

        sb_append(&sb, "    { /* %d */\n", i);
        if (mac)
            emit_mac_op(&sb, inst);
        if (ilu)
            emit_ilu_op(&sb, inst);
        if (inst->mac_dst.temp_reg >= 0 && inst->mac_dst.temp_reg <= 12) {
            snprintf(name, sizeof(name), "R%d", inst->mac_dst.temp_reg);
            emit_assign(&sb, name, inst->mac_dst.write_mask, 'm');
        }
        if (inst->ilu_dst.temp_reg >= 0 && inst->ilu_dst.temp_reg <= 12) {
            snprintf(name, sizeof(name), "R%d", inst->ilu_dst.temp_reg);
            emit_assign(&sb, name, inst->ilu_dst.write_mask, 'u');
        }
        if (inst->out_mask && !inst->out_is_const)
            emit_assign(&sb, output_reg_name(inst->out_index), inst->out_mask, inst->out_from_ilu ? 'u' : 'm');
        if (inst->mac_op == NV2A_VSH_MAC_ARL)
            sb_append(&sb, "        a0 = a0n;\n");
        sb_append(&sb, "    }\n");
    }

    /* The NV2A takes oPos in screen space: an XDK program ends by mapping
     * clip space through the viewport, oPos.xyz = oPos.xyz * c[58] / w +
     * c[59], where the XDK keeps the viewport scale and offset (its
     * reserved constants c-38 and c-37). D3D11 wants clip space, so the
     * mapping is undone with the same two constants; the host viewport then
     * maps it to the same pixels. A program that writes screen space
     * itself is inverted the same way. */
    sb_append(&sb,
        "    #undef R12\n"
        "    VS_OUT o;\n"
        "    float3 vs = c[58].xyz;\n"
        "    vs = vs != 0.0 ? vs : 1.0;\n"
        "    o.pos = float4((oPos.xyz - c[59].xyz) / vs * oPos.w, oPos.w);\n"
        "    o.diffuse  = saturate(oD0);\n"
        "    o.specular = saturate(oD1);\n"
        "    o.tex0 = oT0;\n"
        "    o.tex1 = oT1;\n"
        "    o.tex2 = oT2;\n"
        "    o.tex3 = oT3;\n"
        "    o.fog = saturate(oFog.x);\n"          /* vertex fog: the factor */
        "    o.viewpos = float4(0.0, 0.0, oFog.x, 1.0);\n"   /* table fog: the NV2A fogs from oFog.x */
        "    return o;\n"
        "}\n");

    return sb.pos;
}

/* ================================================================
 * Input Layout
 *
 * Every register the program reads is a float4 (R32G32B32A32_FLOAT), in
 * ascending register order, in one stream: the caller fetches each
 * attribute from the title's streams and expands it as the NV2A does
 * (missing components 0, 0, 0, 1; normalized types to floats). The NV2A's
 * registers have no fixed meaning: a declaration can put texture
 * coordinates in v3 or normals in v1, so a per-register format can't be
 * assumed (twoc notes 18.38; before, v3 was always R8G8B8A8).
 * ================================================================ */

static ID3D11InputLayout *create_vsh_input_layout(
    uint16_t inputs_read, DWORD fvf, ID3DBlob *vs_blob)
{
    D3D11_INPUT_ELEMENT_DESC elems[NV2A_VS_MAX_INPUTS];
    UINT elem_count = 0;
    ID3D11InputLayout *layout = NULL;
    HRESULT hr;
    int i;

    (void)fvf;
    for (i = 0; i < NV2A_VS_MAX_INPUTS; i++) {
        if (!(inputs_read & (1u << i)))
            continue;
        elems[elem_count].SemanticName         = "ATTR";
        elems[elem_count].SemanticIndex        = (UINT)i;
        elems[elem_count].Format               = DXGI_FORMAT_R32G32B32A32_FLOAT;
        elems[elem_count].InputSlot            = 0;
        elems[elem_count].AlignedByteOffset    = 16 * elem_count;
        elems[elem_count].InputSlotClass       = D3D11_INPUT_PER_VERTEX_DATA;
        elems[elem_count].InstanceDataStepRate = 0;
        elem_count++;
    }

    if (elem_count == 0) return NULL;

    hr = ID3D11Device_CreateInputLayout(
        d3d8_GetD3D11Device(),
        elems, elem_count,
        ID3D10Blob_GetBufferPointer(vs_blob),
        ID3D10Blob_GetBufferSize(vs_blob),
        &layout);

    if (FAILED(hr)) {
        fprintf(stderr, "D3D8 VSH: CreateInputLayout failed: 0x%08lX\n", hr);
        return NULL;
    }

    return layout;
}

/* ================================================================
 * Shader Compilation and Caching
 * ================================================================ */

static VshCacheEntry *cache_lookup(uint32_t hash)
{
    int idx = (int)(hash % NV2A_VS_CACHE_SIZE);
    int i;
    for (i = 0; i < NV2A_VS_CACHE_SIZE; i++) {
        int probe = (idx + i) % NV2A_VS_CACHE_SIZE;
        if (!g_vsh_cache[probe].in_use)
            return NULL;
        if (g_vsh_cache[probe].hash == hash)
            return &g_vsh_cache[probe];
    }
    return NULL;
}

static VshCacheEntry *cache_insert(uint32_t hash)
{
    int idx = (int)(hash % NV2A_VS_CACHE_SIZE);
    int i;

    /* Find an empty slot or reuse the probed slot */
    for (i = 0; i < NV2A_VS_CACHE_SIZE; i++) {
        int probe = (idx + i) % NV2A_VS_CACHE_SIZE;
        if (!g_vsh_cache[probe].in_use) {
            g_vsh_cache[probe].hash   = hash;
            g_vsh_cache[probe].in_use = 1;
            return &g_vsh_cache[probe];
        }
    }

    /* Cache full: evict the first probed entry */
    {
        VshCacheEntry *evict = &g_vsh_cache[idx];
        int j;

        d3d8_bake_gen++;                /* a baked draw may hold its shader or layouts (18.90) */

        if (evict->vs)
            ID3D11VertexShader_Release(evict->vs);
        if (evict->vs_blob)
            ID3D10Blob_Release(evict->vs_blob);
        for (j = 0; j < evict->layout_count; j++) {
            if (evict->layouts[j])
                ID3D11InputLayout_Release(evict->layouts[j]);
        }
        memset(evict, 0, sizeof(*evict));
        evict->hash   = hash;
        evict->in_use = 1;
        return evict;
    }
}

/**
 * Compile a vertex shader from microcode.
 *
 * Parses, generates HLSL, compiles, and caches the result.
 * Returns the cache entry (with compiled VS and blob).
 */
static VshCacheEntry *compile_shader(const DWORD *microcode, int num_insns,
                                      uint32_t hash)
{
    NV2AVshProgram program;
    static char hlsl_buf[98304];    /* up to ~700 bytes an instruction; callers hold their device lock */
    int hlsl_len;
    ID3DBlob *code = NULL, *errors = NULL;
    HRESULT hr;
    VshCacheEntry *entry;

    /* Parse microcode */
    d3d8_vsh_parse(microcode, num_insns, &program);

    /* Generate HLSL */
    hlsl_len = d3d8_vsh_generate_hlsl(&program, hlsl_buf, sizeof(hlsl_buf));
    if (hlsl_len <= 0) {
        fprintf(stderr, "D3D8 VSH: HLSL generation failed\n");
        return NULL;
    }

    if (program.writes_constants)
        fprintf(stderr, "D3D8 VSH: shader 0x%08X writes constants; not supported, those writes are dropped\n", hash);
    if (getenv("XBOX_D3D8_VSH_HLSL"))
        fprintf(stderr, "--- NV2A VS 0x%08X ---\n%s\n--- End ---\n", hash, hlsl_buf);

    /* Compile HLSL to bytecode (nv_mul's zero test survives level 3: eq/or/movc) */
    hr = D3DCompile(hlsl_buf, (SIZE_T)hlsl_len, "nv2a_vsh",
                    NULL, NULL, "main", "vs_5_0",
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                    &code, &errors);
    if (FAILED(hr)) {
        fprintf(stderr, "D3D8 VSH: Compile failed: %s\n",
                errors ? (char *)ID3D10Blob_GetBufferPointer(errors) : "unknown");
        fprintf(stderr, "--- Generated HLSL ---\n%s\n--- End ---\n", hlsl_buf);
        if (errors) ID3D10Blob_Release(errors);
        return NULL;
    }
    if (errors) ID3D10Blob_Release(errors);

    /* Insert into cache */
    entry = cache_insert(hash);
    if (!entry) {
        ID3D10Blob_Release(code);
        return NULL;
    }

    /* Create D3D11 vertex shader */
    hr = ID3D11Device_CreateVertexShader(
        d3d8_GetD3D11Device(),
        ID3D10Blob_GetBufferPointer(code),
        ID3D10Blob_GetBufferSize(code),
        NULL, &entry->vs);

    if (FAILED(hr)) {
        fprintf(stderr, "D3D8 VSH: CreateVertexShader failed: 0x%08lX\n", hr);
        ID3D10Blob_Release(code);
        entry->in_use = 0;
        return NULL;
    }

    entry->vs_blob     = code;
    entry->inputs_read = program.inputs_read;
    entry->layout_count = 0;

    fprintf(stderr, "D3D8 VSH: Compiled shader (hash 0x%08X, %d insns, inputs 0x%04X)\n",
            hash, program.length, program.inputs_read);

    return entry;
}

/**
 * Get the input layout for a cache entry.
 * Creates and caches the layout on first request per (input mask, texcoord sizes).
 */
static ID3D11InputLayout *get_cached_layout(VshCacheEntry *entry, DWORD fvf)
{
    uint16_t mask = entry->inputs_read;
    uint32_t key = (uint32_t)mask | (((uint32_t)(fvf >> 16) & 0xFFFF) << 16);
    int i;

    /* Check if we already created a layout for this mask+texcoord layout */
    for (i = 0; i < entry->layout_count; i++) {
        if (entry->layout_masks[i] == key)
            return entry->layouts[i];
    }

    /* Create new layout */
    if (entry->layout_count >= 16)
        return entry->layouts[0]; /* Fallback to first */

    ID3D11InputLayout *layout = create_vsh_input_layout(mask, fvf, entry->vs_blob);
    entry->layouts[entry->layout_count]      = layout;
    entry->layout_masks[entry->layout_count] = key;
    entry->layout_count++;

    return layout;
}

/* ================================================================
 * Public API Implementation
 * ================================================================ */

HRESULT d3d8_vsh_init(void)
{
    D3D11_BUFFER_DESC cbd;
    HRESULT hr;

    memset(g_vsh_slots, 0, sizeof(g_vsh_slots));
    memset(g_vsh_cache, 0, sizeof(g_vsh_cache));
    memset(&g_vsh_constants, 0, sizeof(g_vsh_constants));
    g_vsh_slot_count = 0;
    g_vsh_constants_dirty = TRUE;
    g_vsh_baked_last[0] = NULL;

    /* Create the constant buffer for VS constants (192 * float4 = 3072 bytes) */
    memset(&cbd, 0, sizeof(cbd));
    cbd.ByteWidth      = sizeof(NV2AVSConstants);
    cbd.Usage           = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;

    hr = ID3D11Device_CreateBuffer(d3d8_GetD3D11Device(), &cbd, NULL, &g_vsh_cb);
    if (FAILED(hr)) {
        fprintf(stderr, "D3D8 VSH: Failed to create constant buffer: 0x%08lX\n", hr);
        return hr;
    }

    fprintf(stderr, "D3D8 VSH: Vertex shader translator initialized\n");
    return S_OK;
}

void d3d8_vsh_shutdown(void)
{
    int i, j;

    /* Release all cached shaders and layouts */
    for (i = 0; i < NV2A_VS_CACHE_SIZE; i++) {
        VshCacheEntry *e = &g_vsh_cache[i];
        if (!e->in_use) continue;
        if (e->vs)      ID3D11VertexShader_Release(e->vs);
        if (e->vs_blob) ID3D10Blob_Release(e->vs_blob);
        for (j = 0; j < e->layout_count; j++) {
            if (e->layouts[j])
                ID3D11InputLayout_Release(e->layouts[j]);
        }
    }
    memset(g_vsh_cache, 0, sizeof(g_vsh_cache));

    if (g_vsh_cb) {
        ID3D11Buffer_Release(g_vsh_cb);
        g_vsh_cb = NULL;
    }
    if (g_kring) {
        ID3D11Buffer_Release(g_kring);
        g_kring = NULL;
    }
    if (g_ctx1) {
        ID3D11DeviceContext1_Release(g_ctx1);
        g_ctx1 = NULL;
    }
    g_kring_state = 0;
    g_kring_cur_ok = FALSE;
    g_kring_gen++;

    memset(g_vsh_slots, 0, sizeof(g_vsh_slots));
    g_vsh_slot_count = 0;
}

HRESULT d3d8_vsh_create_shader(const DWORD *microcode, int num_insns,
                                DWORD *out_handle)
{
    int slot;

    if (!microcode || num_insns <= 0 || !out_handle)
        return E_INVALIDARG;

    if (num_insns > NV2A_VS_MAX_INSTRUCTIONS)
        num_insns = NV2A_VS_MAX_INSTRUCTIONS;

    /* Find a free slot */
    slot = -1;
    for (int i = 0; i < NV2A_VS_MAX_SLOTS; i++) {
        if (!g_vsh_slots[i].in_use) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        fprintf(stderr, "D3D8 VSH: No free shader slots\n");
        return E_OUTOFMEMORY;
    }

    /* Store microcode (deferred compilation) */
    memcpy(g_vsh_slots[slot].microcode, microcode,
           (size_t)num_insns * 4 * sizeof(DWORD));
    g_vsh_slots[slot].length = num_insns;
    g_vsh_slots[slot].in_use = 1;
    g_vsh_slot_hash[slot] = fnv1a_hash(g_vsh_slots[slot].microcode, (size_t)num_insns * 4 * sizeof(DWORD));
    g_vsh_slot_count++;

    /* Generate handle: slot index + 0x10000 to distinguish from FVF codes.
     * Xbox D3D8 uses handles with the high bit set (> 0xFFFF). */
    *out_handle = (DWORD)(slot + 0x10000);

    fprintf(stderr, "D3D8 VSH: Created shader handle 0x%lX (%d instructions)\n",
            *out_handle, num_insns);

    return S_OK;
}

HRESULT d3d8_vsh_delete_shader(DWORD handle)
{
    int slot;

    if (!d3d8_vsh_is_programmable(handle))
        return E_INVALIDARG;

    slot = (int)(handle - 0x10000);
    if (slot < 0 || slot >= NV2A_VS_MAX_SLOTS)
        return E_INVALIDARG;

    if (g_vsh_slots[slot].in_use) {
        g_vsh_slots[slot].in_use = 0;
        g_vsh_slot_count--;
    }

    return S_OK;
}

void d3d8_vsh_set_constant(int start_reg, const float *data, int count)
{
    int end_reg;

    if (!data || start_reg < 0)
        return;

    end_reg = start_reg + count;
    if (end_reg > NV2A_VS_MAX_CONSTANTS)
        end_reg = NV2A_VS_MAX_CONSTANTS;

    for (int i = start_reg; i < end_reg; i++) {
        int src_offset = (i - start_reg) * 4;
        g_vsh_constants.c[i][0] = data[src_offset + 0];
        g_vsh_constants.c[i][1] = data[src_offset + 1];
        g_vsh_constants.c[i][2] = data[src_offset + 2];
        g_vsh_constants.c[i][3] = data[src_offset + 3];
    }

    g_vsh_constants_dirty = TRUE;
    g_kring_cur_ok = FALSE;
}

BOOL d3d8_vsh_is_programmable(DWORD handle)
{
    return (handle >= 0x10000) ? TRUE : FALSE;
}

/* The constants ring there, made the first time (FALSE: not on this device). */
static BOOL kring_ready(void)
{
    ID3D11DeviceContext *ctx = d3d8_GetD3D11Context();
    D3D11_FEATURE_DATA_D3D11_OPTIONS o;
    D3D11_BUFFER_DESC bd;

    if (g_kring_state)
        return g_kring_state > 0;
    g_kring_state = -1;
    memset(&o, 0, sizeof(o));
    if (FAILED(ID3D11Device_CheckFeatureSupport(d3d8_GetD3D11Device(), D3D11_FEATURE_D3D11_OPTIONS, &o, sizeof(o))) ||
        !o.ConstantBufferOffsetting ||
        FAILED(ID3D11DeviceContext_QueryInterface(ctx, &IID_ID3D11DeviceContext1, (void **)&g_ctx1))) {
        fprintf(stderr, "D3D8 VSH: no constant-buffer offsets (D3D11.1): baked draws upload their constants\n");
        return FALSE;
    }
    memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = KRING_SIZE;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(ID3D11Device_CreateBuffer(d3d8_GetD3D11Device(), &bd, NULL, &g_kring))) {
        ID3D11DeviceContext1_Release(g_ctx1);
        g_ctx1 = NULL;
        fprintf(stderr, "D3D8 VSH: the constants ring wasn't made: baked draws upload their constants\n");
        return FALSE;
    }
    g_kring_state = 1;
    fprintf(stderr, "D3D8 VSH: constants ring %u MB (D3D11.1 offsets) for baked draws\n", KRING_SIZE >> 20);
    return TRUE;
}

/* 192 registers from `p` (or, twoc step 95, from the 12 blocks of 16 at
 * `blk`) into the ring; returns their offset, (UINT)-1 if the map failed. */
static UINT kring_write2(const void *p, const float *const *blk)
{
    ID3D11DeviceContext *ctx = d3d8_GetD3D11Context();
    D3D11_MAPPED_SUBRESOURCE mapped;
    D3D11_MAP mt = D3D11_MAP_WRITE_NO_OVERWRITE;
    UINT off = g_kring_off;

    if (off + VSH_REGS_BYTES > KRING_SIZE) {
        off = 0;
        mt = D3D11_MAP_WRITE_DISCARD;
        g_kring_gen++;
    }
    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)g_kring, 0, mt, 0, &mapped)))
        return (UINT)-1;
    if (blk) {
        int i;
        for (i = 0; i < 12; i++)
            memcpy((BYTE *)mapped.pData + off + 256 * i, blk[i], 256);
    } else
        memcpy((BYTE *)mapped.pData + off, p, VSH_REGS_BYTES);
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)g_kring, 0);
    g_kring_off = off + VSH_REGS_BYTES;     /* 3072 bytes: 256-byte (16-constant) steps */
    g_kring_frame_bytes += VSH_REGS_BYTES;
    return off;
}

static UINT kring_write(const void *p)
{
    return kring_write2(p, NULL);
}

/* Slot b1 bound to the ring's block at `off`. */
static void kring_bind(UINT off)
{
    UINT first = off / 16, num = VSH_REGS_BYTES / 16;

    if (g_d3d8_bound.vs_cb[1] != g_kring || g_d3d8_bound.vs_cb1_first != first) {
        g_d3d8_bound.vs_cb[1] = g_kring;
        g_d3d8_bound.vs_cb1_first = first;
        LONG64 t0 = d3d8_ctx_t0();      /* timed as the context's other calls (d3d8_internal.h) */
        ID3D11DeviceContext1_VSSetConstantBuffers1(g_ctx1, 1, 1, &g_kring, &first, &num);
        d3d8_ctx_t1(t0);
    }
}

BOOL d3d8_vsh_prepare_draw(DWORD handle)
{
    ID3D11DeviceContext *ctx;
    int slot;
    NV2AVshSlot *vsh;
    uint32_t hash;
    VshCacheEntry *entry;
    ID3D11InputLayout *layout;
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr;

    if (!d3d8_vsh_is_programmable(handle))
        return FALSE;

    ctx = d3d8_GetD3D11Context();
    if (!ctx) return FALSE;

    /* Resolve handle to shader slot */
    slot = (int)(handle - 0x10000);
    if (slot < 0 || slot >= NV2A_VS_MAX_SLOTS)
        return FALSE;

    vsh = &g_vsh_slots[slot];
    if (!vsh->in_use)
        return FALSE;

    /* The microcode's hash, taken once when the slot was filled (local perf
     * change, twoc notes 18.63: it was rehashed byte by byte at every draw). */
    hash = g_vsh_slot_hash[slot];

    /* Look up in cache */
    entry = cache_lookup(hash);
    if (!entry) {
        /* Cache miss: parse, generate HLSL, compile */
        entry = compile_shader(vsh->microcode, vsh->length, hash);
        if (!entry)
            return FALSE;
    }

    /* Bind the vertex shader */
    d3d8_bind_vs(ctx, entry->vs);

    /* Bind the input layout (sizes texcoords from the bound stream FVF) */
    layout = get_cached_layout(entry, d3d8_GetCurrentFVF());
    if (layout)
        d3d8_bind_layout(ctx, layout);

    /* A captured draw (twoc notes 18.90): its constants in the ring, where
     * the baked draws after it find them (the block before when they haven't
     * changed since); g_vsh_cb is uploaded again at the next draw that isn't. */
    if (d3d8_bake_capturing() && kring_ready()) {
        if (!g_kring_cur_ok || g_kring_cur_gen != g_kring_gen) {
            UINT off = kring_write(&g_vsh_constants);
            if (off != (UINT)-1) {
                g_kring_cur = off;
                g_kring_cur_gen = g_kring_gen;
                g_kring_cur_ok = TRUE;
            }
        }
        if (g_kring_cur_ok && g_kring_cur_gen == g_kring_gen) {
            kring_bind(g_kring_cur);
            g_vsh_constants_dirty = TRUE;
            g_vsh_baked_last[0] = NULL;
            return TRUE;
        }
    }

    /* Update constant buffer if dirty */
    if (g_vsh_constants_dirty) {
        hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)g_vsh_cb,
                                     0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            memcpy(mapped.pData, &g_vsh_constants, sizeof(g_vsh_constants));
            ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)g_vsh_cb, 0);
        }
        g_vsh_constants_dirty = FALSE;
        g_vsh_baked_last[0] = NULL;
    }

    /* Bind constant buffer to slot b1 */
    d3d8_bind_vs_cb(ctx, 1, g_vsh_cb);

    return TRUE;
}

/* The captured draw's place in the ring (k_gen 0: not there). */
void d3d8_vsh_bake(D3D8Baked *b)
{
    b->k_gen = 0;
    if (b->vsh && g_kring_state > 0 && g_d3d8_bound.vs_cb[1] == g_kring && g_kring_cur_ok &&
        g_kring_cur_gen == g_kring_gen) {
        b->k_gen = g_kring_gen;
        b->k_first = g_kring_cur;
    }
}

/* A baked draw's program constants (twoc notes 18.90), bound in b1: the
 * capture's block in the ring when they are as captured (not `fresh`) and it
 * still holds it, else the 192 registers (twoc step 95: as 12 blocks of 16,
 * `blk`) put in the ring, or (no ring) uploaded into g_vsh_cb unless they
 * are the blocks uploaded by the baked draw before and not `fresh`.
 * g_vsh_constants is left as it is, so the next ordinary draw uploads it
 * again. */
void d3d8_vsh_baked_consts(const D3D8Baked *b, const float *const *blk, BOOL fresh)
{
    ID3D11DeviceContext *ctx = d3d8_GetD3D11Context();
    D3D11_MAPPED_SUBRESOURCE mapped;

    if (g_kring_state > 0) {
        UINT off;
        if (!fresh && b->k_gen && b->k_gen == g_kring_gen) {
            kring_bind(b->k_first);
            return;
        }
        if ((off = kring_write2(NULL, blk)) != (UINT)-1) {
            kring_bind(off);
            g_vsh_constants_dirty = TRUE;
            return;
        }
    }
    d3d8_bind_vs_cb(ctx, 1, g_vsh_cb);
    if (!fresh && g_vsh_baked_last[0] && memcmp((const void *)g_vsh_baked_last, (const void *)blk, sizeof(g_vsh_baked_last)) == 0)
        return;
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)g_vsh_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        int i;
        for (i = 0; i < 12; i++)
            memcpy((BYTE *)mapped.pData + 256 * i, blk[i], 256);
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)g_vsh_cb, 0);
    }
    g_vsh_constants_dirty = TRUE;
    if (fresh)
        g_vsh_baked_last[0] = NULL;
    else
        memcpy((void *)g_vsh_baked_last, (const void *)blk, sizeof(g_vsh_baked_last));
}

/* Twoc step 95 (cut 3): a capture from the game frame before drawn again
 * (d3d8_DrawBakedAgain): the program's constants (g_vsh_constants, as the
 * caller's SetVertexShaderConstant calls left them) taken as a capturing
 * draw takes them in d3d8_vsh_prepare_draw: into the ring unless the block
 * there already holds them, bound in b1, and `b` told where for this
 * frame's later passes (k_gen 0 without the ring: g_vsh_cb uploaded when
 * dirty and bound, as before). */
void d3d8_vsh_bake_again(D3D8Baked *b)
{
    ID3D11DeviceContext *ctx = d3d8_GetD3D11Context();
    D3D11_MAPPED_SUBRESOURCE mapped;

    b->k_gen = 0;
    if (kring_ready()) {
        if (!g_kring_cur_ok || g_kring_cur_gen != g_kring_gen) {
            UINT off = kring_write(&g_vsh_constants);
            if (off != (UINT)-1) {
                g_kring_cur = off;
                g_kring_cur_gen = g_kring_gen;
                g_kring_cur_ok = TRUE;
            }
        }
        if (g_kring_cur_ok && g_kring_cur_gen == g_kring_gen) {
            kring_bind(g_kring_cur);
            g_vsh_constants_dirty = TRUE;
            g_vsh_baked_last[0] = NULL;
            b->k_gen = g_kring_cur_gen;
            b->k_first = g_kring_cur;
            return;
        }
    }
    if (g_vsh_constants_dirty) {
        if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)g_vsh_cb, 0, D3D11_MAP_WRITE_DISCARD, 0,
                                              &mapped))) {
            memcpy(mapped.pData, &g_vsh_constants, sizeof(g_vsh_constants));
            ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)g_vsh_cb, 0);
        }
        g_vsh_constants_dirty = FALSE;
        g_vsh_baked_last[0] = NULL;
    }
    d3d8_bind_vs_cb(ctx, 1, g_vsh_cb);
}

/* The frame's captures begin again (d3d8_BakeReset): a block given before
 * may be at the same address with other constants now. */
void d3d8_vsh_bake_reset(void)
{
    g_vsh_baked_last[0] = NULL;
    /* The ring starts over now if the frame may not fit (a quarter more than
     * the last frame put in it, and 1 MB). */
    if ((UINT64)g_kring_off + g_kring_frame_bytes + g_kring_frame_bytes / 4 + (1u << 20) > KRING_SIZE)
        g_kring_off = KRING_SIZE;
    if (g_kring_frame_bytes > g_kring_peak)
        g_kring_peak = g_kring_frame_bytes;
    g_kring_last = g_kring_frame_bytes;
    g_kring_frame_bytes = 0;
}

void d3d8_vsh_mem_stats(UINT *size, UINT *frame, UINT *peak, UINT *gen)
{
    *size = g_kring_state == 1 ? KRING_SIZE : 0;
    *frame = g_kring_last;
    *peak = g_kring_peak;
    *gen = g_kring_gen;
}
