/*
 * gpfx: run the MCPX GP's effects program offline on the dsp56300 emulator
 * (github.com/mborgerson/dsp56300, MIT) over mixbin dumps (crash_twoc notes
 * 18.75, reverb stage 0). Nothing here is linked into crash_twoc.
 *
 * The GP boots the way the hardware does: the first 0x800 words of scratch
 * (DSOUND's loader at the start of the effects image) become P memory, the
 * loader reads the 6-word command block at scratch 0x800 (x0 X offset, x1
 * code words, x2 state scratch offset, x3 state words, x4 command: bit 0
 * load the code to P:$171, bit 1 load the state to X:$80+x0), then each frame
 * waits for the start-frame bit, runs the effects code, DMAs bins 0-5 to
 * rings in scratch, and asks to halt. Scratch is a flat buffer here: the
 * image at 0, the delay lines after it.
 *
 * Memory map and peripherals as the hardware has them (the facts follow
 * xemu's hw/xbox/mcpx/apu/dsp, LGPL-2.1+ for dsp_dma.c; the code is new):
 * X 0-0xFFF RAM with the 32 mixbins at X:0x1400-0x17FF aliasing X:0xC00-0xFFF,
 * Y 0-0x7FF, P 0-0xFFF; X:FFFFC4 bit 0 = done for this frame, X:FFFFC5 the
 * interrupt bits (1 start frame, 0x80 DMA end of list), X:FFFFD4-D7 the DMA
 * engine's next block, start block, control, configuration. A DMA block is
 * 7 words: next, control, count, DSP address, scratch offset, scratch base,
 * scratch size - 1.
 *
 * gpfx --image IMG --out OUT.f32 [--bins B.bins --from S --to S]
 *      [--synth impulse|noise:BIN[,BIN..]:AMP --secs S --t0 S] [--zero BIN,..]
 *      [--cmd x0,x1,x2,x3,x4] [--post SEC:IMG:x0,x1,x2,x3,x4]
 * IMG: the image copy as fxlog.py dumps it (scratch 0 on). --cmd is the boot
 * command (default the game file's: 0,0xC44,0x3928,0x9FA,3). --post does what
 * DSOUND does on a preset change: at SEC copy IMG's words x2..x2+4*x3 into
 * scratch and post the block (a preset: 4,0xC44,0x3938,0x84,2); boot from an
 * image whose fx0 flags word has bit 0 clear (fx0's init runs once).
 * B.bins: float32, 32 samples x 8 bins (0,1,6,7,8,9,10,11) a frame (the
 * step-71 tap). --synth feeds an impulse (at --t0, default 0.02 s) or white
 * noise (from --t0, rms AMP) into the bins listed. --zero clears bins (10:
 * the reverb's input). OUT.f32: float32, bins 0-5 after the GP, a row a sample.
 * Diagnostics: GPFX_TRACE=A-B logs every DMA block of frames A-B (single
 * stepped, with the PC that started each transfer), GPFX_WATCH=a-b (hex X
 * addresses) adds the PC of every write there in those frames, GPFX_XLOG=A-B
 * which 16-word X buffers hold signal, GPFX_DUMP=PREFIX scratch and X/Y/P at
 * the end (gpwin.py's input).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "dsp56300.h"

#define NBINS 32
#define NS 32
#define SCRATCH_SIZE (2u << 20)

static uint32_t xram[0x1000], yram[0x800], pram[0x1000];
static uint8_t *scratch;
static uint32_t scratch_hi;
static Dsp56300Jit *dsp;

static uint32_t irq;          /* X:FFFFC5 */
static int eol;
static uint32_t dma_cfg, dma_ctl, dma_start, dma_next, dma_reads;
static unsigned long long n_dma_blocks, n_fifo_bytes, n_unknown;
static long long cur_frame, trace_a = -1, trace_b = -1;   /* GPFX_TRACE=A-B: log DMA blocks of frames A-B */

static uint32_t mem_rd(int sp, uint32_t a)
{
    if (sp == 0) {
        if (a < 0x1000) return xram[a];
        if (a >= 0x1400 && a < 0x1800) return xram[0xC00 + a - 0x1400];
    } else if (sp == 1) {
        if (a < 0x800) return yram[a];
    } else if (a < 0x1000) {
        return pram[a];
    }
    n_unknown++;
    return 0;
}

static void mem_wr(int sp, uint32_t a, uint32_t v)
{
    v &= 0xFFFFFF;
    if (sp == 0) {
        if (a < 0x1000) { xram[a] = v; return; }
        if (a >= 0x1400 && a < 0x1800) { xram[0xC00 + a - 0x1400] = v; return; }
    } else if (sp == 1) {
        if (a < 0x800) { yram[a] = v; return; }
    } else if (a < 0x1000) {
        dsp56300_write_memory(dsp, DSP56300_MEM_SPACE_P, a, v); /* marks the JIT's dirty map */
        return;
    }
    n_unknown++;
}

/* DMA address space: X below 0x1800, Y at 0x1800, P at 0x2800 */
static int dma_space(uint32_t a, uint32_t *off)
{
    if (a < 0x1800) { *off = a; return 0; }
    if (a >= 0x1800 && a < 0x2000) { *off = a - 0x1800; return 1; }
    if (a >= 0x2800 && a < 0x3800) { *off = a - 0x2800; return 2; }
    fprintf(stderr, "gpfx: DMA address %06x out of range\n", a);
    *off = 0;
    return 0;
}

static void scratch_rw(uint8_t *buf, uint32_t addr, uint32_t len, int to_scratch)
{
    if (addr + len > SCRATCH_SIZE) {
        fprintf(stderr, "gpfx: scratch access %x+%x past the buffer\n", addr, len);
        exit(2);
    }
    if (addr + len > scratch_hi) scratch_hi = addr + len;
    if (to_scratch) memcpy(scratch + addr, buf, len);
    else memcpy(buf, scratch + addr, len);
}

static void dma_run(void)
{
    static uint8_t buf[0x10000 * 4];
    if (!(dma_ctl & 0x10) || (dma_ctl & 0x08))
        return;
    while (!(dma_next & 0x4000)) {
        uint32_t ba, d_off, mem_a;
        int bs = dma_space(dma_next & 0x3FFF, &ba);
        uint32_t next = mem_rd(bs, ba), ctl = mem_rd(bs, ba + 1), count = mem_rd(bs, ba + 2);
        uint32_t dsp_a = mem_rd(bs, ba + 3), s_off = mem_rd(bs, ba + 4);
        uint32_t s_base = mem_rd(bs, ba + 5), s_size = mem_rd(bs, ba + 6) + 1;
        int interleave = ctl & 1, to_scratch = (ctl >> 1) & 1, writeback = (ctl >> 4) & 1;
        uint32_t buf_id = (ctl >> 5) & 0xF, fmt = (ctl >> 10) & 7;
        uint32_t isz = fmt == 0 ? 1 : fmt == 1 ? 2 : 4, nitems = count, len;
        int ms = dma_space(dsp_a, &mem_a);
        n_dma_blocks++;
        if (cur_frame >= trace_a && cur_frame <= trace_b)
            fprintf(stderr, "f%lld blk %c:%04x next %04x ctl %06x (%s buf %x fmt %u il %d wb %d) count %x dsp %06x soff %x sbase %x ssize %x\n",
                    cur_frame, "XYP"[bs], ba, next, ctl, to_scratch ? "to" : "from", buf_id, fmt, interleave, writeback,
                    count, dsp_a, s_off, s_base, s_size);
        dma_next = next;
        if (next & 0x4000) eol = 1;
        if (fmt != 1 && fmt != 2 && fmt != 6) {
            fprintf(stderr, "gpfx: DMA format %u not handled\n", fmt);
            exit(2);
        }
        if (to_scratch) {
            uint32_t chans = interleave ? (count & 0xF) + 1 : 1, blocks = interleave ? count >> 4 : count;
            nitems = chans * blocks;
            for (uint32_t i = 0; i < blocks; i++)
                for (uint32_t c = 0; c < chans; c++) {
                    uint32_t v = mem_rd(ms, mem_a + c * blocks + i), k = i * chans + c;
                    if (isz == 2) ((uint16_t *)buf)[k] = (uint16_t)(v >> 8);
                    else ((uint32_t *)buf)[k] = v;
                }
        }
        len = nitems * isz;
        if (buf_id <= 3) {
            if (!to_scratch) { fprintf(stderr, "gpfx: FIFO read not handled\n"); exit(2); }
            n_fifo_bytes += len; /* GP output FIFOs: unused by this image */
        } else if (buf_id == 0xE) {
            uint32_t done = 0;
            if (s_off >= s_size) s_off = 0;
            while (done < len) {
                uint32_t chunk = s_size - s_off;
                if (chunk > len - done) chunk = len - done;
                scratch_rw(buf + done, s_base + s_off, chunk, to_scratch);
                s_off += chunk;
                if (s_off >= s_size) s_off = 0;
                done += chunk;
            }
        } else if (buf_id == 0xF) {
            scratch_rw(buf, s_base + s_off, len, to_scratch);
        } else {
            fprintf(stderr, "gpfx: DMA buffer %x not handled\n", buf_id);
            exit(2);
        }
        if (!to_scratch)
            for (uint32_t i = 0; i < nitems; i++) {
                uint32_t v = isz == 2 ? (uint32_t)((uint16_t *)buf)[i] << 8 : ((uint32_t *)buf)[i] & 0xFFFFFF;
                mem_wr(ms, mem_a + i, v);
            }
        if (cur_frame >= trace_a && cur_frame <= trace_b && isz == 4) {
            double e = 0;
            for (uint32_t i = 0; i < nitems; i++) { int32_t w = (int32_t)(((uint32_t *)buf)[i] << 8) >> 8; e += (double)w * w; }
            fprintf(stderr, "f%lld     %s %06x..: %s\n", cur_frame, to_scratch ? "wrote" : "read", dsp_a,
                    e > 0 ? "signal" : "zero");
            if (e > 0) fprintf(stderr, "f%lld     level %.1f dB\n", cur_frame, 10 * log10(e / nitems) - 138.47);
        }
        if (writeback)
            mem_wr(bs, ba + 4, s_off);
    }
}

static uint32_t periph_rd(void *op, uint32_t a)
{
    (void)op;
    switch (a) {
    case 0xFFFFB3: return 0;
    case 0xFFFFC5: return irq | (eol ? 0x80 : 0);
    case 0xFFFFD4: return dma_next;
    case 0xFFFFD5: return dma_start;
    case 0xFFFFD6:
        if (dma_ctl & 0x10 && ++dma_reads > 2) {  /* the transfer is done after a few polls */
            dma_ctl = (dma_ctl & ~0x10u) | 0x20;
            dma_reads = 0;
        }
        return dma_ctl;
    case 0xFFFFD7: return dma_cfg;
    }
    n_unknown++;
    return 0;
}

static void periph_wr(void *op, uint32_t a, uint32_t v)
{
    (void)op;
    switch (a) {
    case 0xFFFFC4: if (v & 1) dsp56300_set_halt_requested(dsp, true); return;
    case 0xFFFFC5: irq &= ~v; if (v & 0x80) eol = 0; return;
    case 0xFFFFD4: dma_next = v; return;
    case 0xFFFFD5: dma_start = v; return;
    case 0xFFFFD7: dma_cfg = v; return;
    case 0xFFFFD6:
        switch (v & 7) {
        case 1: dma_ctl = (dma_ctl | 0x10) & ~0x20u; dma_reads = 0; break;
        case 2: dma_ctl = (dma_ctl | 0x20) & ~0x10u; break;
        case 3: dma_ctl |= 0x08; break;
        case 4: dma_ctl &= ~0x08u; break;
        default: fprintf(stderr, "gpfx: DMA action %u\n", v & 7); exit(2);
        }
        dma_run();
        return;
    case 0xFFFFB0: case 0xFFFFB1: case 0xFFFFB2: return;  /* timer setup */
    }
    n_unknown++;
}

static uint8_t *load(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb");
    uint8_t *d;
    if (!f) { fprintf(stderr, "gpfx: can't open %s\n", p); exit(1); }
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    d = malloc(*n);
    if (fread(d, 1, *n, f) != *n) exit(1);
    fclose(f);
    return d;
}

static void post(uint32_t c[5])
{
    for (int k = 0; k < 5; k++) ((uint32_t *)scratch)[0x200 + k] = c[k];
}

static uint32_t f2w(float f)
{
    double s = f * 8388608.0;
    int v = s >= 8388607.0 ? 8388607 : s <= -8388608.0 ? -8388608 : (int)s;
    return (uint32_t)v & 0xFFFFFF;
}

static int parse5(const char *s, uint32_t c[5])
{
    return sscanf(s, "%i,%i,%i,%i,%i", (int *)&c[0], (int *)&c[1], (int *)&c[2], (int *)&c[3], (int *)&c[4]) == 5;
}

int main(int argc, char **argv)
{
    const char *img = 0, *out = 0, *bins = 0, *synth = 0, *post_img = 0;
    double from = 0, to = 1e9, secs = 4, post_sec = -1, t0 = 0.02;
    uint32_t cmd[5] = {0, 0xC44, 0x3928, 0x9FA, 3}, pcmd[5];
    int zero[NBINS] = {0};
    static const int sel[8] = {0, 1, 6, 7, 8, 9, 10, 11};
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : "";
        if (!strcmp(a, "--image")) img = v, i++;
        else if (!strcmp(a, "--out")) out = v, i++;
        else if (!strcmp(a, "--bins")) bins = v, i++;
        else if (!strcmp(a, "--from")) from = atof(v), i++;
        else if (!strcmp(a, "--to")) to = atof(v), i++;
        else if (!strcmp(a, "--synth")) synth = v, i++;
        else if (!strcmp(a, "--secs")) secs = atof(v), i++;
        else if (!strcmp(a, "--t0")) t0 = atof(v), i++;
        else if (!strcmp(a, "--cmd")) { if (!parse5(v, cmd)) return 1; i++; }
        else if (!strcmp(a, "--zero")) {
            for (const char *p = v; *p; ) { zero[atoi(p) & 31] = 1; while (*p && *p != ',') p++; if (*p) p++; }
            i++;
        } else if (!strcmp(a, "--post")) {
            static char tmp[1024];
            char *c1, *c2;
            snprintf(tmp, sizeof tmp, "%s", v);
            c1 = strchr(tmp, ':'); c2 = c1 ? strrchr(tmp, ':') : 0;
            if (!c1 || c2 == c1) return 1;
            *c1 = 0; *c2 = 0;
            post_sec = atof(tmp); post_img = c1 + 1;
            if (!parse5(c2 + 1, pcmd)) return 1;
            i++;
        } else { fprintf(stderr, "gpfx: unknown %s\n", a); return 1; }
    }
    if (!img || !out || (!bins && !synth)) {
        fprintf(stderr, "usage: gpfx --image IMG --out OUT.f32 (--bins B --from S --to S | --synth impulse|noise:BIN,..:AMP --secs S) [--zero B,..] [--cmd x0,x1,x2,x3,x4] [--post SEC:IMG:x0,x1,x2,x3,x4]\n");
        return 1;
    }

    size_t n;
    uint8_t *im = load(img, &n);
    scratch = calloc(1, SCRATCH_SIZE);
    memcpy(scratch, im, n);
    post(cmd);

    Dsp56300MemoryRegion xr[3] = {
        {0x0000, 0x1000, DSP56300_REGION_BUFFER, {.buffer = {xram, 0}}},
        {0x1400, 0x1800, DSP56300_REGION_BUFFER, {.buffer = {xram, 0xC00}}},
        {0xFFFF80, 0x1000000, DSP56300_REGION_CALLBACK, {.callback = {0, periph_rd, periph_wr}}},
    };
    Dsp56300MemoryRegion yr[1] = {{0, 0x800, DSP56300_REGION_BUFFER, {.buffer = {yram, 0}}}};
    Dsp56300MemoryRegion pr[1] = {{0, 0x1000, DSP56300_REGION_BUFFER, {.buffer = {pram, 0}}}};
    Dsp56300CreateInfo info = {{xr, 3, yr, 1, pr, 1}};
    dsp = dsp56300_create(&info);
    dsp56300_reset(dsp);
    for (int i = 0; i < 0x800; i++) pram[i] = ((uint32_t *)scratch)[i] & 0xFFFFFF;  /* the boot ROM's copy */
    dsp56300_invalidate_cache(dsp);

    FILE *bf = 0;
    long long f0 = 0, nfr;
    if (bins) {
        bf = fopen(bins, "rb");
        if (!bf) { fprintf(stderr, "gpfx: can't open %s\n", bins); return 1; }
        _fseeki64(bf, 0, SEEK_END);
        long long tot = _ftelli64(bf) / (NS * 8 * 4);
        f0 = (long long)(from * 1500);
        nfr = (long long)(to * 1500);
        if (nfr > tot) nfr = tot;
        nfr -= f0;
        _fseeki64(bf, f0 * NS * 8 * 4, SEEK_SET);
    } else {
        nfr = (long long)(secs * 1500);
    }
    int sb[NBINS] = {0}, nsb = 0, noise = 0;
    float amp = 0;
    if (synth) {
        const char *p = strchr(synth, ':');
        noise = !strncmp(synth, "noise", 5);
        if (!p) return 1;
        for (p++; *p && *p != ':'; ) { sb[nsb++] = atoi(p); while (*p && *p != ',' && *p != ':') p++; if (*p == ',') p++; }
        amp = (float)atof(*p == ':' ? p + 1 : "0.5");
    }

    FILE *of = fopen(out, "wb");
    float mix[NBINS][NS], fr[NS][8], o[NS][6];
    uint32_t rng = 12345;
    unsigned long long cyc_sum = 0, cyc_max = 0, overruns = 0;
    long long f_t0 = (long long)(t0 * 1500);
    long long post_frame = post_sec >= 0 ? (long long)(post_sec * 1500) : -1;
    /* frame -1 runs the loader's init (it ends in the frame loop's halt) */
    long long xlog_a = -1, xlog_b = -1;
    if (getenv("GPFX_TRACE")) sscanf(getenv("GPFX_TRACE"), "%lld-%lld", &trace_a, &trace_b);
    if (getenv("GPFX_XLOG")) sscanf(getenv("GPFX_XLOG"), "%lld-%lld", &xlog_a, &xlog_b);
    for (long long f = -1; f < nfr; f++) {
        cur_frame = f;
        memset(mix, 0, sizeof mix);
        if (f >= 0 && bf) {
            if (fread(fr, sizeof fr, 1, bf) != 1) break;
            for (int i = 0; i < NS; i++)
                for (int k = 0; k < 8; k++) mix[sel[k]][i] = fr[i][k];
        } else if (f >= 0 && synth) {
            for (int i = 0; i < NS; i++) {
                float s = 0;
                if (noise) {
                    float u = 0;
                    for (int k = 0; k < 4; k++) { rng = rng * 1664525u + 1013904223u; u += (rng >> 8) / 16777216.0f - 0.5f; }
                    s = f >= f_t0 ? amp * u * 1.7320508f : 0;  /* sum of 4 uniforms, rms = amp */
                } else if (f == f_t0 && i == 0) {
                    s = amp;
                }
                for (int k = 0; k < nsb; k++) mix[sb[k]][i] = s;
            }
        }
        if (f == post_frame && post_img) {
            size_t pn;
            uint8_t *pi = load(post_img, &pn);
            memcpy(scratch + pcmd[2], pi + pcmd[2], pcmd[3] * 4);
            post(pcmd);
            free(pi);
        }
        for (int b = 0; b < NBINS; b++)
            for (int i = 0; i < NS; i++) xram[0xC00 + b * NS + i] = zero[b] ? 0 : f2w(mix[b][i]);
        irq |= 2;  /* start of frame */
        dsp56300_set_halt_requested(dsp, false);
        dsp56300_set_cycle_count(dsp, 0);
        if (f >= trace_a && f <= trace_b) {
            /* traced frame: one instruction at a time, the PC after each DMA start */
            unsigned long long k0 = n_dma_blocks;
            static uint32_t shadow[0x1000];
            uint32_t wa = 0, wb = 0;   /* GPFX_WATCH=A-B (X addresses, hex): writes by PC */
            if (getenv("GPFX_WATCH")) sscanf(getenv("GPFX_WATCH"), "%x-%x", &wa, &wb);
            memcpy(shadow, xram, sizeof shadow);
            while (!dsp56300_halt_requested(dsp) && dsp56300_cycle_count(dsp) < 4000000) {
                Dsp56300State ts;
                dsp56300_get_state(dsp, &ts);
                dsp56300_step(dsp);
                if (n_dma_blocks != k0) {
                    fprintf(stderr, "f%lld   ^ started at P:%04x, cycle %u\n", f, ts.pc, dsp56300_cycle_count(dsp));
                    k0 = n_dma_blocks;
                    memcpy(shadow, xram, sizeof shadow);
                }
                for (uint32_t a = wa; a < wb && a < 0x1000; a++)
                    if (xram[a] != shadow[a]) {
                        fprintf(stderr, "f%lld   P:%04x wrote X:%03x %06x -> %06x\n", f, ts.pc, a, shadow[a], xram[a]);
                        shadow[a] = xram[a];
                    }
            }
        } else {
            do dsp56300_run(dsp, 1000);
            while (!dsp56300_halt_requested(dsp) && dsp56300_cycle_count(dsp) < 4000000);
        }
        unsigned c = dsp56300_cycle_count(dsp);
        if (!dsp56300_halt_requested(dsp)) overruns++;
        if (f >= 0) { cyc_sum += c; if (c > cyc_max) cyc_max = c; }
        if (f < 0) continue;
        if (f >= xlog_a && f <= xlog_b) {  /* GPFX_XLOG=A-B: which 16-word X buffers hold signal */
            fprintf(stderr, "f%lld X:", f);
            for (uint32_t a = 0x600; a < 0xC00; a += 0x10) {
                double e = 0;
                for (int k = 0; k < 16; k++) { int32_t w = (int32_t)(xram[a + k] << 8) >> 8; e += (double)w * w; }
                if (e > 0) fprintf(stderr, " %03x:%.0f", a, 10 * log10(e / 16) - 138.47);
            }
            fprintf(stderr, "\n");
        }
        for (int i = 0; i < NS; i++)
            for (int b = 0; b < 6; b++) {
                int32_t w = (int32_t)(xram[0xC00 + b * NS + i] << 8) >> 8;
                o[i][b] = w / 8388608.0f;
            }
        fwrite(o, sizeof o, 1, of);
    }
    fclose(of);
    if (getenv("GPFX_DUMP")) {  /* GPFX_DUMP=PREFIX: scratch and X/Y/P memory at the end */
        char p[1024];
        FILE *df;
        snprintf(p, sizeof p, "%s.scratch", getenv("GPFX_DUMP"));
        if ((df = fopen(p, "wb"))) { fwrite(scratch, 1, scratch_hi, df); fclose(df); }
        snprintf(p, sizeof p, "%s.xyp", getenv("GPFX_DUMP"));
        if ((df = fopen(p, "wb"))) { fwrite(xram, 4, 0x1000, df); fwrite(yram, 4, 0x800, df); fwrite(pram, 4, 0x1000, df); fclose(df); }
    }
    Dsp56300State st;
    dsp56300_get_state(dsp, &st);
    printf("frames %lld cycles/frame mean %.0f max %llu overruns %llu pc %06x dma blocks %llu fifo bytes %llu "
           "scratch hi %#x unknown accesses %llu cmd now %x\n",
           nfr, nfr ? (double)cyc_sum / nfr : 0, cyc_max, overruns, st.pc, n_dma_blocks, n_fifo_bytes,
           scratch_hi, n_unknown, ((uint32_t *)scratch)[0x204]);
    return 0;
}
