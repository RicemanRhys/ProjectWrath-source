/**
 * Player settings (step 54, notes 18.57): crash_twoc.ini next to the exe.
 *
 * Each setting is one of the RECOMP_* switches the bridge already reads. At
 * the start (twoc_settings_load, the first thing WinMain does) every key of
 * the file whose variable the launcher left unset is put into the
 * environment, so a run_twoc.ps1 switch or a variable set by hand wins over
 * the file, and without the file nothing is set: the run is as before.
 *
 *   fullscreen  = 0 | 1                       RECOMP_FULLSCREEN
 *   widescreen  = 0 | 1 | W:H                 RECOMP_WIDESCREEN
 *   render_size = game | WxH | native         RECOMP_RENDER_SIZE (game: the guest's 480 lines)
 *   pacing      = auto | vsync | limiter | limiter+vsync    RECOMP_PACING
 *   anisotropy  = 1 | 2 | 4 | 8 | 16          RECOMP_ANISOTROPY (1: the game's own filtering)
 *   volume      = 0-100                       RECOMP_VOLUME (the game's volume in Windows' mixer)
 *   music_volume   = 0-100                    RECOMP_MUSIC_VOLUME (stream voices: music, ambience, speech)
 *   effects_volume = 0-100                    RECOMP_EFFECTS_VOLUME (buffer voices: the sound effects)
 *   speakers       = stereo | 5.1             RECOMP_SPEAKERS (from the next start)
 *   hrtf           = 0 | 1                    RECOMP_HRTF (unset: on)
 *   pause          = 0 | 1                    RECOMP_OVERLAY_PAUSE (unset: on)
 *   keyboard       = 0 | 1                    RECOMP_KEYBOARD (unset: on)
 *   pad_<xbox input>, key_<xbox input>        RECOMP_PAD_*, RECOMP_KEY_* (controls.h)
 *
 * The music and effects volumes are gains inside the emulated APU
 * (xboxrecomp's voice processor, notes 18.58), under the game's volume; 100
 * is the game's mix. 5.1 opens the output at six channels when the default
 * device has those speakers (else stereo, as without it) and tells the game
 * it has surround speakers (recomp_manual.c), as the Xbox dashboard did.
 * The HRTF is the voice processor's filter on 3D voices (notes 18.59), which
 * places them left-right as the Xbox does; 0 turns it off (the mix before
 * 18.59: 3D voices the same on both sides).
 *
 * Step 57: pause 0 keeps the game running behind the overlay (it gets an
 * idle pad meanwhile); the controller map and the keyboard are controls.c's.
 *
 * The settings overlay (overlay.cpp) shows the values in effect and writes
 * the file back when the player changes one.
 */
#ifndef TWOC_SETTINGS_H
#define TWOC_SETTINGS_H

#include "controls.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { TWOC_PACING_AUTO, TWOC_PACING_VSYNC, TWOC_PACING_LIMITER, TWOC_PACING_LIMITER_VSYNC, TWOC_PACING_COUNT };

typedef struct {
    int fullscreen;
    int widescreen;                     /* on: wide_w:wide_h (16:9 unless RECOMP_WIDESCREEN named another) */
    double wide_w, wide_h;
    char render_size[32];               /* "" (the game's 480 lines), "WxH" or "native" */
    int pacing;                         /* TWOC_PACING_* */
    int anisotropy;                     /* 1 (off), 2, 4, 8, 16 */
    int volume;                         /* 0-100; -1: never set (Windows' own level) */
    int music, effects;                 /* 0-100; -1: never set (100, the game's mix) */
    int speakers;                       /* 0 stereo, 1 5.1; -1: never set (stereo) */
    int hrtf;                           /* 1 on (also when unset), 0 off */
    int pause;                          /* 1: the overlay holds the game (also when unset), 0: it runs */
    int keyboard;                       /* 1: the keyboard plays too (also when unset), 0: not */
    twoc_controls_t controls;           /* the controller map and the key bindings */
} twoc_settings_t;

/* Read crash_twoc.ini into the RECOMP_* variables the launcher left unset. */
void twoc_settings_load(void);
/* The settings in effect, from the RECOMP_* variables. */
void twoc_settings_current(twoc_settings_t *s);
/* Write `s` to crash_twoc.ini. Returns 0 on success. */
int twoc_settings_save(const twoc_settings_t *s);
const char *twoc_settings_path(void);
const char *twoc_pacing_name(int pacing);
int twoc_pacing_parse(const char *v);   /* TWOC_PACING_AUTO for anything unknown */

/* host_audio.cpp: the process's volume in Windows' mixer (its audio session),
 * 0-100, set on a short-lived thread of its own. */
void twoc_audio_set_volume(int percent);
/* Mute the session while the overlay holds the game (a marker file records it). */
void twoc_audio_mute(int on);
/* At the start: lift a mute the overlay left when the process ended. */
void twoc_audio_startup(void);
/* The music (stream != 0) or effects volume, 0-100, in the APU's voice
 * processor; -1 (never set) is 100. */
void twoc_audio_set_kind(int stream, int percent);
/* The APU's HRTF on 3D voices, on or off (it is on unless this says off). */
void twoc_audio_set_hrtf(int on);

#ifdef __cplusplus
}
#endif

#endif
