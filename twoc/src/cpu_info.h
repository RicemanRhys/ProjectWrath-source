/**
 * The processor (step 97): what it is, and the process's power throttling.
 *
 * Once, at the start (twoc_cpu_start, WinMain after the settings):
 *
 *   - the CPU's name (CPUID's brand string), its cores and threads
 *     (GetLogicalProcessorInformationEx), and whether it is hybrid:
 *     GetSystemCpuSetInformation gives each logical processor an
 *     EfficiencyClass, and more than one class means performance and
 *     efficiency cores (Intel's 12th generation on, as the Nitro 5's
 *     i5-12500H: 4 P-cores with 2 threads each and 8 E-cores);
 *   - the process's power throttling. Windows may run a process it thinks
 *     is in the background at "EcoQoS", lower clocks and, on a hybrid CPU,
 *     the efficiency cores. The game opts out of that execution-speed
 *     throttling (SetProcessInformation, ProcessPowerThrottling: the
 *     control bit set, the state bit clear), so its threads are scheduled
 *     as the foreground game they are. It changes no output, only where and
 *     how fast Windows runs the threads. RECOMP_CPU_THROTTLING=windows
 *     leaves Windows' own rules (a test aid).
 *
 * [CPU] lines in the log, the About tab's two lines, and Collect logs'
 * system summary (which reads the same Windows calls itself).
 */
#ifndef TWOC_CPU_INFO_H
#define TWOC_CPU_INFO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void twoc_cpu_start(void);
/* "Intel(R) Core(TM) i5-12500H; 12 cores, 16 threads: 4 performance cores (8 threads), 8 efficiency cores" */
const char *twoc_cpu_text(void);
/* "not throttled (the game opted out of Windows' power throttling)" or what it is */
const char *twoc_cpu_power_text(void);
/* 1 when the CPU has more than one efficiency class */
int twoc_cpu_hybrid(void);

/*
 * Step 99: where the game and render threads run. Each calls
 * twoc_cpu_where once a frame (GetCurrentProcessorNumberEx: the logical
 * processor it is on at that moment, counted); the bridge's 10-s interval
 * line takes twoc_cpu_where_text ("the game thread on performance cores
 * 98.0%, efficiency cores 2.0% of 600 samples, on 3 logical processors (most
 * on CPU 4 61%, then CPU 6 30%); the render thread ..."; with one kind of
 * core it says so and gives the samples and processors alone), and the
 * counts start again. Logging only: no thread is pinned or moved.
 * RECOMP_DEV_CPU_CLASSES=N (a test aid) reads the first N logical
 * processors as performance cores and the rest as efficiency cores, so the
 * hybrid lines can be seen on a CPU with one kind of core.
 */
#define TWOC_WHERE_GAME 0
#define TWOC_WHERE_RENDER 1
void twoc_cpu_where(int who);
int twoc_cpu_where_text(char *buf, size_t cap);
/* The kinds of core (1 one kind; 2 performance and efficiency; 3 with a middle kind), 0 not known. */
int twoc_cpu_ranks(void);
/* A logical processor's kind: 0 the most performant ... twoc_cpu_ranks() - 1; -1 not known. */
int twoc_cpu_rank_of(unsigned group, unsigned number);

#ifdef __cplusplus
}
#endif

#endif
