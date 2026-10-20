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

#ifdef __cplusplus
}
#endif

#endif
