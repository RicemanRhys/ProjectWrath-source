/**
 * Controls (step 57): the controller map and the keyboard. See controls.h.
 */

#include <windows.h>
#include <xinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xinput_xbox.h"
#include "controls.h"

#pragma comment(lib, "xinput.lib")

#define MAXI(a, b) ((a) > (b) ? (a) : (b))
#define MINI(a, b) ((a) < (b) ? (a) : (b))

/* ── Names ──────────────────────────────────────────────────── */

static const struct {
    const char *ini, *label, *src_ini, *src_label, *hint;
} g_xin[XIN_COUNT] = {
    { "a", "A", "a", "A", "Jump" },
    { "b", "B", "b", "B", "Crouch, slide, body slam" },
    { "x", "X", "x", "X", "Spin" },
    { "y", "Y", "y", "Y", "" },
    { "black", "Black", "lb", "LB", "" },
    { "white", "White", "rb", "RB", "" },
    { "lt", "Left trigger", "lt", "LT", "Raise the bazooka" },
    { "rt", "Right trigger", "rt", "RT", "Run faster (shoes)" },
    { "start", "Start", "start", "Menu (Start)", "Pause" },
    { "back", "Back", "back", "View (Back)", "" },
    { "ls", "Left stick click", "ls", "Left stick click", "" },
    { "rs", "Right stick click", "rs", "Right stick click", "" },
    { "up", "D-pad up", "up", "D-pad up", "" },
    { "down", "D-pad down", "down", "D-pad down", "" },
    { "left", "D-pad left", "left", "D-pad left", "" },
    { "right", "D-pad right", "right", "D-pad right", "" },
    { "lstick", "Left stick", "lstick", "Left stick", "Move" },
    { "rstick", "Right stick", "rstick", "Right stick", "" },
};

static const struct {
    const char *ini, *label;
} g_kin_dirs[KIN_COUNT - XIN_BUTTONS] = {
    { "lstick_up", "Left stick up" }, { "lstick_down", "Left stick down" },
    { "lstick_left", "Left stick left" }, { "lstick_right", "Left stick right" },
    { "rstick_up", "Right stick up" }, { "rstick_down", "Right stick down" },
    { "rstick_left", "Right stick left" }, { "rstick_right", "Right stick right" },
};

/* The keys a binding may take: not Esc (the overlay), Alt or the Windows
 * keys (the keyboard is ignored while they are held), F12 or the mouse. */
static const struct {
    unsigned char vk;
    const char *ini, *label;
} g_keys[] = {
    { VK_SPACE, "space", "Space" }, { VK_RETURN, "enter", "Enter" }, { VK_TAB, "tab", "Tab" },
    { VK_BACK, "backspace", "Backspace" }, { VK_LSHIFT, "lshift", "Left Shift" },
    { VK_RSHIFT, "rshift", "Right Shift" }, { VK_LCONTROL, "lctrl", "Left Ctrl" },
    { VK_RCONTROL, "rctrl", "Right Ctrl" }, { VK_CAPITAL, "capslock", "Caps Lock" },
    { VK_UP, "up", "Up" }, { VK_DOWN, "down", "Down" }, { VK_LEFT, "left", "Left" }, { VK_RIGHT, "right", "Right" },
    { VK_INSERT, "insert", "Insert" }, { VK_DELETE, "delete", "Delete" }, { VK_HOME, "home", "Home" },
    { VK_END, "end", "End" }, { VK_PRIOR, "pageup", "Page Up" }, { VK_NEXT, "pagedown", "Page Down" },
    { 'A', "a", "A" }, { 'B', "b", "B" }, { 'C', "c", "C" }, { 'D', "d", "D" }, { 'E', "e", "E" },
    { 'F', "f", "F" }, { 'G', "g", "G" }, { 'H', "h", "H" }, { 'I', "i", "I" }, { 'J', "j", "J" },
    { 'K', "k", "K" }, { 'L', "l", "L" }, { 'M', "m", "M" }, { 'N', "n", "N" }, { 'O', "o", "O" },
    { 'P', "p", "P" }, { 'Q', "q", "Q" }, { 'R', "r", "R" }, { 'S', "s", "S" }, { 'T', "t", "T" },
    { 'U', "u", "U" }, { 'V', "v", "V" }, { 'W', "w", "W" }, { 'X', "x", "X" }, { 'Y', "y", "Y" },
    { 'Z', "z", "Z" },
    { '0', "0", "0" }, { '1', "1", "1" }, { '2', "2", "2" }, { '3', "3", "3" }, { '4', "4", "4" },
    { '5', "5", "5" }, { '6', "6", "6" }, { '7', "7", "7" }, { '8', "8", "8" }, { '9', "9", "9" },
    { VK_OEM_COMMA, "comma", "," }, { VK_OEM_PERIOD, "period", "." }, { VK_OEM_2, "slash", "/" },
    { VK_OEM_1, "semicolon", ";" }, { VK_OEM_7, "quote", "'" }, { VK_OEM_4, "lbracket", "[" },
    { VK_OEM_6, "rbracket", "]" }, { VK_OEM_5, "backslash", "\\" }, { VK_OEM_MINUS, "minus", "-" },
    { VK_OEM_PLUS, "equals", "=" }, { VK_OEM_3, "grave", "`" },
    { VK_NUMPAD0, "num0", "Num 0" }, { VK_NUMPAD1, "num1", "Num 1" }, { VK_NUMPAD2, "num2", "Num 2" },
    { VK_NUMPAD3, "num3", "Num 3" }, { VK_NUMPAD4, "num4", "Num 4" }, { VK_NUMPAD5, "num5", "Num 5" },
    { VK_NUMPAD6, "num6", "Num 6" }, { VK_NUMPAD7, "num7", "Num 7" }, { VK_NUMPAD8, "num8", "Num 8" },
    { VK_NUMPAD9, "num9", "Num 9" }, { VK_ADD, "numplus", "Num +" }, { VK_SUBTRACT, "numminus", "Num -" },
    { VK_MULTIPLY, "numstar", "Num *" }, { VK_DIVIDE, "numslash", "Num /" }, { VK_DECIMAL, "numperiod", "Num ." },
    { VK_F1, "f1", "F1" }, { VK_F2, "f2", "F2" }, { VK_F3, "f3", "F3" }, { VK_F4, "f4", "F4" },
    { VK_F5, "f5", "F5" }, { VK_F6, "f6", "F6" }, { VK_F7, "f7", "F7" }, { VK_F8, "f8", "F8" },
    { VK_F9, "f9", "F9" }, { VK_F10, "f10", "F10" }, { VK_F11, "f11", "F11" },
};
#define N_KEYS (int)(sizeof(g_keys) / sizeof(g_keys[0]))

/* Defaults: movement on WASD and the arrows, the actions under the right
 * hand (J K L I) and again under the left (E, Shift, R, Space). */
static const unsigned char g_key_defaults[KIN_COUNT][KIN_SLOTS] = {
    [XIN_A] = { VK_SPACE, 'K' },
    [XIN_B] = { 'L', VK_LSHIFT },
    [XIN_X] = { 'J', 'E' },
    [XIN_Y] = { 'I', 'R' },
    [XIN_BLACK] = { 'N', 0 },
    [XIN_WHITE] = { 'M', 0 },
    [XIN_LT] = { 'U', 'Q' },
    [XIN_RT] = { 'O', 'F' },
    [XIN_START] = { VK_RETURN, 'P' },
    [XIN_BACK] = { VK_BACK, VK_TAB },
    [KIN_LUP] = { 'W', VK_UP },
    [KIN_LDOWN] = { 'S', VK_DOWN },
    [KIN_LLEFT] = { 'A', VK_LEFT },
    [KIN_LRIGHT] = { 'D', VK_RIGHT },
};

const char *controls_xin_ini(int xin) { return xin >= 0 && xin < XIN_COUNT ? g_xin[xin].ini : ""; }
const char *controls_xin_label(int xin) { return xin >= 0 && xin < XIN_COUNT ? g_xin[xin].label : ""; }
const char *controls_xin_hint(int xin) { return xin >= 0 && xin < XIN_COUNT ? g_xin[xin].hint : ""; }
const char *controls_src_ini(int src) { return src >= 0 && src < XIN_COUNT ? g_xin[src].src_ini : ""; }
const char *controls_src_label(int src) { return src >= 0 && src < XIN_COUNT ? g_xin[src].src_label : "?"; }

const char *controls_kin_ini(int kin)
{
    return kin < XIN_BUTTONS ? controls_xin_ini(kin) : kin < KIN_COUNT ? g_kin_dirs[kin - XIN_BUTTONS].ini : "";
}

const char *controls_kin_label(int kin)
{
    return kin < XIN_BUTTONS ? controls_xin_label(kin) : kin < KIN_COUNT ? g_kin_dirs[kin - XIN_BUTTONS].label : "";
}

static int key_index(int vk)
{
    int i;

    for (i = 0; i < N_KEYS; i++)
        if (g_keys[i].vk == vk)
            return i;
    return -1;
}

const char *controls_key_label(int vk)
{
    int i = key_index(vk);
    return i >= 0 ? g_keys[i].label : "";
}

/* ── Defaults, text ─────────────────────────────────────────── */

void controls_default_pad(twoc_controls_t *c)
{
    int i;

    for (i = 0; i < XIN_COUNT; i++)
        c->pad[i] = (unsigned char)i;
}

void controls_default_keys(twoc_controls_t *c)
{
    memcpy(c->key, g_key_defaults, sizeof(c->key));
}

int controls_pad_is_default(const twoc_controls_t *c)
{
    int i;

    for (i = 0; i < XIN_COUNT; i++)
        if (c->pad[i] != i)
            return 0;
    return 1;
}

int controls_parse_pad(const char *v, int xin, twoc_controls_t *c)
{
    int s, lo = xin < XIN_BUTTONS ? 0 : XIN_BUTTONS, hi = xin < XIN_BUTTONS ? XIN_BUTTONS : XIN_COUNT;

    for (s = lo; s < hi; s++)
        if (_stricmp(v, g_xin[s].src_ini) == 0) {
            c->pad[xin] = (unsigned char)s;
            return 0;
        }
    return -1;
}

int controls_parse_keys(const char *v, int kin, twoc_controls_t *c)
{
    unsigned char k[KIN_SLOTS] = { 0 };
    char buf[64], *tok, *ctx = NULL;
    int n = 0, i;

    snprintf(buf, sizeof(buf), "%s", v);
    for (tok = strtok_s(buf, " \t,", &ctx); tok; tok = strtok_s(NULL, " \t,", &ctx)) {
        if (_stricmp(tok, "none") == 0) {   /* an empty slot: "none k" keeps K second */
            n++;
            continue;
        }
        for (i = 0; i < N_KEYS && _stricmp(tok, g_keys[i].ini) != 0; i++)
            ;
        if (i == N_KEYS)
            return -1;
        if (n < KIN_SLOTS)
            k[n++] = g_keys[i].vk;
    }
    memcpy(c->key[kin], k, sizeof(k));
    return 0;
}

void controls_pad_text(const twoc_controls_t *c, int xin, char *buf, unsigned cap)
{
    snprintf(buf, cap, "%s", controls_src_ini(c->pad[xin]));
}

void controls_keys_text(const twoc_controls_t *c, int kin, char *buf, unsigned cap)
{
    int s, i, last = -1;

    buf[0] = 0;
    for (s = 0; s < KIN_SLOTS; s++)
        if (c->key[kin][s] && key_index(c->key[kin][s]) >= 0)
            last = s;
    for (s = 0; s <= last; s++)         /* an empty slot before a key: "none" */
        snprintf(buf + strlen(buf), cap - strlen(buf), "%s%s", s ? " " : "",
                 (i = key_index(c->key[kin][s])) >= 0 && c->key[kin][s] ? g_keys[i].ini : "none");
    if (!buf[0])
        snprintf(buf, cap, "none");
}

void controls_assign_pad(twoc_controls_t *c, int xin, int src)
{
    int other;

    if (xin < 0 || xin >= XIN_COUNT || src < 0 || src >= XIN_COUNT || (xin < XIN_BUTTONS) != (src < XIN_BUTTONS))
        return;
    for (other = 0; other < XIN_COUNT; other++)
        if (other != xin && c->pad[other] == src)
            c->pad[other] = c->pad[xin];
    c->pad[xin] = (unsigned char)src;
}

void controls_assign_key(twoc_controls_t *c, int kin, int slot, int vk)
{
    int k, s;

    if (kin < 0 || kin >= KIN_COUNT || slot < 0 || slot >= KIN_SLOTS)
        return;
    if (vk)
        for (k = 0; k < KIN_COUNT; k++)
            for (s = 0; s < KIN_SLOTS; s++)
                if (c->key[k][s] == vk)
                    c->key[k][s] = 0;
    c->key[kin][slot] = (unsigned char)vk;
}

/* ── In effect ──────────────────────────────────────────────── */

static SRWLOCK g_lock = SRWLOCK_INIT;
static twoc_controls_t g_cur;
static int g_have, g_default_pad = 1;
static volatile LONG g_keyboard;

void controls_set(const twoc_controls_t *c, int keyboard)
{
    AcquireSRWLockExclusive(&g_lock);
    g_cur = *c;
    g_have = 1;
    g_default_pad = controls_pad_is_default(c);
    ReleaseSRWLockExclusive(&g_lock);
    InterlockedExchange(&g_keyboard, keyboard != 0);
}

int controls_keyboard_on(void)
{
    return g_keyboard != 0;
}

static const WORD g_bits[XIN_BUTTONS] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    XBOX_GAMEPAD_START, XBOX_GAMEPAD_BACK, XBOX_GAMEPAD_LEFT_THUMB, XBOX_GAMEPAD_RIGHT_THUMB,
    XBOX_GAMEPAD_DPAD_UP, XBOX_GAMEPAD_DPAD_DOWN, XBOX_GAMEPAD_DPAD_LEFT, XBOX_GAMEPAD_DPAD_RIGHT,
};

void controls_apply_pad(void *gp)
{
    XBOX_GAMEPAD *g = (XBOX_GAMEPAD *)gp, out;
    unsigned char map[XIN_COUNT], v[XIN_BUTTONS];
    SHORT st[2][2];
    int i;

    AcquireSRWLockShared(&g_lock);
    if (!g_have || g_default_pad) {     /* the runtime's own table: the state as it is */
        ReleaseSRWLockShared(&g_lock);
        return;
    }
    memcpy(map, g_cur.pad, sizeof(map));
    ReleaseSRWLockShared(&g_lock);

    /* The PC controller's inputs, back out of the runtime's table (which
     * loses nothing: A-RB as 0/255, the triggers and sticks as they are). */
    for (i = 0; i < 8; i++)
        v[i] = g->bAnalogButtons[i];
    for (i = 8; i < XIN_BUTTONS; i++)
        v[i] = (g->wButtons & g_bits[i]) ? 255 : 0;
    st[0][0] = g->sThumbLX;
    st[0][1] = g->sThumbLY;
    st[1][0] = g->sThumbRX;
    st[1][1] = g->sThumbRY;

    memset(&out, 0, sizeof(out));
    for (i = 0; i < XIN_BUTTONS; i++) {
        unsigned char x = v[map[i]];
        if (i < 8)
            out.bAnalogButtons[i] = x;
        else if (x >= XBOX_ANALOG_BUTTON_THRESHOLD)
            out.wButtons |= g_bits[i];
    }
    out.sThumbLX = st[map[XIN_LSTICK] - XIN_BUTTONS][0];
    out.sThumbLY = st[map[XIN_LSTICK] - XIN_BUTTONS][1];
    out.sThumbRX = st[map[XIN_RSTICK] - XIN_BUTTONS][0];
    out.sThumbRY = st[map[XIN_RSTICK] - XIN_BUTTONS][1];
    *g = out;
}

/* d3d_host.c: the keys held on the bridge window, from its own messages, so
 * only while it has the focus (none at all without the bridge). */
extern int d3dh_key_down(int vk);

static int key_down(int vk)
{
    return vk && d3dh_key_down(vk);
}

static void stick_from_keys(const unsigned char k[4][KIN_SLOTS], SHORT *x, SHORT *y)
{
    int d[4], s, i;

    for (i = 0; i < 4; i++)
        for (d[i] = 0, s = 0; s < KIN_SLOTS; s++)
            d[i] |= key_down(k[i][s]);
    i = (d[3] - d[2]);                  /* right - left */
    s = (d[0] - d[1]);                  /* up - down */
    *x = (SHORT)(i && s ? i * 23170 : i * 32767);   /* full deflection, diagonals on the circle */
    *y = (SHORT)(i && s ? s * 23170 : s * 32767);
}

int controls_apply_keys(void *gp)
{
    static XBOX_GAMEPAD last;
    XBOX_GAMEPAD *g = (XBOX_GAMEPAD *)gp, kb;
    unsigned char key[KIN_COUNT][KIN_SLOTS];
    int i, s, changed;

    memset(&kb, 0, sizeof(kb));
    if (g_keyboard && !key_down(VK_MENU) && !key_down(VK_LWIN) && !key_down(VK_RWIN)) {
        AcquireSRWLockShared(&g_lock);
        memcpy(key, g_have ? g_cur.key : g_key_defaults, sizeof(key));
        ReleaseSRWLockShared(&g_lock);
        for (i = 0; i < XIN_BUTTONS; i++)
            for (s = 0; s < KIN_SLOTS; s++)
                if (key_down(key[i][s])) {
                    if (i < 8)
                        kb.bAnalogButtons[i] = 255;
                    else
                        kb.wButtons |= g_bits[i];
                }
        stick_from_keys((const unsigned char(*)[KIN_SLOTS])&key[KIN_LUP], &kb.sThumbLX, &kb.sThumbLY);
        stick_from_keys((const unsigned char(*)[KIN_SLOTS])&key[KIN_RUP], &kb.sThumbRX, &kb.sThumbRY);
    }
    changed = memcmp(&kb, &last, sizeof(kb)) != 0;
    last = kb;
    g->wButtons |= kb.wButtons;
    for (i = 0; i < 8; i++)
        if (kb.bAnalogButtons[i])
            g->bAnalogButtons[i] = kb.bAnalogButtons[i];
    if (kb.sThumbLX || kb.sThumbLY) {   /* a movement key held: the keyboard has that stick */
        g->sThumbLX = kb.sThumbLX;
        g->sThumbLY = kb.sThumbLY;
    }
    if (kb.sThumbRX || kb.sThumbRY) {
        g->sThumbRX = kb.sThumbRX;
        g->sThumbRY = kb.sThumbRY;
    }
    return changed;
}

/* ── Waiting for a press (the overlay) ─────────────────────── */

static unsigned char g_cap_keys[256];   /* held when the wait began, until released */
static WORD g_cap_btn;
static unsigned char g_cap_trig[2], g_cap_stick[2];

/* The PC controllers' state, all ports together. Returns 0 if none is there. */
static int pads_now(WORD *btn, unsigned char trig[2], unsigned char stick[2])
{
    DWORD p;
    int any = 0;

    *btn = 0;
    trig[0] = trig[1] = stick[0] = stick[1] = 0;
    for (p = 0; p < XUSER_MAX_COUNT; p++) {
        XINPUT_STATE s;
        int m;
        if (XInputGetState(p, &s) != ERROR_SUCCESS)
            continue;
        any = 1;
        *btn |= s.Gamepad.wButtons;
        trig[0] = (unsigned char)MAXI(trig[0], s.Gamepad.bLeftTrigger);
        trig[1] = (unsigned char)MAXI(trig[1], s.Gamepad.bRightTrigger);
        m = MAXI(abs(s.Gamepad.sThumbLX), abs(s.Gamepad.sThumbLY)) >> 8;
        stick[0] = (unsigned char)MAXI(stick[0], MINI(m, 127));
        m = MAXI(abs(s.Gamepad.sThumbRX), abs(s.Gamepad.sThumbRY)) >> 8;
        stick[1] = (unsigned char)MAXI(stick[1], MINI(m, 127));
    }
    return any;
}

void controls_capture_begin(void)
{
    int i;

    for (i = 0; i < N_KEYS; i++)
        g_cap_keys[g_keys[i].vk] = (unsigned char)key_down(g_keys[i].vk);
    pads_now(&g_cap_btn, g_cap_trig, g_cap_stick);
}

int controls_capture_key(void)
{
    int i, got = -1;

    for (i = 0; i < N_KEYS; i++) {
        int vk = g_keys[i].vk, d = key_down(vk);
        if (!d)
            g_cap_keys[vk] = 0;
        else if (!g_cap_keys[vk] && got < 0)
            got = vk;
    }
    return got;
}

int controls_capture_pad(int stick)
{
    static const struct {
        WORD bit;
        int xin;
    } bits[] = {
        { XINPUT_GAMEPAD_A, XIN_A }, { XINPUT_GAMEPAD_B, XIN_B }, { XINPUT_GAMEPAD_X, XIN_X },
        { XINPUT_GAMEPAD_Y, XIN_Y }, { XINPUT_GAMEPAD_LEFT_SHOULDER, XIN_BLACK },
        { XINPUT_GAMEPAD_RIGHT_SHOULDER, XIN_WHITE }, { XINPUT_GAMEPAD_START, XIN_START },
        { XINPUT_GAMEPAD_BACK, XIN_BACK }, { XINPUT_GAMEPAD_LEFT_THUMB, XIN_LS },
        { XINPUT_GAMEPAD_RIGHT_THUMB, XIN_RS }, { XINPUT_GAMEPAD_DPAD_UP, XIN_UP },
        { XINPUT_GAMEPAD_DPAD_DOWN, XIN_DOWN }, { XINPUT_GAMEPAD_DPAD_LEFT, XIN_LEFT },
        { XINPUT_GAMEPAD_DPAD_RIGHT, XIN_RIGHT },
    };
    WORD btn;
    unsigned char trig[2], st[2];
    int i, got = -1;

    if (!pads_now(&btn, trig, st))
        return -1;
    g_cap_btn &= btn;                   /* released: may count again */
    for (i = 0; i < 2; i++) {
        if (trig[i] < XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
            g_cap_trig[i] = 0;
        if (st[i] < 40)                 /* back near the centre (under ~30%) */
            g_cap_stick[i] = 0;
    }
    if (stick) {
        for (i = 0; i < 2 && got < 0; i++)
            if (st[i] >= 80 && g_cap_stick[i] < 40)     /* pushed past ~60% */
                got = XIN_LSTICK + i;
        return got;
    }
    for (i = 0; i < (int)(sizeof(bits) / sizeof(bits[0])) && got < 0; i++)
        if ((btn & bits[i].bit) && !(g_cap_btn & bits[i].bit))
            got = bits[i].xin;
    for (i = 0; i < 2 && got < 0; i++)
        if (trig[i] >= 128 && g_cap_trig[i] < XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
            got = XIN_LT + i;
    return got;
}
