/**
 * Power and chip-speed logging (step 91).
 *
 * A Legion Go crashed twice, once soon after Legion Space's mode was switched
 * from Performance to Balanced and back while the game ran. This logs what
 * the chip and Windows' power settings did: a thread of its own wakes once a
 * second (it sleeps in between and does nothing per frame) and reads
 *
 *   - Windows' power mode, the overlay on the plan (PowerGetEffectiveOverlayScheme:
 *     Best power efficiency, Balanced, Best performance) and the effective
 *     mode Windows reports to apps (PowerRegisterForEffectivePowerModeNotifications);
 *   - the active power plan (PowerGetActiveScheme, its name);
 *   - plugged in or on battery, the battery's level and what it delivers (mW);
 *   - the CPU's speed: the PDH "Processor Information(_Total)" counters
 *     (% Processor Performance times Processor Frequency, the effective MHz
 *     Task Manager shows; % of Maximum Frequency, the P-state's share), with
 *     CallNtPowerInformation(ProcessorInformation)'s MHz beside them;
 *   - every 10 s, a temperature from the "Thermal Zone Information" counters
 *     (ACPI thermal zones), when Windows has any; if it has none, a line says
 *     so once.
 *
 * powrprof.dll and pdh.dll are loaded at run time, so the exe imports
 * nothing new. The game's threads never wait on any of it: the bridge's 10-s
 * interval line (power_log_interval) copies the latest numbers under a lock
 * the sampler holds only to copy them.
 *
 * Lines: "[POWER] ..." at the start, at once whenever the power mode, the
 * effective mode, the plan, plugged in / on battery or the CPU's speed class
 * changes (with the game's frame counter, the game's updates a second and
 * the game and render threads' ms a frame from the last interval), and the
 * 10-s interval's "[D3DB] host power:" line. RECOMP_POWER_LOG=PATH also
 * writes the [POWER] lines and a line every 10 s to PATH, written through to
 * the disk, so they survive the system crashing (the package's launcher sets
 * it: run_twoc.power.log). RECOMP_POWER_LOG=0 turns the sampler off.
 * RECOMP_DEV_POWER_FAKE=N (a test aid) reads the power mode as another one
 * from N s after the sampler starts, for 15 s, so the change lines can be
 * seen without changing the PC's settings.
 */
#ifndef TWOC_POWER_LOG_H
#define TWOC_POWER_LOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned (*frame_counter)(void);    /* the game's frame counter */
    double (*now_s)(void);              /* seconds since the bridge started, as its lines count them */
} power_log_hooks_t;

/* Starts the sampler (once). */
void power_log_start(const power_log_hooks_t *hooks);
/* The 10-s interval's power text (no newline): the source, the battery, the
 * modes, the plan, the CPU's speed over the interval, the temperature. The
 * interval's averages start again. Any thread; never waits on the sampler. */
void power_log_interval(char *buf, size_t cap);
/* The interval's game updates a second, and the game and render threads'
 * ms a game frame (below 0: not measured), for the change lines. */
void power_log_game(double ups);
void power_log_thread_ms(double game_ms, double render_ms);

#ifdef __cplusplus
}
#endif

#endif
