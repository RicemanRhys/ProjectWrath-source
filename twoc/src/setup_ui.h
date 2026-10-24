/**
 * The first-run setup screen (step 107, notes 18.110): a window of its own
 * (D3D11 and Dear ImGui, the F1 menu's look), shown before the game boots
 * when game_files is missing or fails the check, after a recovery, or when
 * the F1 menu asked for it. The game's own window and device don't exist yet
 * at that point (the bridge makes them when the game's code creates its D3D
 * device), so the screen can't borrow them; once the files are ready the
 * window closes and the game starts in the same process. setup.h does the
 * work; this only shows it and takes the player's choices (mouse, keyboard,
 * any controller: D-pad or stick, A, B).
 */
#ifndef TWOC_SETUP_UI_H
#define TWOC_SETUP_UI_H

#ifdef __cplusplus
extern "C" {
#endif

enum { SETUP_UI_FIRST, SETUP_UI_BROKEN, SETUP_UI_RECOVERY, SETUP_UI_REPLACE };
enum { SETUP_UI_READY, SETUP_UI_START_ANYWAY, SETUP_UI_QUIT, SETUP_UI_NO_WINDOW };

/* `mode` SETUP_UI_*; `why` the check's words (SETUP_UI_BROKEN); `files_ok` the current game_files passed the
 * check (the screen offers to start with them). Returns SETUP_UI_READY (set up or checked: start), _START_ANYWAY
 * (start with the files as they are), _QUIT, or _NO_WINDOW (couldn't be shown). */
int twoc_setup_ui_run(int mode, const char *why, int files_ok);

#ifdef __cplusplus
}
#endif

#endif
