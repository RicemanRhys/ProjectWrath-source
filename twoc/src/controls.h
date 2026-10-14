/**
 * Controls (step 57): the PC controller's buttons remapped onto the Xbox
 * controller the game reads, and the keyboard as a second pad on top of it.
 *
 * The game reads one thing, the Xbox controller's state, through the
 * hand-written XPP poll (recomp_manual.c). The runtime fills that state from
 * XInput by a fixed table (xboxrecomp xinput_device.c: A B X Y as themselves,
 * LB as Black, RB as White, the triggers, sticks, d-pad, Start, Back). The
 * poll then
 *   1. remaps it (controls_apply_pad): each Xbox input takes the PC input the
 *      map names. The default map is that table exactly, and the default map
 *      is not applied at all: the state is the runtime's, unchanged;
 *   2. adds the keyboard (controls_apply_keys): a key down presses the Xbox
 *      input it is bound to, on top of the controller; movement keys give the
 *      stick full deflection (diagonals on the circle) and take over that
 *      stick while held. Only while the bridge window has the focus (its own
 *      key messages), and never while Alt or a Windows key is held (Alt+Enter,
 *      Alt+Tab).
 *
 * Settings (settings.c, crash_twoc.ini):
 *   pad_<xbox input> = <pc input>        RECOMP_PAD_<XBOX INPUT>
 *   key_<xbox input> = <key> [<key>] | none    RECOMP_KEY_<XBOX INPUT>
 *   extra_<pc input> = <xbox button> | none    RECOMP_EXTRA_<PC INPUT> (step 70: paddles, touchpad, ...)
 *   keyboard = 0 | 1                      RECOMP_KEYBOARD (unset: on)
 * A binding at its default is not written.
 *
 * Step 70 (notes 18.73): the PC controller is every pad the PC has (pads.h),
 * by position: the map's PC inputs are a PlayStation pad's Cross, Circle,
 * Square, Triangle for A, B, X, Y, and a Switch pad's bottom, right, left
 * and top buttons. The other pads' inputs come in through apply_pad's
 * `more`; with none (no pad but XInput's) the default map still leaves the
 * state as it is.
 */
#ifndef TWOC_CONTROLS_H
#define TWOC_CONTROLS_H

#ifdef __cplusplus
extern "C" {
#endif

/* The Xbox controller's inputs, as the game names them. The first 8 are its
 * analog buttons (bAnalogButtons order), the next 8 its digital ones; PC
 * controller inputs use the same numbers (LB = Black, RB = White). */
enum {
    XIN_A, XIN_B, XIN_X, XIN_Y, XIN_BLACK, XIN_WHITE, XIN_LT, XIN_RT,
    XIN_START, XIN_BACK, XIN_LS, XIN_RS, XIN_UP, XIN_DOWN, XIN_LEFT, XIN_RIGHT,
    XIN_BUTTONS,                        /* 16: the buttons above; the sticks below */
    XIN_LSTICK = XIN_BUTTONS, XIN_RSTICK,
    XIN_COUNT
};

/* Step 70 (notes 18.73): PC controller inputs past the Xbox layout, from pads
 * read through SDL (pads.h): the guide button, four paddles (SDL's right 1,
 * left 1, right 2, left 2: a Steam Controller's or Steam Deck's R4 L4 R5 L5,
 * a DualSense Edge's back buttons), the touchpad click (a Steam Controller's
 * or Deck's left trackpad) and six more (misc1: Share, Capture, Mute or the
 * Steam Controller's "..."; misc2: its right trackpad click). They are no
 * Xbox input's own: each may press one Xbox button on top of the map, its
 * "extra button" binding. */
enum {
    SRC_GUIDE = XIN_COUNT, SRC_PADDLE1, SRC_PADDLE2, SRC_PADDLE3, SRC_PADDLE4,
    SRC_TOUCHPAD, SRC_MISC1, SRC_MISC2, SRC_MISC3, SRC_MISC4, SRC_MISC5, SRC_MISC6,
    SRC_COUNT
};
#define SRC_EXTRAS (SRC_COUNT - XIN_COUNT)
#define XIN_NONE 0xFF                   /* an extra button bound to nothing */

/* Keyboard bindings: the 16 buttons, then each stick's four directions. */
enum {
    KIN_LUP = XIN_BUTTONS, KIN_LDOWN, KIN_LLEFT, KIN_LRIGHT,
    KIN_RUP, KIN_RDOWN, KIN_RLEFT, KIN_RRIGHT,
    KIN_COUNT
};
#define KIN_SLOTS 2                     /* keys per binding */

typedef struct {
    unsigned char pad[XIN_COUNT];       /* the PC input (an XIN_ number) each Xbox input takes */
    unsigned char key[KIN_COUNT][KIN_SLOTS];    /* virtual-key codes, 0: none */
    unsigned char extra[SRC_EXTRAS];    /* step 70: the Xbox button each SRC_ input presses, or XIN_NONE */
} twoc_controls_t;

/* The map, and the extra buttons' defaults: the four paddles (a Steam
 * Controller's back grips) as B (left) and X (right) on the upper ones, Y
 * (left) and A (right) on the lower; the rest bound to nothing. */
void controls_default_pad(twoc_controls_t *c);
void controls_default_keys(twoc_controls_t *c);
int controls_pad_is_default(const twoc_controls_t *c);

/* Names. ini: the word in crash_twoc.ini; label: what the overlay shows. */
const char *controls_xin_ini(int xin);          /* "a", "black", "lstick" */
const char *controls_xin_label(int xin);        /* "A", "Black", "Left stick" */
const char *controls_xin_hint(int xin);         /* the game's own use of it, or "" */
const char *controls_src_ini(int src);          /* "lb", "start" */
const char *controls_src_label(int src);        /* "LB", "Menu (Start)"; pads_src_label names by pad */
const char *controls_extra_ini(int src);        /* SRC_ inputs: "paddle1", "touchpad"; ini key extra_<this> */
const char *controls_kin_ini(int kin);          /* "a", "lstick_up" */
const char *controls_kin_label(int kin);
const char *controls_key_label(int vk);         /* "Space", "W"; "" for none */

/* Settings text. pad: one PC input; keys: "space k" or "none". Parse returns
 * 0, or -1 for a word it doesn't know (the binding is left as it was). */
int controls_parse_pad(const char *v, int xin, twoc_controls_t *c);
int controls_parse_keys(const char *v, int kin, twoc_controls_t *c);
void controls_pad_text(const twoc_controls_t *c, int xin, char *buf, unsigned cap);
void controls_keys_text(const twoc_controls_t *c, int kin, char *buf, unsigned cap);
/* Step 70: an extra button (`src` an SRC_ number): an Xbox button's ini word
 * ("a", "black", "start") or "none". */
int controls_parse_extra(const char *v, int src, twoc_controls_t *c);
void controls_extra_text(const twoc_controls_t *c, int src, char *buf, unsigned cap);

/* Editing (the overlay). Assigning a PC input that another Xbox input has
 * swaps the two, so every Xbox input keeps a way in. A key bound elsewhere
 * moves here. */
void controls_assign_pad(twoc_controls_t *c, int xin, int src);
/* Step 70: an extra button (SRC_) presses Xbox button `xin` (XIN_NONE: nothing). */
void controls_assign_extra(twoc_controls_t *c, int src, int xin);
void controls_assign_key(twoc_controls_t *c, int kin, int slot, int vk);

/* The map and bindings in effect, and whether the keyboard plays. Any thread. */
void controls_set(const twoc_controls_t *c, int keyboard);
int controls_keyboard_on(void);

/* The poll (recomp_manual.c). `g` is an XBOX_GAMEPAD. apply_keys returns 1
 * when the keyboard's part changed since the last call. Step 70: `more`, a
 * pads_src_t or NULL, is the other pads' PC inputs, joined with g's before
 * the map (buttons at their highest, each stick from the one pushed
 * furthest). With the default map and no `more`, g is left as it is. */
void controls_apply_pad(void *g, const void *more);
int controls_apply_keys(void *g);

/* Waiting for a press (the overlay): begin records what is held now; poll
 * returns what was newly pressed since, or -1. Keyboard: a virtual-key code
 * of a bindable key. Controller: an XIN_ number, sticks only when `stick`;
 * step 70: or an SRC_ number for an extra button (not with `stick`). Every
 * pad there is counts (pads_capture_state). */
void controls_capture_begin(void);
int controls_capture_key(void);
int controls_capture_pad(int stick);

#ifdef __cplusplus
}
#endif

#endif
