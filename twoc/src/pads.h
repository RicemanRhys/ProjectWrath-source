/**
 * Pads (step 70, notes 18.73): every controller the PC has, read together as
 * the one Xbox controller the game plays with, on port 0.
 *
 * Two ways in:
 *   - XInput, as before: the runtime's xbox_InputGetState (xboxrecomp
 *     xinput_device.c), so an Xbox pad reads exactly as it did. The XPP poll
 *     (recomp_manual.c) reads XInput user 0 itself, as before; in SDL mode
 *     the other three users are read here and joined to port 0.
 *   - SDL3's gamepad API, in SDL mode ("controller_input = sdl", the
 *     default): every other pad SDL knows (DualShock 3/4, DualSense and its
 *     Edge, Switch Pro and Joy-Con pairs, both Steam Controllers, the Steam
 *     Deck, pads with a mapping in SDL's database), on a thread of its own
 *     that opens them as they come and go and keeps a snapshot of their
 *     inputs, read by the poll without waiting on SDL. SDL's own XInput
 *     devices ("XInput#N") are left to XInput, so an Xbox pad is read once.
 *
 * A pad that shows up twice is read once. SDL hides Steam Input's virtual
 * pad (VID 28DE PID 11FF) from its list by itself; XInput doesn't, so an
 * XInput user that is one is not read while any other pad is (the pad
 * behind it is then read directly). Each pad gets a [PAD] line when it
 * comes, goes, or is left out.
 *
 * Inputs are PC controller inputs by position (controls.h: XIN_ numbers for
 * the Xbox layout, SRC_ numbers past it for paddles, touchpad clicks and the
 * rest), so Cross is A, Circle B, Square X, Triangle Y on a PlayStation pad
 * and the bottom button is A on a Switch pad whatever it is labelled.
 *
 * Rumble: the game's vibration (the XPP vibration entry) goes to XInput user
 * 0 as before and to every other pad read (SDL_RumbleGamepad, refreshed
 * while it lasts).
 *
 * XInput only ("controller_input = xinput"): SDL is not started, and port 0
 * is XInput user 0 and the keyboard, exactly as before step 70. Switched
 * live from the overlay.
 *
 * Test only: RECOMP_DEV_PAD_FILE=<path> attaches SDL virtual gamepads in this
 * process from a one-line text file reread every 50 ms (pads.c); inert when
 * unset.
 */
#ifndef TWOC_PADS_H
#define TWOC_PADS_H

#include "controls.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { PADS_XINPUT_ONLY, PADS_SDL };

/* How the overlay names a pad's inputs. */
enum { PADS_STYLE_XBOX, PADS_STYLE_PS, PADS_STYLE_NINTENDO, PADS_STYLE_STEAM, PADS_STYLE_DECK,
       PADS_STYLE_GENERIC, PADS_STYLE_COUNT };

/* PC controller inputs, every pad's together or one pad's. Buttons and
 * triggers 0-255 at their SRC_/XIN_ number; the sticks Xbox-style (y up). */
typedef struct {
    unsigned char v[SRC_COUNT];         /* v[XIN_LSTICK], v[XIN_RSTICK]: unused */
    short stick[2][2];                  /* left, right: x, y */
} pads_src_t;

/* Once, at the start (main.c), with the setting's mode. */
void pads_init(int mode);
/* Live: SDL started or stopped (overlay). */
void pads_set_mode(int mode);
int pads_mode(void);
const char *pads_mode_name(int mode);   /* "sdl", "xinput": the ini's words */
int pads_mode_parse(const char *v);     /* PADS_SDL unless "xinput" */

/* The poll (recomp_manual.c), port 0. pads_read fills `o` with every pad
 * port 0 reads besides XInput user 0, joined (buttons at their highest, a
 * stick from the pad pushing it furthest) and returns how many there were;
 * *packets counts their changes. 0 in XInput-only mode. */
int pads_read(pads_src_t *o, unsigned *packets);
/* A pad besides XInput user 0 is there for port 0 (SDL mode). */
int pads_others(void);
/* XInput user `u` is plugged in, as the pads thread last saw (SDL mode;
 * -1: not known, XInput-only mode or SDL not up). */
int pads_xinput_present(unsigned u);
/* XInput user 0 is Steam Input's virtual pad and another pad is read: the
 * poll leaves its state out. */
int pads_skip_xinput0(void);
/* The game's vibration on port 0, for every pad but XInput user 0. */
void pads_rumble(unsigned short low, unsigned short high);

/* The overlay. One entry per pad there is, read or not. */
typedef struct {
    char name[96];
    char via[32];                       /* "XInput #1", "SDL" */
    const char *type;                   /* "PlayStation 5", "Xbox", "Steam Controller" */
    int style;                          /* PADS_STYLE_* */
    unsigned short vid, pid;
    int read;                           /* 1: joined to port 0; 0: left out (why) */
    const char *why;
    pads_src_t s;                       /* its inputs now */
    unsigned char has[SRC_COUNT];       /* 1: it has that input */
} pads_info_t;
int pads_list(pads_info_t *out, int max);
/* The style the Controls tab names inputs by: the pad last pressed, else the
 * first one there, else Xbox. */
int pads_style(void);
/* Every pad's inputs now, XInput users included (the overlay's "press the
 * new button"). Returns 0 if no pad is there. */
int pads_capture_state(pads_src_t *o);
const char *pads_sdl_version(void);     /* "3.4.18 (...)", or "" before SDL starts */
/* A PC input's name on a kind of pad: "Cross", "R4 (upper right grip)". */
const char *pads_src_label(int src, int style);
const char *pads_style_name(int style); /* "PlayStation" */

#ifdef __cplusplus
}
#endif

#endif
