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
 *   keyboard = 0 | 1                      RECOMP_KEYBOARD (unset: on)
 * A binding at its default is not written.
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
} twoc_controls_t;

void controls_default_pad(twoc_controls_t *c);
void controls_default_keys(twoc_controls_t *c);
int controls_pad_is_default(const twoc_controls_t *c);

/* Names. ini: the word in crash_twoc.ini; label: what the overlay shows. */
const char *controls_xin_ini(int xin);          /* "a", "black", "lstick" */
const char *controls_xin_label(int xin);        /* "A", "Black", "Left stick" */
const char *controls_xin_hint(int xin);         /* the game's own use of it, or "" */
const char *controls_src_ini(int src);          /* "lb", "start" */
const char *controls_src_label(int src);        /* "LB", "Menu (Start)" */
const char *controls_kin_ini(int kin);          /* "a", "lstick_up" */
const char *controls_kin_label(int kin);
const char *controls_key_label(int vk);         /* "Space", "W"; "" for none */

/* Settings text. pad: one PC input; keys: "space k" or "none". Parse returns
 * 0, or -1 for a word it doesn't know (the binding is left as it was). */
int controls_parse_pad(const char *v, int xin, twoc_controls_t *c);
int controls_parse_keys(const char *v, int kin, twoc_controls_t *c);
void controls_pad_text(const twoc_controls_t *c, int xin, char *buf, unsigned cap);
void controls_keys_text(const twoc_controls_t *c, int kin, char *buf, unsigned cap);

/* Editing (the overlay). Assigning a PC input that another Xbox input has
 * swaps the two, so every Xbox input keeps a way in. A key bound elsewhere
 * moves here. */
void controls_assign_pad(twoc_controls_t *c, int xin, int src);
void controls_assign_key(twoc_controls_t *c, int kin, int slot, int vk);

/* The map and bindings in effect, and whether the keyboard plays. Any thread. */
void controls_set(const twoc_controls_t *c, int keyboard);
int controls_keyboard_on(void);

/* The poll (recomp_manual.c). `g` is an XBOX_GAMEPAD. apply_keys returns 1
 * when the keyboard's part changed since the last call. */
void controls_apply_pad(void *g);
int controls_apply_keys(void *g);

/* Waiting for a press (the overlay): begin records what is held now; poll
 * returns what was newly pressed since, or -1. Keyboard: a virtual-key code
 * of a bindable key. Controller: an XIN_ number, sticks only when `stick`. */
void controls_capture_begin(void);
int controls_capture_key(void);
int controls_capture_pad(int stick);

#ifdef __cplusplus
}
#endif

#endif
