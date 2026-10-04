/*
 * MCPX APU DSP (GP/EP) - Stub implementation
 *
 * The DSP Global Processor (GP) and Encode Processor (EP) handle effects
 * processing (reverb, chorus, etc.) and final output encoding. The full
 * DSP is ~3000 lines of DSP56300 emulation code.
 *
 * For initial audio, we bypass the DSP entirely:
 * - VP mixbins are passed directly to the EP output
 * - GP effects processing is skipped
 * - The EP just copies mixbin 0/1 (front L/R) to the monitor buffer
 *
 * This gives us basic voice playback without effects. The DSP can be
 * connected later for reverb, EQ, and other processing.
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2019-2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "apu_state.h"
#include "apu.h"
#include "fpconv.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ── DSP command doorbell acknowledgement ────────────────────────────────
 *
 * DirectSound does not stop at creating the device. It hands the audio DSP a
 * command block in guest RAM, writes a command word, and spins until the DSP
 * writes zero back. On real hardware the GP runs a DSP56300 program that does
 * that. Here the DSP is a passthrough stub, so the word never changes and the
 * title hangs inside DirectSound initialisation -- which on Wreckless gates the
 * entire engine, not just audio.
 *
 * RECOMP_APU_DSP_ACK=<addr>[,<addr>...] clears those guest dwords once per APU
 * frame, which is what "the command completed" looks like to the title.
 *
 * ponytail: this is a handshake acknowledgement, not a DSP. It says every
 * command succeeded instantly and computes nothing, so anything whose *result*
 * the title reads back will still be wrong. The real fix is DSP56300 emulation
 * in the GP/EP; this exists so audio init stops blocking everything behind it.
 *
 * The address is not derivable from the APU registers: GPSADDR/GPFADDR/
 * EPSADDR/EPFADDR point at the DSP's own scratch and frame memory, while the
 * command block is a DirectSound heap allocation. On Wreckless the registers
 * read 0x01504000 / 0x014EC000 / 0x0151C000 / 0x014F0000 and the doorbell is at
 * 0x014F8810 -- inside none of them. So it has to be observed: run with
 * RECOMP_WATCHDOG_SECS and the spin shows up as ebx plus the poll offset.
 */
#define APU_DSP_ACK_MAX 8
static uint32_t s_dsp_ack[APU_DSP_ACK_MAX];
static int s_dsp_ack_count = -1;

static void dsp_ack_init(void)
{
    const char *spec = getenv("RECOMP_APU_DSP_ACK");
    char buf[256], *p, *end;

    s_dsp_ack_count = 0;
    if (!spec || !*spec)
        return;
    strncpy(buf, spec, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    for (p = buf; *p && s_dsp_ack_count < APU_DSP_ACK_MAX; ) {
        unsigned long v = strtoul(p, &end, 0);
        if (end == p)
            break;
        if (v)
            s_dsp_ack[s_dsp_ack_count++] = (uint32_t)v;
        p = (*end == ',') ? end + 1 : end;
    }
    if (s_dsp_ack_count)
        fprintf(stderr, "[APU] DSP doorbell ack: %d address(es), first 0x%08X\n",
                s_dsp_ack_count, s_dsp_ack[0]);
}

/* HOW THE BINS BECOME STEREO WITHOUT THE GP.
 *
 * On hardware the GP runs DirectSound's effects image, and that program, not
 * the VP, decides how each mixbin reaches the speakers. Measured against xemu
 * running the real image (Crash Bandicoot: The Wrath of Cortex, the Arctic
 * Antics attract demo, notes 18.49):
 *
 *   - the speaker bins leave at the level the VP wrote them. Headroom is
 *     applied once, in the VP (1 << submix_headroom), and the image does not
 *     undo it: the menu music and the level's 2D ambience match xemu;
 *   - the 3D voices' HRTF submix bins (the four named by hrtf_submix, the
 *     crosstalk bins 6-9 by DirectSound's convention) come out about 11 dB
 *     below unity (median -10.9 dB over 17 windows, quartiles -12.1 and
 *     -8.8), measured from music-to-rest ratios, which a compressor in the
 *     playback chain leaves alone;
 *   - the I3DL2 send (bin 10) and the FX sends are inputs to the image's
 *     effects, not outputs. Summed dry at unit gain they made the effects
 *     and the hub's ambience 6-10 dB louder than the music.
 *
 * So by default: front left/right at unity, center and the back pair at
 * -3 dB into both sides, LFE dropped (a stereo fold-down), the HRTF submix
 * bins at -11 dB (-2.6 dB with the VP's HRTF on, below; RECOMP_APU_XTLK_DB
 * overrides both), every other bin dropped.
 * No reverb: that needs the DSP.
 *
 * RECOMP_APU_MIXDOWN=all is the previous mixdown (every bin summed at unit
 * gain, even bins left and odd right), =front the one before that (bins 0 and
 * 1 only). RECOMP_APU_MIXDOWN_ALL=0 still means front. */
enum { MIXDOWN_GP, MIXDOWN_ALL, MIXDOWN_FRONT };

static int mcpx_apu_mixdown_mode(void)
{
    static int mode = -1;
    if (mode < 0) {
        const char *e = getenv("RECOMP_APU_MIXDOWN");
        const char *a = getenv("RECOMP_APU_MIXDOWN_ALL");
        mode = MIXDOWN_GP;
        if (e && !strcmp(e, "all"))
            mode = MIXDOWN_ALL;
        else if ((e && !strcmp(e, "front")) || (a && *a && atoi(a) == 0))
            mode = MIXDOWN_FRONT;
    }
    return mode;
}

/* The -11 dB above was fitted against xemu with its HRTF filter on, while
 * this APU's was off: it held the filter's loss as well as the GP's gain.
 * With the filter on here too (mcpx_apu_set_hrtf), the 3D submixes come out
 * 8.4 dB lower (the attract demo's 3D bins, 55 s; crash_twoc notes 18.59), so
 * they take -2.6 dB, which keeps the balance the -11 dB gave. */
static float mcpx_apu_xtlk_gain(void)
{
    static float g = -1.0f, g_hrtf;
    if (g < 0.0f) {
        const char *e = getenv("RECOMP_APU_XTLK_DB");
        g = powf(10.0f, (float)((e && *e) ? atof(e) : -11.0) / 20.0f);
        g_hrtf = (e && *e) ? g : powf(10.0f, -2.6f / 20.0f);
    }
    return mcpx_apu_get_hrtf() ? g_hrtf : g;
}

/* Per-bin gains to the left and right outputs for MIXDOWN_GP. The HRTF
 * submixes are guest state (NV1BA0_PIO_SET_HRTF_SUBMIXES): 0 and 1 are the
 * left pair, 2 and 3 the right. A title that never sets them leaves them at
 * 0, a speaker bin, which is then counted once, as a speaker bin. */
static void mixdown_weights(MCPXAPUState *d, float wl[NUM_MIXBINS], float wr[NUM_MIXBINS])
{
    const float c3 = 0.70710678f, gx = mcpx_apu_xtlk_gain();
    memset(wl, 0, sizeof(float) * NUM_MIXBINS);
    memset(wr, 0, sizeof(float) * NUM_MIXBINS);
    wl[0] = 1.0f;  wr[1] = 1.0f;          /* front left, front right */
    wl[2] = c3;    wr[2] = c3;            /* center */
    wl[4] = c3;    wr[5] = c3;            /* back left, back right */
    for (int k = 0; k < 4; k++) {
        int b = d->vp.hrtf_submix[k];
        if (b < 6 || b >= NUM_MIXBINS || wl[b] != 0.0f || wr[b] != 0.0f)
            continue;
        if (k < 2) wl[b] = gx;
        else       wr[b] = gx;
    }
}

/* 5.1 (notes 18.58): the speaker bins go to their own channels at unity
 * (bins 0-5 are FL FR C LFE BL BR), and each HRTF submix bin to its
 * speaker at the same gain as in stereo: the submixes 0 and 2 are the front
 * pair, 1 and 3 the back (Crash names bins 6, 8, 7, 9: DirectSound's
 * crosstalk front left, back left, front right, back right). Nothing is
 * folded down, and the sends stay dropped as in stereo.
 * RECOMP_APU_MIXDOWN's older mixdowns are stereo only. */
static void mixdown_51(MCPXAPUState *d, float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME], int off)
{
    static const int speaker_of_submix[4] = { 0, 4, 1, 5 };
    float w[6][NUM_MIXBINS];
    const float gx = mcpx_apu_xtlk_gain();

    memset(w, 0, sizeof(w));
    for (int c = 0; c < 6; c++)
        w[c][c] = 1.0f;
    for (int k = 0; k < 4; k++) {
        int b = d->vp.hrtf_submix[k], used = 0;
        if (b < 6 || b >= NUM_MIXBINS)
            continue;
        for (int c = 0; c < 6; c++)
            used |= w[c][b] != 0.0f;
        if (!used)
            w[speaker_of_submix[k]][b] = gx;
    }
    for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
        for (int c = 0; c < 6; c++) {
            float s = 0.0f;
            for (int b = 0; b < NUM_MIXBINS; ++b)
                s += w[c][b] * mixbins[b][i];
            if (s > 1.0f) s = 1.0f;
            if (s < -1.0f) s = -1.0f;
            d->monitor.frame_buf6[off + i][c] = (int16_t)(s * 32767.0f);
        }
    }
}

static void dsp_ack_frame(MCPXAPUState *d)
{
    int i;

    if (s_dsp_ack_count < 0)
        dsp_ack_init();
    if (!d->ram_ptr)
        return;
    for (i = 0; i < s_dsp_ack_count; i++) {
        uint32_t *slot = (uint32_t *)(d->ram_ptr + s_dsp_ack[i]);
        if (*slot) {
            static int shown[APU_DSP_ACK_MAX];
            if (shown[i]++ < 3)
                fprintf(stderr, "[APU] DSP doorbell 0x%08X: command 0x%08X"
                                " acknowledged\n", s_dsp_ack[i], *slot);
            *slot = 0;
        }
    }
}

void mcpx_apu_dsp_init(MCPXAPUState *d)
{
    /* Allocate minimal DSP state for GP and EP.
     * We need these to exist so reset doesn't crash,
     * but they won't actually run DSP programs. */
    d->gp.dsp = (DSPState *)calloc(1, sizeof(DSPState));
    d->ep.dsp = (DSPState *)calloc(1, sizeof(DSPState));

    if (d->gp.dsp) d->gp.dsp->is_gp = true;
    if (d->ep.dsp) d->ep.dsp->is_gp = false;

    d->gp.realtime = false;
    d->ep.realtime = false;

    fprintf(stderr, "[APU] DSP GP/EP initialized (STUBBED - passthrough mode)\n");
}

void mcpx_apu_update_dsp_preference(MCPXAPUState *d)
{
    /* In the real xemu, this reads settings to decide whether
     * GP/EP should run in realtime or cached mode. We ignore it. */
    (void)d;
}

void mcpx_apu_dsp_frame(MCPXAPUState *d,
                         float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME])
{
    /* Bypass DSP: take mixbin 0 (front-left) and mixbin 1 (front-right)
     * and write them directly to the monitor frame buffer as the final
     * EP output.
     *
     * The Xbox DirectSound typically routes:
     *   Mixbin 0 = Front Left
     *   Mixbin 1 = Front Right
     *   Mixbin 2 = Center (often unused in stereo)
     *   Mixbin 3 = LFE
     *   Mixbin 4-5 = Rear L/R
     *
     * For stereo output, bins 0 and 1 are what we want.
     */

    int off = (d->ep_frame_div % 8) * NUM_SAMPLES_PER_FRAME;

    dsp_ack_frame(d);

    if (d->monitor.point != MCPX_APU_DEBUG_MON_VP && d->monitor.channels == 6) {
        mixdown_51(d, mixbins, off);
    } else if (d->monitor.point != MCPX_APU_DEBUG_MON_VP) {
        float wl[NUM_MIXBINS], wr[NUM_MIXBINS];
        mixdown_weights(d, wl, wr);
        for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
            /* Bins 2..31 used to be computed and then dropped on the floor.
             * On hardware the GP and EP mix the submixes down; here they are
             * stubs, so thirty of thirty-two bins were discarded every frame
             * with no counter anywhere to say so.
             *
             * Measured on Jet Set Radio Future, one 200 s gameplay run, with
             * a positive control moving beside it:
             *
             *     [APU-BIN] 2D heard=557466 lost=0
             *               3D heard=0      lost=377768
             *               lost by bin: 6,7,8,9,10
             *
             * 557,466 music voice-frames heard and none lost; 377,768 effect
             * voice-frames produced correctly and thrown away. The title's 3D
             * positional voices -- its sound effects -- are routed to bins 6
             * to 10 by the guest's own V0BIN..V3BIN, and music on 2D voices
             * lands in bins 0 and 1, which is why the music was always
             * audible and no effect ever was. Every instrument upstream of
             * this line read healthy.
             *
             * Gating the HRTF submix override was tried first and did not fix
             * it, so the defect is the width of this mixdown and nothing else.
             *
             * Even bins left, odd bins right, which preserves the stereo
             * pairing the guest set up -- bins 6/7 and 8/9 arrive with matched
             * counts. This is not what a real EP does; it is the cheapest
             * mixdown that stops discarding audio. */
            float left, right;
            int mode = mcpx_apu_mixdown_mode();
            if (mode == MIXDOWN_GP) {
                left = 0.0f;
                right = 0.0f;
                for (int b = 0; b < NUM_MIXBINS; ++b) {
                    left  += wl[b] * mixbins[b][i];
                    right += wr[b] * mixbins[b][i];
                }
            } else if (mode == MIXDOWN_ALL) {
                left = 0.0f;
                right = 0.0f;
                for (int b = 0; b < NUM_MIXBINS; ++b) {
                    if (b & 1) right += mixbins[b][i];
                    else       left  += mixbins[b][i];
                }
            } else {
                left = mixbins[0][i];
                right = mixbins[1][i];
            }
            /* Clamp to [-1, 1] range */
            if (left > 1.0f) left = 1.0f;
            if (left < -1.0f) left = -1.0f;
            if (right > 1.0f) right = 1.0f;
            if (right < -1.0f) right = -1.0f;

            /* Convert to 16-bit and write (not accumulate) into frame buffer.
             * Each of the 8 sub-frames writes its own 32-sample slice. */
            d->monitor.frame_buf[off + i][0] = (int16_t)(left * 32767.0f);
            d->monitor.frame_buf[off + i][1] = (int16_t)(right * 32767.0f);
        }
    }

    g_dbg.gp.cycles = 0;
    g_dbg.ep.cycles = 0;
}
