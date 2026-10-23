/**
 * Player settings (step 54, notes 18.57): crash_twoc.ini next to the exe.
 *
 * Each setting is one of the RECOMP_* switches the bridge already reads. At
 * the start (twoc_settings_load, the first thing WinMain does) every key of
 * the file whose variable the launcher left unset is put into the
 * environment, so a run_twoc.ps1 switch or a variable set by hand wins over
 * the file, and without the file nothing is set: the run is as before.
 *
 *   fullscreen  = 0 | 1                      RECOMP_FULLSCREEN
 *   widescreen  = 0 | 1 | W:H                 RECOMP_WIDESCREEN (0: 4:3, 1: 16:9; step 64: the
 *                                             overlay's Aspect ratio writes 16:10 and 21:9 as W:H)
 *   render_size = game | WxH | native         RECOMP_RENDER_SIZE (game: the guest's 480 lines)
 *   pacing      = auto | vsync | limiter | limiter+vsync    RECOMP_PACING
 *   anisotropy  = 1 | 2 | 4 | 8 | 16          RECOMP_ANISOTROPY (1: the game's own filtering)
 *   antialiasing = off | fxaa | smaa | msaa2x | msaa4x | msaa8x    RECOMP_ANTIALIASING (unset: off)
 *                | msaa2x+smaa | msaa4x+smaa | msaa8x+smaa        (step 90: MSAA, then SMAA on the frame)
 *   render_scale = 25-200                     RECOMP_RENDER_SCALE (step 90: percent of the render size the
 *                                             picture is drawn at; unset: 100)
 *   ao          = off | ssao | gtao           RECOMP_AO (step 92: ambient occlusion; unset: off)
 *   ao_strength = 0-100                       RECOMP_AO_STRENGTH (step 92: percent of it applied; unset: 100)
 *   ao_half     = 0 | 1                       RECOMP_AO_HALF (step 92: at half the resolution; unset: off)
 *   pause_unfocused = 0 | 1                   RECOMP_PAUSE_UNFOCUSED (step 90: the game holds, muted, while
 *                                             its window is in the background or minimised; unset: off)
 *   fast_title  = 0 | 1                       RECOMP_DEV_FAST_TITLE (step 90: the title animation sped up,
 *                                             -FastTitle's; from the next start; unset: off)
 *   framerate   = 60 | display | N            RECOMP_FRAMERATE (unset: 60; step 77: N a rate in Hz)
 *   adaptive_framerate = 0 | 1                RECOMP_FRAMERATE_ADAPTIVE (unset: on; step 75. The old
 *                                             name RECOMP_D3D_BRIDGE_INTERP_ADAPT still reads)
 *   render_thread = off | on | auto           RECOMP_RENDER_THREAD (unset: auto since step 99; step 79)
 *   show_fps    = 0 | 1 | 2                   RECOMP_SHOW_FPS (unset: off; step 82: the game's updates and
 *                                             the frames shown a second in the picture's corner; step 93:
 *                                             2 Detailed, also the threads' ms, the adaptive step, the CPU's
 *                                             speed, the battery, the power mode, the temperature)
 *   startup_message = 0 | 1                   RECOMP_STARTUP_MESSAGE (unset: on; step 86: "Press F1 to
 *                                             open the settings menu" at the bottom for ~8 s)
 *   hud         = centre | edges              RECOMP_HUD (unset: centre)
 *   fur         = 1 | crash | 0               RECOMP_FUR (step 101: the fur shells, 1 on, 0 off for everyone;
 *                                             step 102: crash, off for Crash only; unset: on)
 *   hub_seethrough = 0 | 1                    RECOMP_HUB_SEETHROUGH (step 102: the hub pads' crystals and gems
 *                                             see-through, as a level draws one collected; unset: off)
 *   volume      = 0-100                       RECOMP_VOLUME (the game's volume in Windows' mixer)
 *   music_volume   = 0-100                    RECOMP_MUSIC_VOLUME (stream voices: music, ambience, speech)
 *   effects_volume = 0-100                    RECOMP_EFFECTS_VOLUME (buffer voices: the sound effects)
 *   speakers       = stereo | 5.1             RECOMP_SPEAKERS (from the next start)
 *   gpu            = auto | <adapter name>    RECOMP_GPU (step 97: the graphics card, gpu.h; matched by
 *                                             name; unset or not on this PC: auto; from the next start)
 *   mute_droid_drone = 0 | 1                  RECOMP_MUTE_DROID_DRONE (step 102: Droid Void's background drone,
 *                                             DRVD_BG, silent from the level's next start; unset: off)
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
 * the pause menu and anything centred stay). Applied at once. (Step 101:
 * hud_margin, which pulled them in, is gone; an old file's line is skipped.)
 *
 * Step 70: controllers (notes 18.73). sdl reads every pad the PC has
 * through SDL3 (PlayStation, Switch, Steam Controller and Deck, and the
 * rest) together with the Xbox pads through XInput, as one controller on
 * the game's port 0, plugged and unplugged while it runs; xinput is
 * XInput user 0 alone, as before. Applied at once.
 *
 * Step 91: step 69's draw_distance is gone; a file that still has it loads
 * as if it didn't, without a message. Step 93: so is step 91's preset (low,
 * standard, high, custom), the same way; every setting it set is still its
 * own key, with the same default.
 *
 * Step 90 (notes 18.93): msaa*+smaa draws with MSAA and then filters the
 * finished frame with SMAA, which also smooths the alpha-tested edges MSAA
 * leaves (fences, foliage). render_scale draws the picture at that share of
 * the render size and shows it at the render size (under 100 lighter, over
 * 100 supersampled), within the graphics card's largest texture; applied at
 * once. pause_unfocused holds the game, muted, as the overlay's hold does,
 * while its window is in the background or minimised (not before the window
 * has had the focus once, nor while it is being switched to fullscreen or
 * the display recovers from a stall). fast_title is -FastTitle: the title
 * animation runs ~128 times its speed, so the main menu opens ~22 s sooner;
 * from the next start, the launcher's switch winning as for every key.
 *
 * Step 92: ambient occlusion (notes 18.95). ssao or gtao darkens corners,
 * creases and the ground under things, worked out from the depth of the
 * world's opaque draws (d3d_host.c, ao.c) and multiplied into the picture
 * before the game's blended draws, particles and HUD, less in fog;
 * ao_strength scales it. Applied at once.
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
enum { TWOC_AO_OFF, TWOC_AO_SSAO, TWOC_AO_GTAO, TWOC_AO_COUNT };   /* step 92: ambient occlusion */
#define TWOC_AO_STRENGTH_DEFAULT 100    /* step 92: ao_strength, percent */

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
    int render_thread;                  /* step 79: TWOC_RT_* (unset: auto, step 99) */
    int show_fps;                       /* step 82: 1 the measured rates in a corner, 0 not (also unset); 2 more */
    int startup_message;                /* step 86: 1 the F1 line for the first seconds (also unset), 0 not */
    int hud;                            /* step 65: 0 the HUD in the middle 4:3, 1 at the edges */
    int fur;                            /* TWOC_FUR_*: step 101 on (also unset) / off; step 102: off for Crash only */
    int mute_drone;                     /* step 102: 1 Droid Void's background drone muted, 0 not (also unset) */
    int hub_seethrough;                 /* step 102: 1 the hub pads' crystals and gems see-through, 0 solid (also unset) */
    int volume;                         /* 0-100; -1: never set (Windows' own level) */
    int music, effects;                 /* 0-100; -1: never set (100, the game's mix) */
    int speakers;                       /* 0 stereo, 1 5.1; -1: never set (stereo) */
    int hrtf;                           /* 1 on (also when unset), 0 off */
    int pause;                          /* 1: the overlay holds the game (also when unset), 0: it runs */
    int keyboard;                       /* 1: the keyboard plays too (also when unset), 0: not */
    int controller_input;               /* step 70: PADS_SDL (also when unset) or PADS_XINPUT_ONLY */
    int render_scale;                   /* step 90: percent of the render size drawn, 25-200 (100 also unset) */
    int pause_unfocused;                /* step 90: 1 the game holds while the window is in the background */
    int fast_title;                     /* step 90: 1 the title animation sped up (from the next start) */
    int ao;                             /* step 92: TWOC_AO_* (off also unset) */
    int ao_strength;                    /* step 92: percent, 0-100 (TWOC_AO_STRENGTH_DEFAULT also unset) */
    int ao_half;                        /* step 92: 1 worked out at half the resolution, upsampled (unset: 0) */
    char gpu[128];                      /* step 97: "" Automatic (also unset), else a graphics card's name */
    twoc_controls_t controls;           /* the controller map and the key bindings */
} twoc_settings_t;

/* Read crash_twoc.ini into the RECOMP_* variables the launcher left unset. */
void twoc_settings_load(void);
/* The settings in effect, from the RECOMP_* variables. */
void twoc_settings_current(twoc_settings_t *s);
/* Step 93: the Display tab's settings in `s` back to their defaults (what a
 * run without the file has): render size, render scale, pacing, frame rate,
 * adaptive, render thread, the counter, the HUD; step 104: and the Graphics
 * tab's, anisotropy, anti-aliasing, ambient occlusion. Fullscreen, the aspect
 * ratio, the graphics card (step 97), the Extras, audio, controls and saves
 * are left as they are. */
void twoc_settings_display_defaults(twoc_settings_t *s);
/* Step 104: the Extras tab's settings back to their defaults: Character fur
 * on, the hub's see-through gems off, the startup message on, the menu's
 * pause on, the pause in the background off, skipping the title off. */
void twoc_settings_extras_defaults(twoc_settings_t *s);
/* Write `s` to crash_twoc.ini. Returns 0 on success, 1 when the file is
 * ignored this run (step 93: RECOMP_IGNORE_INI, the safe settings: the
 * file is neither read nor written), -1 when it couldn't be written. */
int twoc_settings_save(const twoc_settings_t *s);
int twoc_settings_ignored(void);
const char *twoc_settings_path(void);
const char *twoc_pacing_name(int pacing);
int twoc_pacing_parse(const char *v);   /* TWOC_PACING_AUTO for anything unknown */
int twoc_framerate_parse(const char *v);    /* step 77: 0 (60, unset, unknown), 1 (display), N Hz */
void twoc_framerate_text(int fr, char *buf, size_t cap);    /* the file's word: 60, display or N */
const char *twoc_aa_name(int aa);       /* the file's word: off, fxaa, smaa, msaa2x, msaa4x, msaa8x */
int twoc_aa_parse(const char *v);       /* TWOC_AA_OFF for anything unknown */
const char *twoc_rt_name(int rt);       /* step 79: off, on, auto */
int twoc_rt_parse(const char *v);       /* TWOC_RT_AUTO unset, _OFF for anything unknown (sync, a check, is on) */
const char *twoc_ao_name(int ao);       /* step 92: off, ssao, gtao */
int twoc_ao_parse(const char *v);       /* TWOC_AO_OFF for anything unknown */
int twoc_ao_strength_parse(const char *v);  /* 0-100, TWOC_AO_STRENGTH_DEFAULT unset or unknown */
unsigned twoc_aa_samples(int aa);       /* MSAA's samples a pixel; 1 for the others */
int twoc_aa_post(int aa);               /* step 90: the filter on the finished frame: TWOC_AA_FXAA, _SMAA or _OFF */
int twoc_render_scale_parse(const char *v);     /* step 90: 25-200, 100 unset or unknown */
int twoc_show_fps_parse(const char *v);         /* step 93: 0 off (unset), 1 Simple, 2 Detailed */
/* Step 91: the render size's arithmetic, the bridge's and the overlay's
 * (notes 18.56, 18.67, 18.93). render_size "WxH" (each 64-8192) or "native"
 * (the primary monitor's mode) into its box *pw x *ph; "" or "0" is 0 x 0,
 * the game's size; -1 (and 0 x 0) for anything else. */
#define TWOC_RENDER_SIZE_MIN 64
#define TWOC_RENDER_SIZE_MAX 8192
int twoc_render_size_parse(const char *v, unsigned *pw, unsigned *ph);
/* The picture for the guest's gw x gh at width/height `wide` (0: the
 * guest's 4:3): at the guest's height without a box (rw 0), else the largest
 * of its aspect that fits in rw x rh. Widths even. */
void twoc_render_fit(unsigned rw, unsigned rh, unsigned gw, unsigned gh, double wide, unsigned *w, unsigned *h);
/* What a picture of vw x vh is drawn at, at render scale `pct`: the largest
 * side within the graphics card's largest texture. */
#define TWOC_RENDER_SCALE_MAX_TEX 16384u    /* D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION at feature level 11 */
void twoc_render_scaled(unsigned vw, unsigned vh, int pct, unsigned *w, unsigned *h);

/* host_audio.cpp: the process's volume in Windows' mixer (its audio session),
 * 0-100, set on a short-lived thread of its own. */
void twoc_audio_set_volume(int percent);
/* Mute the session while the overlay holds the game (a marker file records it). */
void twoc_audio_mute(int on);
/* At the start: lift a mute the overlay left when the process ended. */
void twoc_audio_startup(void);
/* Step 97: the game quitting from the menu: its voices silenced and played out, a mute of ours lifted. */
void twoc_audio_close(void);
/* The music (stream != 0) or effects volume, 0-100, in the APU's voice
 * processor; -1 (never set) is 100. */
void twoc_audio_set_kind(int stream, int percent);
/* The APU's HRTF on 3D voices, on or off (it is on unless this says off). */
void twoc_audio_set_hrtf(int on);
/* Step 102: "Character fur": the fur shells on (1, also unset), off for
 * everyone (0, step 101's "Crash's fur" off) or off for Crash only (2);
 * the ini's values 1, 0 and crash. */
#define TWOC_FUR_OFF 0
#define TWOC_FUR_ON 1
#define TWOC_FUR_CRASH 2
int twoc_fur_parse(const char *v);
/* Step 102 (recomp_manual.c): Droid Void's background drone muted (1) or as
 * the game plays it (0), from the next time it starts. */
void twoc_set_drone_mute(int on);

#ifdef __cplusplus
}
#endif

#endif
