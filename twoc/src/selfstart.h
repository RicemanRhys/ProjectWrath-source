/**
 * Started by itself (step 107, notes 18.110): crash_twoc.exe double-clicked,
 * or started from a shortcut or another folder, with no launcher and no
 * RECOMP_* variable (test aids, RECOMP_DEV_*, aside). setup.h has already
 * made the data folder the working folder. This does what Play Crash.bat
 * does around the exe:
 *   - the switches it sets (the host renderer, the APU, the vblank, the power
 *     log), each only when unset;
 *   - the logs: the last run's run_twoc.*.log and xbox_kernel.log moved into
 *     logs\ (named by when they were last written), then stdout and stderr
 *     into run_twoc.out.log and run_twoc.err.log; no console window, unless
 *     --console is on the command line (the console it was started from, or a
 *     new one; the output goes there instead of the files); started from a
 *     console without it, one line there says where the log is;
 * and, started by itself or by a package's Play Crash.bat, the first run's
 * crash_twoc.ini: the settings that suit most PCs (twoc_first_ini).
 */
#ifndef TWOC_SELFSTART_H
#define TWOC_SELFSTART_H

#ifdef __cplusplus
extern "C" {
#endif

/* After twoc_data_dir_init said the exe was started by itself; before anything is logged. */
void twoc_self_start(void);
/* 1 once twoc_self_start ran. */
int twoc_self_started(void);
/* Writes crash_twoc.ini when there is none (started by itself or by "Play Crash*.bat"; never over a file).
 * Before twoc_settings_load. */
void twoc_first_ini(void);

#ifdef __cplusplus
}
#endif

#endif
