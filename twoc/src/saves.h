/**
 * Save backups (step 83, notes 18.86).
 *
 * The game keeps every slot in one file, game_files\UDATA\56550003\
 * 4D5C926D6357\crash.sav (with SaveMeta.xbx beside it). Copies of both go to
 * game_files\UDATA_backups\<yyyy-mm-dd_hh-mm-ss>\, outside UDATA so the
 * game's own scans of its save folders never see them:
 *
 *   - at each start, before the game runs (saves_startup);
 *   - 2 s after the game closes a save file it opened for writing, on a
 *     worker thread. The game's opens and closes in its save folders are
 *     seen at its kernel calls (NtCreateFile, NtOpenFile, NtClose, tapped in
 *     recomp_manual.c), so a copy is never taken while the game has a save
 *     file open, and one the game opened meanwhile is thrown away and taken
 *     again later;
 *   - by hand, from the overlay's Saves tab.
 *
 * A copy is skipped when both files equal the newest backup's. Kept: the
 * newest 10, and the newest of each day for 14 days; older ones are deleted
 * (only folders named as above, and only the files a backup holds). A
 * restore asked for while the game runs is done at the next start, after
 * that start's backup of the save it replaces; the restore's target is never
 * deleted meanwhile. RECOMP_SAVE_BACKUPS=0 turns all of it off.
 */
#ifndef TWOC_SAVES_H
#define TWOC_SAVES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[32];                      /* the folder: yyyy-mm-dd_hh-mm-ss[_N] */
    char when[24];                      /* "2026-10-17 14:03:22" */
    unsigned size;                      /* crash.sav's bytes (0: the backup has none) */
    char why[48];                       /* "at start", "after a save", "by hand", ... */
} saves_entry_t;

typedef struct {
    int on;                             /* backups are taken */
    char dir[512];                      /* the backups folder (UTF-8) */
    char status[320];                   /* the last action's outcome, "" none */
    char pending[32];                   /* a restore at the next start: its backup, "" none */
    int busy;                           /* an action is under way */
} saves_state_t;

/* WinMain, before the game runs: a restore asked for in the last run, this
 * start's backup, the worker thread. */
void saves_startup(void);

/* The guest's file calls in its save folders (recomp_manual.c).
 * open_begin before the kernel call: 0 when `path` isn't in a save folder,
 * else a token for open_end, which follows the call. close_begin before
 * NtClose (when saves_guest_tracking()): 0 for a handle not tracked;
 * close_end after it. */
int saves_guest_open_begin(const char *path, unsigned len, unsigned access, unsigned share, unsigned disposition,
                           unsigned options);
void saves_guest_open_end(int token, int ok, uint32_t handle);
int saves_guest_tracking(void);
int saves_guest_close_begin(uint32_t handle);
void saves_guest_close_end(int token);

/* The overlay's Saves tab. The list is newest first. */
int saves_list(saves_entry_t *out, int max);
void saves_state(saves_state_t *st);
void saves_refresh(void);
void saves_backup_now(void);
void saves_restore_at_next_start(const char *name);
void saves_cancel_restore(void);
void saves_open_folder(void);

#ifdef __cplusplus
}
#endif

#endif
