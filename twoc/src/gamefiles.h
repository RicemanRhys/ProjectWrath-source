/**
 * The game files at the start (step 83, notes 18.86): default.xbe and
 * Crashdat in game_files, checked against the disc's (gamefiles.c's table).
 */
#ifndef TWOC_GAMEFILES_H
#define TWOC_GAMEFILES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Before the XBE loads. Says what is wrong (the log and a message box) and
 * returns 0 when the game shouldn't start; 1 when the files are the disc's
 * or the player started anyway. */
int twoc_game_files_check(void);
/* Step 93: what the check found, in a line ("not checked this run" before it). */
const char *twoc_game_files_result(void);

#ifdef __cplusplus
}
#endif

#endif
