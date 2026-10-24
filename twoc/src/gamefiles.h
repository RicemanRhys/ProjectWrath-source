/**
 * The game files at the start (step 83, notes 18.86; step 107, notes 18.110):
 * default.xbe and Crashdat in game_files, checked against the supported
 * disc's manifest (setup.h) by default.xbe's SHA-256 and every file's
 * presence and size.
 */
#ifndef TWOC_GAMEFILES_H
#define TWOC_GAMEFILES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    GF_OK,                              /* as on the disc (extra files in Crashdat allowed, logged) */
    GF_NONE,                            /* no game_files folder: a first run */
    GF_MISSING,                         /* game_files without default.xbe or Crashdat */
    GF_XBE,                             /* another default.xbe */
    GF_FILES                            /* files missing or with another size */
};

/* Before the XBE loads: the [FILES] line, no message box. Returns GF_*; `why` says what is wrong in plain words
 * (for the setup screen). */
int twoc_game_files_check_quiet(char *why, size_t cap);
/* The same with step 83's message boxes, for when the setup screen can't be shown: returns 0 when the game
 * shouldn't start; 1 when the files are the disc's or the player started anyway. */
int twoc_game_files_check(void);
/* Step 93: what the check found, in a line ("not checked this run" before it). */
const char *twoc_game_files_result(void);
/* Step 107: the setup screen's outcome, added to the result line ("set up from ...", "started anyway"). */
void twoc_game_files_note(const char *what);

#ifdef __cplusplus
}
#endif

#endif
