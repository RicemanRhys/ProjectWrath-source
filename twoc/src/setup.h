/**
 * The first-run setup's core (step 107, notes 18.110): the game files made
 * from the player's own disc image or extracted folder, checked against the
 * supported disc's manifest (twoc/data/disc_usa.tsv: every file's path, size
 * and SHA-256; no game data), and put in place safely. setup_ui.cpp is the
 * screen; this has no window.
 *
 * The data folder (twoc_data_dir) holds game_files and everything the setup
 * makes beside it:
 *   game_files.new                      the copy being made (never used as is)
 *   game_files.old                      the files a replace took out, until the
 *                                       next start that passes the check
 *   game_files.setup-in-progress.txt    the journal: source, start time, phase;
 *                                       written first, removed only at the end
 *   game_files.setup-request.txt        the F1 menu's "Choose another disc
 *                                       image...": open the setup at the next start
 *
 * Safety rules (each tested in step 107):
 *   - extraction goes into game_files.new, every file's size and SHA-256 is
 *     checked while copying and again from the disk afterwards (each file
 *     flushed with FlushFileBuffers); only then the swap: what game_files
 *     holds besides the disc's files (saves in UDATA, their backups, TDATA,
 *     anything else) is moved into the new folder, game_files becomes
 *     game_files.old and game_files.new becomes game_files;
 *   - an existing game_files is replaced only after the player confirmed it
 *     (the screen asks; this only takes `replace`);
 *   - an error, a Cancel or a close deletes only what this run made (the
 *     manifest's files and the folders they left empty in game_files.new,
 *     and the journal); nothing that isn't a disc file is ever deleted;
 *   - at every start, before anything else (twoc_setup_recover): a journal or
 *     a game_files.new left by an interrupted run is cleaned up, game_files.old
 *     is put back if game_files is missing, and the setup screen says so;
 *   - a file held by an antivirus or the indexer is retried (10 times over
 *     about 5 s) before it counts as an error.
 */
#ifndef TWOC_SETUP_H
#define TWOC_SETUP_H

#include <stdint.h>
#include <stddef.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SETUP_PATH_MAX 1024             /* wchar_ts of a path the setup keeps */

/* ── The data folder (Part 1.4 and Part 3) ─────────────────────────────── */

/* Chooses the data folder and makes it the working folder, when the exe was started by itself (no launcher, no
 * RECOMP_* variable): the exe's folder when it can be written to, else %LOCALAPPDATA%\ProjectWrath (step 109;
 * %LOCALAPPDATA%\CrashTWOC for the developer's crash_twoc.exe, names.h), whose first use copies an old
 * %LOCALAPPDATA%\CrashTWOC's contents in, never changing that one. Started by a launcher: the working folder as it
 * was. Called once, first thing. Returns 1 when started by itself. */
int twoc_data_dir_init(void);
const wchar_t *twoc_data_dir(void);     /* absolute, no trailing backslash */
/* 1 when %LOCALAPPDATA%\ProjectWrath is used because the exe's folder can't be written to. */
int twoc_data_dir_fallback(void);
/* Step 109: the [DATA] line of the old data folder's copy ("" none), for the log once it is open. */
const char *twoc_data_dir_note(void);
/* The data folder as UTF-8, for the log and the screen. */
const char *twoc_data_dir_utf8(void);
/* A file kept beside the exe (ProjectWrath.ini, ProjectWrath.muted: names.h), or in the data folder when the exe's
 * folder can't be written to. `name` in UTF-8; `out` in the narrow functions' code page. Returns 0 or -1. */
int twoc_settings_file(const char *name, char *out, size_t cap);

/* ── Recovery (at every start, before anything else) ───────────────────── */

typedef struct {
    int happened;                       /* a leftover was found and dealt with */
    int old_put_back;                   /* game_files.old became game_files again */
    char text[1200];                    /* what happened, in plain words */
    wchar_t source[SETUP_PATH_MAX];     /* the source of the setup that didn't finish ("" unknown): Retry */
} setup_recovery_t;

void twoc_setup_recover(void);
const setup_recovery_t *twoc_setup_recovery(void);
/* After the check passed: game_files.old's disc files deleted on a low-priority thread (a folder with anything
 * else in it is kept and logged). */
void twoc_setup_old_cleanup(void);
/* The F1 menu's "Choose another disc image...": the request file, then a restart. Returns 0 or -1. */
int twoc_setup_request_write(void);
/* 1 if the request file was there (it is deleted). */
int twoc_setup_request_take(void);

/* ── The supported disc's manifest ─────────────────────────────────────── */

unsigned twoc_disc_files(void);
uint64_t twoc_disc_bytes(void);
/* index of a disc path ('/' or '\\', any case) or -1; size and SHA-256 (hex) of entry i */
int twoc_disc_find(const char *path);
void twoc_disc_entry(unsigned i, const char **path, uint32_t *size, const char **sha256);
/* "Crash Bandicoot: The Wrath of Cortex (Xbox, USA)" */
const char *twoc_disc_name(void);
/* An XBE's certificate in words: "Crash Bandicoot: tWoC (title id 56550003, region North America, version 1)". */
int twoc_xbe_describe(const unsigned char *xbe, size_t n, char *title, size_t tcap, unsigned *title_id,
                      unsigned *region, unsigned *version);
void twoc_region_name(unsigned region, char *buf, size_t cap);

/* ── Jobs (a worker thread; the screen watches) ────────────────────────── */

enum {
    SETUP_IDLE, SETUP_IDENTIFYING, SETUP_IDENTIFIED, SETUP_COPYING, SETUP_VERIFYING, SETUP_SWAPPING, SETUP_DONE,
    SETUP_FAILED, SETUP_CANCELLED
};
enum { SETUP_SRC_NONE, SETUP_SRC_IMAGE, SETUP_SRC_FOLDER };

typedef struct {
    int state;                          /* SETUP_* */
    int kind;                           /* SETUP_SRC_* */
    int in_place;                       /* the folder chosen is game_files itself: checked, not copied */
    int replace;                        /* game_files exists and will be replaced (asked first) */
    int verify_only;                    /* the F1 menu's Verify game files */
    wchar_t source[SETUP_PATH_MAX];     /* what was chosen */
    char source_utf8[SETUP_PATH_MAX * 3];
    char layout[160];                   /* "trimmed XISO (...)", "an extracted folder (default.xbe one folder down)" */
    char found[320];                    /* the disc found, in words */
    int found_ok;                       /* ... the supported one */
    uint64_t bytes_done, bytes_total;
    unsigned files_done, files_total;
    double secs;                        /* since the copy (or the check) began */
    double mb_s;                        /* recent speed */
    double eta_s;                       /* time left (-1 unknown) */
    char current[400];                  /* the file being read or written */
    char why[1400];                     /* a failure: what happened */
    char hint[700];                     /* ... what to try */
    char note[700];                     /* done / cancelled: what was cleaned up, what was kept */
    uint64_t free_bytes, need_bytes;    /* the data folder's disk */
} setup_status_t;

/* Reads what `path` is (an image or a folder), checks default.xbe and the file list and sizes; ends in
 * SETUP_IDENTIFIED (found_ok says whether it is the supported disc) or SETUP_FAILED. */
int twoc_setup_identify(const wchar_t *path);
/* After SETUP_IDENTIFIED with found_ok: copies into game_files.new, verifies, swaps; `replace` must be 1 when
 * game_files exists (the player said yes). Ends in SETUP_DONE, SETUP_FAILED or SETUP_CANCELLED. */
int twoc_setup_install(int replace);
/* The F1 menu's check: every file of game_files against the manifest (size and SHA-256), nothing changed. */
int twoc_setup_verify(void);
void twoc_setup_cancel(void);
void twoc_setup_status(setup_status_t *out);
int twoc_setup_busy(void);
/* Waits for the worker (a close while it runs: cancelled, then cleaned up). */
void twoc_setup_wait(void);
/* Back to SETUP_IDLE (a finished or failed job forgotten). */
void twoc_setup_reset(void);

/* game_files exists (a folder) / has default.xbe. */
int twoc_setup_game_files_exist(void);

#ifdef __cplusplus
}
#endif

#endif
