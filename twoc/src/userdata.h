/**
 * Saves outside the package (step 110, notes 18.113).
 *
 * Before step 110 the saves lived inside the game's folder (game_files\UDATA, TDATA and UDATA_backups), so deleting
 * that folder, extracting a new package beside it or moving the game could lose them. The packaged exe
 * (ProjectWrath.exe) now keeps them in a per-user data folder:
 *
 *   %LOCALAPPDATA%\ProjectWrath\
 *     saves\UDATA\56550003\...     the game's saves (its U: drive), as game_files\UDATA was
 *     saves\TDATA\56550003\        its title data (T:)
 *     saves\UDATA_backups\...      the save's backups (saves.h)
 *     ProjectWrath.ini             the settings (setup.h's twoc_settings_file)
 *     cache\                       the toolkit's emulated hard disk: partition images and the game's Z: cache,
 *                                  before this in %LOCALAPPDATA%\xboxrecomp (safe to delete while the game is closed)
 *
 * LOCALAPPDATA, not Documents or Roaming: OneDrive's folder backup moves Documents into the cloud and roaming
 * profiles copy Roaming between PCs, and both can keep a half-written, older or duplicated ("crash-PC.sav") copy of
 * a file the game is writing. Local AppData is never synced.
 *
 * Which layout a run uses:
 *   - the package's layout (everything in game_files, exactly as before) when portable.txt sits beside the exe, when
 *     RECOMP_SAVES_IN_PACKAGE=1, or for the developer's crash_twoc.exe (names.h) unless RECOMP_SAVES_OUTSIDE=1;
 *   - otherwise the data folder: RECOMP_DATA_DIR when set (tests), else %LOCALAPPDATA%\ProjectWrath (CrashTWOC for
 *     crash_twoc.exe with RECOMP_SAVES_OUTSIDE=1).
 *
 * The first start with the data folder copies the package's saves in when the data folder has no save and no backup
 * yet (every file checked by SHA-256, the old copy never moved or deleted); with saves in both, the data folder's are
 * used and nothing is merged. A data folder that can't be used (read-only, full, a path too long) sends this run back
 * to the package's layout, and says why: a save is never lost or hidden.
 *
 * The game opens its saves through \Device\Harddisk0\partition1\UDATA (and TDATA), which the toolkit maps into
 * game_files. recomp_manual.c's taps of NtCreateFile and NtOpenFile (step 83's) hand those opens a path in the data
 * folder instead (twoc_userdata_redirect); nothing else of the game's goes there. In both layouts the game's writes
 * of a save file go to a copy that replaces the save whole when the game closes it.
 */
#ifndef TWOC_USERDATA_H
#define TWOC_USERDATA_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* After twoc_data_dir_init, the logs and the setup's recovery, before the settings are read: the layout chosen, the
 * folders made, a migration done. Returns 1 when the saves are outside the package this run. */
int twoc_userdata_init(void);
/* 1: this run keeps the saves (and the ini, and the toolkit's disk) in the data folder. */
int twoc_saves_outside(void);
/* The folder that holds UDATA, TDATA and UDATA_backups: <data folder>\saves, or <game folder>\game_files. Absolute,
 * no trailing backslash. */
const wchar_t *twoc_saves_dir(void);
const char *twoc_saves_dir_utf8(void);
/* The data folder itself (%LOCALAPPDATA%\ProjectWrath), "" in the package's layout. */
const wchar_t *twoc_user_dir(void);
const char *twoc_user_dir_utf8(void);
/* The toolkit's save folder for xbox_path_init (UTF-8), NULL for its own default (%LOCALAPPDATA%\xboxrecomp). */
const char *twoc_xbox_dir_utf8(void);
/* A message for the player this run ("" none): the saves copied, found in two places, or the data folder unusable.
 * Shown by the startup message and the Saves and About tabs. */
const char *twoc_userdata_message(void);
/* The Saves tab's line on where the saves are and why ("" none). */
const char *twoc_userdata_note(void);

/* The guest's open of `path` (`len` bytes, not NUL-terminated; `root` the OBJECT_ATTRIBUTES' RootDirectory; its
 * access mask, create disposition and options): when it is in the game's save folders (after the title's U: and T:
 * links), `out` gets the path to open instead, in the code page the toolkit reads it with: the data folder's
 * (\\?\<data folder>\saves\UDATA\...) when the saves are outside, and, in either layout, for a save file opened to
 * write, a copy of it in <saves>\.saving\ (*wtok >= 0: the game's write is put in place at its close, so a process
 * killed during a save leaves the old save whole); `eff` gets the path as the toolkit would have logged it. Returns 1
 * then, 0 to leave the open alone, -1 to refuse it (a path too long for the toolkit). */
int twoc_userdata_redirect(const char *path, unsigned len, uint32_t root, uint32_t access, uint32_t disposition,
                           uint32_t options, char *out, size_t cap, char *eff, size_t ecap, int *wtok);
/* After the open that *wtok came from: its result and the handle. */
void twoc_userdata_write_opened(int tok, int ok, uint32_t handle);
/* Before the game's NtClose of `handle`: the write it finishes (-1: none; cheap when no write is open). */
int twoc_userdata_closing(uint32_t handle);
/* After that close: the written copy flushed and renamed over the save. */
void twoc_userdata_closed(int tok);

#ifdef __cplusplus
}
#endif

#endif
