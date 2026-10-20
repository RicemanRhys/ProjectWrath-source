/**
 * Quitting and starting again from the F1 menu (step 97).
 *
 * Both go the same way, on a thread of their own (twoc_end_begin), after
 * d3d_host.c has written crash_twoc.ini if the player changed a setting:
 *
 *   1. save backups: nothing new is started, and a copy (or the game's own
 *      save write) under way is let finish, up to 10 s (saves_quiesce; step
 *      83: never during one);
 *   2. audio: the APU's voices to silence, XAudio2's queue played out, a
 *      mute the menu left lifted (twoc_audio_close);
 *   3. the window hidden, the log's last lines written ([QUIT] / [RESTART]),
 *      and the process ended as a window close ends it (TerminateProcess).
 *
 * Quit game ends with code 0. Restart now:
 *
 *   - started by a launcher (TWOC_LAUNCHER set: the package's Play Crash.bat
 *     and every launcher that calls it, run_twoc.ps1, the DXVK test's), the
 *     exit code is 75 (TWOC_EXIT_RESTART) and the launcher starts the exe
 *     again, so its logs, its console window and run_twoc.ps1's -Seconds
 *     watchdog stay in order;
 *   - started without one, the exe starts itself again: the same command
 *     line, working folder and environment (as they were before
 *     crash_twoc.ini filled in its variables), made before anything is
 *     closed (so a failure leaves the game running with the menu's
 *     message); the new copy waits for the old one's process to be gone
 *     (TWOC_RESTART_PARENT) before it does anything, so two never run at
 *     once.
 *
 * Never a loop: a fourth restart within a minute is refused with a reason
 * (the times are kept in %TEMP%\crash_twoc_restarts.txt), and the launchers
 * stop after 10 restarts in one window.
 */
#ifndef TWOC_RESTART_H
#define TWOC_RESTART_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TWOC_EXIT_RESTART 75            /* the launcher's cue to start the game again */
#define TWOC_RESTARTS_A_MINUTE 3

/* WinMain's first call, before the settings: the command line, working folder and environment kept for a
 * restart; a copy started by the one before waits here for it to be gone. */
void twoc_restart_init(void);
/* The launcher's name (TWOC_LAUNCHER), or NULL when the exe was started without one. */
const char *twoc_restart_launcher(void);
/* Starts the clean end: `restart` 0 Quit game (code 0), 1 Restart now; `why` for the log. Returns 0 when it
 * has begun (the process ends shortly), -1 when it can't (a restart refused or not possible: `err` says why
 * in the menu's words, and the game goes on). */
int twoc_end_begin(int restart, const char *why, HWND hwnd, char *err, size_t cap);
/* 1 once the clean end has begun. */
int twoc_ending(void);

#ifdef __cplusplus
}
#endif

#endif
