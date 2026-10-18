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
 *   widescreen  = 0 | 1 | W:H                 RECOMP_WIDESCREEN (0: 4:3, 1: 16:9; step 64: the
 *                                             overlay's Aspect ratio writes 16:10 and 21:9 as W:H)
 *   render_size = game | WxH | native         RECOMP_RENDER_SIZE (game: the guest's 480 lines)
 *   pacing      = auto | vsync | limiter | limiter+vsync    RECOMP_PACING
 *   anisotropy  = 1 | 2 | 4 | 8 | 16          RECOMP_ANISOTROPY (1: the game's own filtering)
 *   antialiasing = off | fxaa | smaa | msaa2x | msaa4x | msaa8x    RECOMP_ANTIALIASING (unset: off)
 *                | msaa2x+smaa | msaa4x+smaa | msaa8x+smaa        (step 90: MSAA, then SMAA on the frame)
 *   render_scale = 25-200                     RECOMP_RENDER_SCALE (step 90: percent of the render size the
 *                                             picture is drawn at; unset: 100)
 *   pause_unfocused = 0 | 1                   RECOMP_PAUSE_UNFOCUSED (step 90: the game holds, muted, while
 *                                             its window is in the background or minimised; unset: off)
 *   framerate   = 60 | display | N            RECOMP_FRAMERATE (unset: 60; step 77: N a rate in Hz)
 *   adaptive_framerate = 0 | 1                RECOMP_FRAMERATE_ADAPTIVE (unset: on; step 75. The old
 *                                             name RECOMP_D3D_BRIDGE_INTERP_ADAPT still reads)
 *   render_thread = off | on | auto           RECOMP_RENDER_THREAD (unset: off for now; step 79)
 *   show_fps    = 0 | 1                       RECOMP_SHOW_FPS (unset: off; step 82: the game's updates and
 *                                             the frames shown a second in the picture's corner)
 *   startup_message = 0 | 1                   RECOMP_STARTUP_MESSAGE (unset: on; step 86: "Press F1 to
 *                                             open the settings menu" at the bottom for ~8 s)
 *   hud         = centre | edges              RECOMP_HUD (unset: centre)
 *   hud_margin  = 0-10                        RECOMP_HUD_MARGIN (percent of the width; unset: 0)
 *   draw_distance = 1 | 1.5 | 2 | 3           RECOMP_DRAW_DISTANCE (unset: 1, the game's)
 *   volume      = 0-100                       RECOMP_VOLUME (the game's volume in Windows' mixer)
 *   music_volume   = 0-100                    RECOMP_MUSIC_VOLUME (stream voices: music, ambience, speech)
 *   effects_volume = 0-100                    RECOMP_EFFECTS_VOLUME (buffer voices: the sound effects)
 *   speakers       = stereo | 5.1             RECOMP_SPEAKERS (from the next start)
 *   hrtf           = 0 | 1                    RECOMP_HRTF (unset: on)
 *   pause          = 0 | 1                    RECOMP_OVERLAY_PAUSE (unset: on)
 *   keyboard       = 0 | 1                    RECOMP_KEYBOARD (unset: on)
 *   controller_input = sdl | xinput           RECOMP_CONTROLLER_INPUT (unset: sdl; pads.h)
 *   pad_<xbox input>, key_<xbox input>        RECOMP_PAD_*, RECOMP_KEY_* (controls.h)
 *   extra_<pc input>                          RECOMP_EXTRA_* (controls.h, step 70)
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
 * Step 58: anti-aliasing (notes 18.61). FXAA and SMAA filter the finished
 * frame at the present (aa.c); MSAA draws the back buffer and the
 * screen-sized render targets multisampled and resolves them before
 * anything reads them (d3d_host.c). Applied at once.
 *
 * Step 61: the frame rate (notes 18.64). 60 shows each game frame for the
 * display's whole interval, as before; display draws it again at every
 * refresh in between with the camera interpolated from the frame before
 * (d3d_host.c), on a display at 2, 3 or 4 x 59.94 Hz paced by its vsync.
 * The game still runs 59.94 updates a second. From the next frame.
 *
 * Step 75: the adaptive frame rate. With the frame rate at display, a frame
 * too heavy to draw at every refresh gets fewer frames in between (notes
 * 18.77) while that lasts; adaptive_framerate 0 draws them all whatever the
 * cost, and in the heaviest places the game then runs slower than 59.94
 * updates a second. Applied at once.
 *
 * Step 79: the render thread (notes 18.82). With the frame rate above 60,
 * on draws the frames between game frames (and presents them) on a thread
 * of its own beside the game's; auto is on when the PC has 4 or more
 * processor cores; off keeps everything on the game's thread. At the next
 * frame.
 *
 * Step 65: the HUD position (notes 18.68). In a picture wider than 4:3 the
 * game's HUD counters sit in the middle 4:3; edges moves those on the left
 * to the left edge and those on the right to the right edge (title cards,
 * the pause menu and anything centred stay), hud_margin percent of the
 * width in from it. Applied at once.
 *
 * Step 70: controllers (notes 18.73). sdl reads every pad the PC has
 * through SDL3 (PlayStation, Switch, Steam Controller and Deck, and the
 * rest) together with the Xbox pads through XInput, as one controller on
 * the game's port 0, plugged and unplugged while it runs; xinput is
 * XInput user 0 alone, as before. Applied at once.
 *
 * Step 69: the draw distance (notes 18.72). Each level sets its camera's
 * far plane and its fog; draw_distance multiplies both (and the sky with
 * them), so the world is drawn further out under fog that ends further
 * away. Crates and fruit are still drawn within 45 units and enemies woken
 * within 25, as the game decides. From the next frame.
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
enum { TWOC_AA_OFF, TWOC_AA_FXAA, TWOC_AA_SMAA, TWOC_AA_MSAA2, TWOC_AA_MSAA4, TWOC_AA_MSAA8,
       TWOC_AA_MSAA2_SMAA, TWOC_AA_MSAA4_SMAA, TWOC_AA_MSAA8_SMAA,     /* step 90: MSAA, then SMAA on the frame */
       TWOC_AA_COUNT };
#define TWOC_RENDER_SCALE_MIN 25        /* step 90: render_scale's range, percent */
#define TWOC_RENDER_SCALE_MAX 200
enum { TWOC_RT_OFF, TWOC_RT_ON, TWOC_RT_AUTO, TWOC_RT_COUNT };   /* step 79: the render thread */

typedef struct {
    int fullscreen;
    int widescreen;                     /* on: wide_w:wide_h (16:9 unless RECOMP_WIDESCREEN named another) */
    double wide_w, wide_h;
    char render_size[32];               /* "" (the game's 480 lines), "WxH" or "native" */
    int pacing;                         /* TWOC_PACING_* */
    int anisotropy;                     /* 1 (off), 2, 4, 8, 16 */
    int antialiasing;                   /* TWOC_AA_* */
    int framerate;                      /* step 61: 0 the game's 60, 1 the display's (interpolated); step 77: N Hz */
    int adaptive;                       /* step 75: 1 fewer sub-frames while late (also when unset), 0 always all */
    int render_thread;                  /* step 79: TWOC_RT_* (unset: off) */
    int show_fps;                       /* step 82: 1 the measured rates in a corner, 0 not (also unset) */
    int startup_message;                /* step 86: 1 the F1 line for the first seconds (also unset), 0 not */
    int hud, hud_margin;                /* step 65: 0 the HUD in the middle 4:3, 1 at the edges; 0-10 percent in */
    double draw_distance;               /* step 69: the far plane and fog times this; 1 the game's (also unset) */
    int volume;                         /* 0-100; -1: never set (Windows' own level) */
    int music, effects;                 /* 0-100; -1: never set (100, the game's mix) */
    int speakers;                       /* 0 stereo, 1 5.1; -1: never set (stereo) */
    int hrtf;                           /* 1 on (also when unset), 0 off */
    int pause;                          /* 1: the overlay holds the game (also when unset), 0: it runs */
    int keyboard;                       /* 1: the keyboard plays too (also when unset), 0: not */
    int controller_input;               /* step 70: PADS_SDL (also when unset) or PADS_XINPUT_ONLY */
    int render_scale;                   /* step 90: percent of the render size drawn, 25-200 (100 also unset) */
    int pause_unfocused;                /* step 90: 1 the game holds while the window is in the background */
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
int twoc_framerate_parse(const char *v);    /* step 77: 0 (60, unset, unknown), 1 (display), N Hz */
void twoc_framerate_text(int fr, char *buf, size_t cap);    /* the file's word: 60, display or N */
const char *twoc_aa_name(int aa);       /* the file's word: off, fxaa, smaa, msaa2x, msaa4x, msaa8x */
int twoc_aa_parse(const char *v);       /* TWOC_AA_OFF for anything unknown */
const char *twoc_rt_name(int rt);       /* step 79: off, on, auto */
int twoc_rt_parse(const char *v);       /* TWOC_RT_OFF for anything unknown (sync, a check, is on) */
unsigned twoc_aa_samples(int aa);       /* MSAA's samples a pixel; 1 for the others */
int twoc_aa_post(int aa);               /* step 90: the filter on the finished frame: TWOC_AA_FXAA, _SMAA or _OFF */
int twoc_render_scale_parse(const char *v);     /* step 90: 25-200, 100 unset or unknown */

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
