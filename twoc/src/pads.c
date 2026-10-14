/**
 * Pads (step 70, notes 18.73): every controller the PC has. See pads.h.
 *
 * Threads: SDL is the pads thread's alone (it starts SDL, pumps its events,
 * opens and reads the pads, rumbles them, stops SDL). Everyone else reads
 * the snapshot it publishes under g_lock, and XInput directly.
 */

#include <windows.h>
#include <xinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "xinput_xbox.h"
#include "pads.h"

#define MAX_PADS 8
#define VID_VALVE 0x28DE
#define PID_STEAM_VIRTUAL 0x11FF        /* Steam Input's virtual pad (SDL's USB_PRODUCT_STEAM_VIRTUAL_GAMEPAD) */
#define PID_STEAM_DECK 0x1205

typedef struct {
    SDL_JoystickID id;
    SDL_Gamepad *gp;                    /* the pads thread's; NULL in the snapshot */
    char name[96];
    const char *type;
    int style;
    unsigned short vid, pid;
    pads_src_t s;
    unsigned char has[SRC_COUNT];
} sdl_pad_t;

typedef struct {
    int present;
    unsigned short vid, pid;
    int virt;                           /* Steam Input's virtual pad */
    char name[96];                      /* SDL's name for it, when SDL lists it */
    SDL_JoystickID sdl_id;
} xi_user_t;

static volatile LONG g_mode = PADS_XINPUT_ONLY;

/* The snapshot. */
static SRWLOCK g_lock = SRWLOCK_INIT;
static sdl_pad_t g_pads[MAX_PADS];
static int g_npads;
static xi_user_t g_xi[XUSER_MAX_COUNT];
static int g_xi_known;                  /* g_xi is the thread's, not a guess */
static unsigned g_packets;              /* the SDL pads' changes so far */
static int g_style = -1;                /* the pad last pressed */
static char g_version[96];

/* The game's vibration, for the thread. */
static volatile LONG g_rumble;          /* low << 16 | high */

static HANDLE g_thread, g_stop;
static SRWLOCK g_mode_lock = SRWLOCK_INIT;

/* ── Names ──────────────────────────────────────────────────── */

const char *pads_mode_name(int mode)
{
    return mode == PADS_XINPUT_ONLY ? "xinput" : "sdl";
}

int pads_mode_parse(const char *v)
{
    return v && _stricmp(v, "xinput") == 0 ? PADS_XINPUT_ONLY : PADS_SDL;
}

int pads_mode(void)
{
    return (int)g_mode;
}

/* The SDL button each PC input is, by position; -1: an axis or a stick. */
static const int g_sdl_button[SRC_COUNT] = {
    [XIN_A] = SDL_GAMEPAD_BUTTON_SOUTH, [XIN_B] = SDL_GAMEPAD_BUTTON_EAST,
    [XIN_X] = SDL_GAMEPAD_BUTTON_WEST, [XIN_Y] = SDL_GAMEPAD_BUTTON_NORTH,
    [XIN_BLACK] = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, [XIN_WHITE] = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    [XIN_LT] = -1, [XIN_RT] = -1,
    [XIN_START] = SDL_GAMEPAD_BUTTON_START, [XIN_BACK] = SDL_GAMEPAD_BUTTON_BACK,
    [XIN_LS] = SDL_GAMEPAD_BUTTON_LEFT_STICK, [XIN_RS] = SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    [XIN_UP] = SDL_GAMEPAD_BUTTON_DPAD_UP, [XIN_DOWN] = SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    [XIN_LEFT] = SDL_GAMEPAD_BUTTON_DPAD_LEFT, [XIN_RIGHT] = SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    [XIN_LSTICK] = -1, [XIN_RSTICK] = -1,
    [SRC_GUIDE] = SDL_GAMEPAD_BUTTON_GUIDE,
    [SRC_PADDLE1] = SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, [SRC_PADDLE2] = SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,
    [SRC_PADDLE3] = SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, [SRC_PADDLE4] = SDL_GAMEPAD_BUTTON_LEFT_PADDLE2,
    [SRC_TOUCHPAD] = SDL_GAMEPAD_BUTTON_TOUCHPAD,
    [SRC_MISC1] = SDL_GAMEPAD_BUTTON_MISC1, [SRC_MISC2] = SDL_GAMEPAD_BUTTON_MISC2,
    [SRC_MISC3] = SDL_GAMEPAD_BUTTON_MISC3, [SRC_MISC4] = SDL_GAMEPAD_BUTTON_MISC4,
    [SRC_MISC5] = SDL_GAMEPAD_BUTTON_MISC5, [SRC_MISC6] = SDL_GAMEPAD_BUTTON_MISC6,
};

/* Each input's name on each kind of pad (PADS_STYLE_ order). */
static const char *const g_labels[SRC_COUNT][PADS_STYLE_COUNT] = {
    /*               Xbox          PlayStation       Nintendo (by position)  Steam Controller   Steam Deck     other */
    [XIN_A]       = { "A", "Cross", "B (bottom)", "A", "A", "A (bottom)" },
    [XIN_B]       = { "B", "Circle", "A (right)", "B", "B", "B (right)" },
    [XIN_X]       = { "X", "Square", "Y (left)", "X", "X", "X (left)" },
    [XIN_Y]       = { "Y", "Triangle", "X (top)", "Y", "Y", "Y (top)" },
    [XIN_BLACK]   = { "LB", "L1", "L", "LB", "L1", "Left shoulder" },
    [XIN_WHITE]   = { "RB", "R1", "R", "RB", "R1", "Right shoulder" },
    [XIN_LT]      = { "LT", "L2", "ZL", "LT", "L2", "Left trigger" },
    [XIN_RT]      = { "RT", "R2", "ZR", "RT", "R2", "Right trigger" },
    [XIN_START]   = { "Menu (Start)", "Options", "+ (Plus)", "Menu", "Menu", "Start" },
    [XIN_BACK]    = { "View (Back)", "Create (Share)", "- (Minus)", "View", "View", "Back" },
    [XIN_LS]      = { "Left stick click", "L3", "Left stick press", "Left stick click", "L3", "Left stick click" },
    [XIN_RS]      = { "Right stick click", "R3", "Right stick press", "Right stick click", "R3", "Right stick click" },
    [XIN_UP]      = { "D-pad up", "D-pad up", "D-pad up", "D-pad up", "D-pad up", "D-pad up" },
    [XIN_DOWN]    = { "D-pad down", "D-pad down", "D-pad down", "D-pad down", "D-pad down", "D-pad down" },
    [XIN_LEFT]    = { "D-pad left", "D-pad left", "D-pad left", "D-pad left", "D-pad left", "D-pad left" },
    [XIN_RIGHT]   = { "D-pad right", "D-pad right", "D-pad right", "D-pad right", "D-pad right", "D-pad right" },
    [XIN_LSTICK]  = { "Left stick", "Left stick", "Left stick", "Left stick", "Left stick", "Left stick" },
    [XIN_RSTICK]  = { "Right stick", "Right stick", "Right stick", "Right stick", "Right stick", "Right stick" },
    [SRC_GUIDE]   = { "Xbox button", "PS button", "Home", "Steam button", "Steam button", "Guide" },
    [SRC_PADDLE1] = { "Upper right paddle", "Right back button", "Right paddle", "R4 (upper right grip)",
                      "R4", "Right paddle 1" },
    [SRC_PADDLE2] = { "Upper left paddle", "Left back button", "Left paddle", "L4 (upper left grip)",
                      "L4", "Left paddle 1" },
    [SRC_PADDLE3] = { "Lower right paddle", "Right back button 2", "Right paddle 2", "R5 (lower right grip)",
                      "R5", "Right paddle 2" },
    [SRC_PADDLE4] = { "Lower left paddle", "Left back button 2", "Left paddle 2", "L5 (lower left grip)",
                      "L5", "Left paddle 2" },
    [SRC_TOUCHPAD] = { "Touchpad click", "Touchpad click", "Touchpad click", "Left trackpad click",
                       "Left trackpad click", "Touchpad click" },
    [SRC_MISC1]   = { "Share", "Mute", "Capture", "... (Quick Access)", "... (Quick Access)", "Misc 1" },
    [SRC_MISC2]   = { "Misc 2", "Misc 2", "Misc 2", "Right trackpad click", "Right trackpad click", "Misc 2" },
    [SRC_MISC3]   = { "Misc 3", "Misc 3", "Misc 3", "Misc 3", "Misc 3", "Misc 3" },
    [SRC_MISC4]   = { "Misc 4", "Misc 4", "Misc 4", "Misc 4", "Misc 4", "Misc 4" },
    [SRC_MISC5]   = { "Misc 5", "Misc 5", "Misc 5", "Misc 5", "Misc 5", "Misc 5" },
    [SRC_MISC6]   = { "Misc 6", "Misc 6", "Misc 6", "Misc 6", "Misc 6", "Misc 6" },
};

const char *pads_src_label(int src, int style)
{
    if (src < 0 || src >= SRC_COUNT)
        return "?";
    if (style < 0 || style >= PADS_STYLE_COUNT)
        style = PADS_STYLE_XBOX;
    return g_labels[src][style];
}

const char *pads_style_name(int style)
{
    static const char *const n[PADS_STYLE_COUNT] = { "Xbox", "PlayStation", "Nintendo", "Steam Controller",
                                                     "Steam Deck", "other pads" };
    return style >= 0 && style < PADS_STYLE_COUNT ? n[style] : n[0];
}

/* A pad's kind from SDL's type and its USB ids. */
static void pad_kind(SDL_GamepadType t, unsigned short vid, unsigned short pid, int *style, const char **type)
{
    switch (t) {
    case SDL_GAMEPAD_TYPE_XBOX360:  *style = PADS_STYLE_XBOX; *type = "Xbox 360"; return;
    case SDL_GAMEPAD_TYPE_XBOXONE:  *style = PADS_STYLE_XBOX; *type = "Xbox One / Series"; return;
    case SDL_GAMEPAD_TYPE_PS3:      *style = PADS_STYLE_PS; *type = "PlayStation 3"; return;
    case SDL_GAMEPAD_TYPE_PS4:      *style = PADS_STYLE_PS; *type = "PlayStation 4"; return;
    case SDL_GAMEPAD_TYPE_PS5:      *style = PADS_STYLE_PS; *type = "PlayStation 5"; return;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: *style = PADS_STYLE_NINTENDO; *type = "Switch Pro"; return;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: *style = PADS_STYLE_NINTENDO; *type = "Joy-Con pair"; return;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: *style = PADS_STYLE_NINTENDO; *type = "Joy-Con"; return;
    case SDL_GAMEPAD_TYPE_GAMECUBE: *style = PADS_STYLE_NINTENDO; *type = "GameCube"; return;
    default:
        break;
    }
    if (vid == VID_VALVE && pid == PID_STEAM_DECK) {
        *style = PADS_STYLE_DECK;
        *type = "Steam Deck";
    } else if (t == SDL_GAMEPAD_TYPE_STEAM) {
        *style = PADS_STYLE_STEAM;
        *type = "Steam Controller";
    } else {
        *style = PADS_STYLE_GENERIC;
        *type = "Gamepad";
    }
}

/* ── XInput users ───────────────────────────────────────────── */

typedef struct {
    XINPUT_CAPABILITIES Capabilities;
    WORD VendorId, ProductId, ProductVersion, unk1;
    DWORD unk2;
} xinput_caps_ex_t;
typedef DWORD(WINAPI *xinput_caps_ex_fn)(DWORD one, DWORD user, DWORD flags, xinput_caps_ex_t *caps);

/* XInput's own state as PC inputs (the runtime's table, xinput_device.c,
 * backed out as controls.c does: nothing is lost). */
static void xi_to_src(const XINPUT_GAMEPAD *g, pads_src_t *o)
{
    static const struct { WORD bit; int src; } b[] = {
        { XINPUT_GAMEPAD_A, XIN_A }, { XINPUT_GAMEPAD_B, XIN_B }, { XINPUT_GAMEPAD_X, XIN_X },
        { XINPUT_GAMEPAD_Y, XIN_Y }, { XINPUT_GAMEPAD_LEFT_SHOULDER, XIN_BLACK },
        { XINPUT_GAMEPAD_RIGHT_SHOULDER, XIN_WHITE }, { XINPUT_GAMEPAD_START, XIN_START },
        { XINPUT_GAMEPAD_BACK, XIN_BACK }, { XINPUT_GAMEPAD_LEFT_THUMB, XIN_LS },
        { XINPUT_GAMEPAD_RIGHT_THUMB, XIN_RS }, { XINPUT_GAMEPAD_DPAD_UP, XIN_UP },
        { XINPUT_GAMEPAD_DPAD_DOWN, XIN_DOWN }, { XINPUT_GAMEPAD_DPAD_LEFT, XIN_LEFT },
        { XINPUT_GAMEPAD_DPAD_RIGHT, XIN_RIGHT },
    };
    int i;

    memset(o, 0, sizeof(*o));
    for (i = 0; i < (int)(sizeof(b) / sizeof(b[0])); i++)
        o->v[b[i].src] = (g->wButtons & b[i].bit) ? 255 : 0;
    o->v[XIN_LT] = g->bLeftTrigger;
    o->v[XIN_RT] = g->bRightTrigger;
    o->stick[0][0] = g->sThumbLX;
    o->stick[0][1] = g->sThumbLY;
    o->stick[1][0] = g->sThumbRX;
    o->stick[1][1] = g->sThumbRY;
}

static long long push2(const short s[2])
{
    return (long long)s[0] * s[0] + (long long)s[1] * s[1];
}

/* `a` joined with `b`: buttons at their highest, each stick the furthest pushed. */
static void join(pads_src_t *a, const pads_src_t *b)
{
    int i;

    for (i = 0; i < SRC_COUNT; i++)
        if (b->v[i] > a->v[i])
            a->v[i] = b->v[i];
    for (i = 0; i < 2; i++)
        if (push2(b->stick[i]) > push2(a->stick[i])) {
            a->stick[i][0] = b->stick[i][0];
            a->stick[i][1] = b->stick[i][1];
        }
}

static int any_pressed(const pads_src_t *s)
{
    int i;

    for (i = 0; i < SRC_COUNT; i++)
        if (i != XIN_LSTICK && i != XIN_RSTICK && s->v[i] >= XBOX_ANALOG_BUTTON_THRESHOLD)
            return 1;
    return 0;
}

/* With g_lock held: pads port 0 reads besides XInput user `u`, by the snapshot. */
static int others_than(unsigned u)
{
    int n = g_npads;
    unsigned x;

    for (x = 0; x < XUSER_MAX_COUNT; x++)
        if (x != u && g_xi[x].present && !g_xi[x].virt)
            n++;
    return n;
}

/* With g_lock held: XInput user u is read (Steam Input's virtual pad isn't,
 * while another pad is). */
static int xi_read(unsigned u)
{
    return g_xi[u].present && !(g_xi[u].virt && others_than(u) > 0);
}

/* ── The poll's side (any thread) ───────────────────────────── */

int pads_read(pads_src_t *o, unsigned *packets)
{
    pads_src_t s;
    int n = 0, read[XUSER_MAX_COUNT] = { 0 }, i;
    unsigned pk = 0, u;

    memset(o, 0, sizeof(*o));
    *packets = 0;
    if (g_mode != PADS_SDL)
        return 0;
    AcquireSRWLockShared(&g_lock);
    for (i = 0; i < g_npads; i++)
        join(o, &g_pads[i].s);
    n = g_npads;
    pk = g_packets;
    for (u = 1; u < XUSER_MAX_COUNT; u++)
        read[u] = g_xi_known && xi_read(u);
    ReleaseSRWLockShared(&g_lock);
    for (u = 1; u < XUSER_MAX_COUNT; u++) {
        XINPUT_STATE x;
        if (!read[u] || XInputGetState(u, &x) != ERROR_SUCCESS)
            continue;
        xi_to_src(&x.Gamepad, &s);
        join(o, &s);
        pk += x.dwPacketNumber;
        n++;
    }
    *packets = pk;
    return n;
}

int pads_others(void)
{
    int n = 0;
    unsigned u;

    if (g_mode != PADS_SDL)
        return 0;
    AcquireSRWLockShared(&g_lock);
    n = g_npads;
    for (u = 1; u < XUSER_MAX_COUNT && g_xi_known; u++)
        n += xi_read(u);
    ReleaseSRWLockShared(&g_lock);
    return n > 0;
}

int pads_xinput_present(unsigned u)
{
    int r = -1;

    if (g_mode != PADS_SDL || u >= XUSER_MAX_COUNT)
        return -1;
    AcquireSRWLockShared(&g_lock);
    if (g_xi_known)
        r = g_xi[u].present;
    ReleaseSRWLockShared(&g_lock);
    return r;
}

int pads_skip_xinput0(void)
{
    int r = 0;

    if (g_mode != PADS_SDL)
        return 0;
    AcquireSRWLockShared(&g_lock);
    r = g_xi_known && g_xi[0].present && !xi_read(0);
    ReleaseSRWLockShared(&g_lock);
    return r;
}

void pads_rumble(unsigned short low, unsigned short high)
{
    XINPUT_VIBRATION v;
    int read[XUSER_MAX_COUNT] = { 0 };
    unsigned u;

    if (g_mode != PADS_SDL)
        return;
    InterlockedExchange(&g_rumble, (LONG)((unsigned)low << 16 | high));
    AcquireSRWLockShared(&g_lock);
    for (u = 1; u < XUSER_MAX_COUNT; u++)
        read[u] = g_xi_known && xi_read(u);
    ReleaseSRWLockShared(&g_lock);
    v.wLeftMotorSpeed = low;
    v.wRightMotorSpeed = high;
    for (u = 1; u < XUSER_MAX_COUNT; u++)
        if (read[u])
            XInputSetState(u, &v);
}

/* ── The overlay's side ─────────────────────────────────────── */

/* XInput-only mode: which users are there, asked once a second (XInput is
 * slow to say a user isn't). The overlay's thread only. */
static void xi_probe(int present[XUSER_MAX_COUNT])
{
    static int cache[XUSER_MAX_COUNT];
    static ULONGLONG last;
    ULONGLONG now = GetTickCount64();
    unsigned u;

    if (!last || now - last >= 1000) {
        last = now;
        for (u = 0; u < XUSER_MAX_COUNT; u++) {
            XINPUT_CAPABILITIES c;
            cache[u] = XInputGetCapabilities(u, XINPUT_FLAG_GAMEPAD, &c) == ERROR_SUCCESS;
        }
    }
    memcpy(present, cache, sizeof(cache));
}

int pads_list(pads_info_t *out, int max)
{
    xi_user_t xi[XUSER_MAX_COUNT];
    int read[XUSER_MAX_COUNT], present[XUSER_MAX_COUNT], n = 0, i, known, style = -1;
    unsigned u;

    AcquireSRWLockShared(&g_lock);
    memcpy(xi, g_xi, sizeof(xi));
    known = g_mode == PADS_SDL && g_xi_known;
    for (u = 0; u < XUSER_MAX_COUNT; u++)
        read[u] = known ? xi_read(u) : u == 0;
    ReleaseSRWLockShared(&g_lock);
    if (known)
        for (u = 0; u < XUSER_MAX_COUNT; u++)
            present[u] = xi[u].present;
    else
        xi_probe(present);

    for (u = 0; u < XUSER_MAX_COUNT && n < max; u++) {
        pads_info_t *p = &out[n];
        XINPUT_STATE x;
        if (!present[u])
            continue;
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s",
                 known && xi[u].virt ? "Steam Input's virtual pad" : known && xi[u].name[0] ? xi[u].name
                                                                                           : "Xbox controller");
        snprintf(p->via, sizeof(p->via), "XInput #%u", u);
        p->type = known && xi[u].virt ? "Steam virtual" : "Xbox";
        p->style = PADS_STYLE_XBOX;
        p->vid = known ? xi[u].vid : 0;
        p->pid = known ? xi[u].pid : 0;
        p->read = read[u];
        p->why = read[u] ? "" : known && xi[u].virt ? "the pad behind it is read directly"
                                                    : "XInput only reads user 0";
        if (XInputGetState(u, &x) == ERROR_SUCCESS)
            xi_to_src(&x.Gamepad, &p->s);
        for (i = 0; i < SRC_COUNT; i++)
            p->has[i] = i < SRC_GUIDE;
        if (p->read && any_pressed(&p->s))
            style = PADS_STYLE_XBOX;
        n++;
    }
    AcquireSRWLockShared(&g_lock);
    for (i = 0; i < g_npads && n < max; i++, n++) {
        pads_info_t *p = &out[n];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", g_pads[i].name);
        snprintf(p->via, sizeof(p->via), "SDL");
        p->type = g_pads[i].type;
        p->style = g_pads[i].style;
        p->vid = g_pads[i].vid;
        p->pid = g_pads[i].pid;
        p->read = 1;
        p->why = "";
        p->s = g_pads[i].s;
        memcpy(p->has, g_pads[i].has, sizeof(p->has));
    }
    ReleaseSRWLockShared(&g_lock);
    if (style >= 0) {
        AcquireSRWLockExclusive(&g_lock);
        g_style = style;
        ReleaseSRWLockExclusive(&g_lock);
    }
    return n;
}

int pads_style(void)
{
    int s;

    AcquireSRWLockShared(&g_lock);
    s = g_style >= 0 ? g_style : g_npads > 0 ? g_pads[0].style : PADS_STYLE_XBOX;
    ReleaseSRWLockShared(&g_lock);
    return s;
}

int pads_capture_state(pads_src_t *o)
{
    pads_info_t p[MAX_PADS + XUSER_MAX_COUNT];
    int n = pads_list(p, (int)(sizeof(p) / sizeof(p[0]))), i, any = 0;

    memset(o, 0, sizeof(*o));
    for (i = 0; i < n; i++)
        if (p[i].read) {
            join(o, &p[i].s);
            any = 1;
        }
    return any;
}

const char *pads_sdl_version(void)
{
    return g_version;
}

/* ── The pads thread ────────────────────────────────────────── */

static sdl_pad_t t_pads[MAX_PADS];      /* the thread's own, with their SDL_Gamepad */
static int t_npads;
static xi_user_t t_xi[XUSER_MAX_COUNT];
static unsigned t_packets;
static int t_style = -1;

static xinput_caps_ex_fn xi_caps_ex(void)
{
    static xinput_caps_ex_fn fn;
    static int tried;

    if (!tried) {
        HMODULE m = LoadLibraryA("xinput1_4.dll");
        tried = 1;
        if (m)
            fn = (xinput_caps_ex_fn)(void *)GetProcAddress(m, (LPCSTR)108);  /* XInputGetCapabilitiesEx */
    }
    return fn;
}

/* Test only: RECOMP_DEV_FAKE_XINPUT=U:VID:PID makes XInput user U look
 * plugged in with those ids (28DE:11FF: Steam Input's virtual pad), to check
 * the double-pad rule without one; its state reads as not connected. */
static int g_fake_xi = -1;
static unsigned g_fake_vid, g_fake_pid;

/* Which XInput users are there, and which is Steam Input's virtual pad. */
static void xi_refresh(void)
{
    unsigned u;

    for (u = 0; u < XUSER_MAX_COUNT; u++) {
        xi_user_t *x = &t_xi[u];
        XINPUT_CAPABILITIES c;
        int fake = g_fake_xi == (int)u;
        int present = fake || XInputGetCapabilities(u, XINPUT_FLAG_GAMEPAD, &c) == ERROR_SUCCESS;
        if (present && !x->present) {
            xinput_caps_ex_t ex;
            xinput_caps_ex_fn fn = xi_caps_ex();
            memset(&ex, 0, sizeof(ex));
            x->vid = x->pid = 0;
            if (fake) {
                x->vid = (unsigned short)g_fake_vid;
                x->pid = (unsigned short)g_fake_pid;
            } else if (fn && fn(1, u, 0, &ex) == ERROR_SUCCESS) {
                x->vid = ex.VendorId;
                x->pid = ex.ProductId;
            }
            x->virt = x->vid == VID_VALVE && x->pid == PID_STEAM_VIRTUAL;
            x->present = 1;
            if (x->virt)
                fprintf(stderr, "[PAD] + XInput #%u: Steam Input's virtual pad (%04X:%04X): read only while no "
                                "other pad is (the pad behind it is read directly)\n", u, x->vid, x->pid);
            else
                fprintf(stderr, "[PAD] + XInput #%u: %s (%04X:%04X) read through XInput%s\n", u,
                        x->name[0] ? x->name : "Xbox controller", x->vid, x->pid,
                        u == 0 ? "" : ", joined to port 0");
        } else if (!present && x->present) {
            fprintf(stderr, "[PAD] - XInput #%u: %s (%04X:%04X) gone\n", u,
                    x->virt ? "Steam Input's virtual pad" : x->name[0] ? x->name : "Xbox controller", x->vid, x->pid);
            x->present = 0;
            x->virt = 0;
        }
    }
}

static int sdl_pad_find(SDL_JoystickID id)
{
    int i;

    for (i = 0; i < t_npads; i++)
        if (t_pads[i].id == id)
            return i;
    return -1;
}

static void sdl_pad_added(SDL_JoystickID id)
{
    const char *path = SDL_GetGamepadPathForID(id), *name = SDL_GetGamepadNameForID(id);
    unsigned short vid = SDL_GetGamepadVendorForID(id), pid = SDL_GetGamepadProductForID(id);
    unsigned u;
    sdl_pad_t *p;
    int i;

    if (path && strncmp(path, "XInput#", 7) == 0) {     /* XInput's: read through XInput */
        u = (unsigned)atoi(path + 7);
        if (u < XUSER_MAX_COUNT) {
            snprintf(t_xi[u].name, sizeof(t_xi[u].name), "%s", name ? name : "");
            t_xi[u].sdl_id = id;
        }
        return;
    }
    if (sdl_pad_find(id) >= 0)
        return;
    if (t_npads == MAX_PADS) {
        fprintf(stderr, "[PAD] %s (%04X:%04X): not read, %d pads already\n", name ? name : "?", vid, pid, MAX_PADS);
        return;
    }
    p = &t_pads[t_npads];
    memset(p, 0, sizeof(*p));
    p->gp = SDL_OpenGamepad(id);
    if (!p->gp) {
        fprintf(stderr, "[PAD] %s (%04X:%04X): SDL couldn't open it: %s\n", name ? name : "?", vid, pid,
                SDL_GetError());
        return;
    }
    p->id = id;
    p->vid = vid;
    p->pid = pid;
    snprintf(p->name, sizeof(p->name), "%s", name ? name : "Gamepad");
    pad_kind(SDL_GetGamepadType(p->gp), vid, pid, &p->style, &p->type);
    for (i = 0; i < SRC_COUNT; i++)
        p->has[i] = g_sdl_button[i] >= 0 ? SDL_GamepadHasButton(p->gp, (SDL_GamepadButton)g_sdl_button[i]) : 0;
    p->has[XIN_LT] = SDL_GamepadHasAxis(p->gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    p->has[XIN_RT] = SDL_GamepadHasAxis(p->gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    p->has[XIN_LSTICK] = SDL_GamepadHasAxis(p->gp, SDL_GAMEPAD_AXIS_LEFTX);
    p->has[XIN_RSTICK] = SDL_GamepadHasAxis(p->gp, SDL_GAMEPAD_AXIS_RIGHTX);
    if (p->style == PADS_STYLE_STEAM || p->style == PADS_STYLE_DECK)
        for (i = SRC_MISC3; i <= SRC_MISC6; i++)
            p->has[i] = 0;              /* the sticks' and grips' touch sensors: a resting finger, not a press */
    t_npads++;
    fprintf(stderr, "[PAD] + %s (%s, %04X:%04X) read through SDL, joined to port 0\n", p->name, p->type, vid, pid);
}

static void sdl_pad_removed(SDL_JoystickID id)
{
    int i = sdl_pad_find(id);
    unsigned u;

    for (u = 0; u < XUSER_MAX_COUNT; u++)
        if (t_xi[u].sdl_id == id) {
            t_xi[u].sdl_id = 0;
            t_xi[u].name[0] = 0;
        }
    if (i < 0)
        return;
    fprintf(stderr, "[PAD] - %s (%s, %04X:%04X) removed\n", t_pads[i].name, t_pads[i].type, t_pads[i].vid,
            t_pads[i].pid);
    SDL_CloseGamepad(t_pads[i].gp);
    memmove(&t_pads[i], &t_pads[i + 1], (size_t)(t_npads - i - 1) * sizeof(t_pads[0]));
    t_npads--;
}

/* An SDL trigger (0-32767) as an Xbox analog button. */
static unsigned char trigger_byte(Sint16 v)
{
    return v <= 0 ? 0 : (unsigned char)(((int)v * 255 + 16383) / 32767);
}

/* SDL's y points down, the Xbox's up: negated, the centre staying 0 and
 * -32768 (all the way up) becoming 32767. */
static short stick_y(Sint16 v)
{
    return (short)(v == -32768 ? 32767 : -(int)v);
}

static void sdl_pad_read(sdl_pad_t *p)
{
    pads_src_t s;
    int i;

    memset(&s, 0, sizeof(s));
    for (i = 0; i < SRC_COUNT; i++)
        if (g_sdl_button[i] >= 0 && p->has[i])
            s.v[i] = SDL_GetGamepadButton(p->gp, (SDL_GamepadButton)g_sdl_button[i]) ? 255 : 0;
    s.v[XIN_LT] = trigger_byte(SDL_GetGamepadAxis(p->gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    s.v[XIN_RT] = trigger_byte(SDL_GetGamepadAxis(p->gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    s.stick[0][0] = SDL_GetGamepadAxis(p->gp, SDL_GAMEPAD_AXIS_LEFTX);
    s.stick[0][1] = stick_y(SDL_GetGamepadAxis(p->gp, SDL_GAMEPAD_AXIS_LEFTY));
    s.stick[1][0] = SDL_GetGamepadAxis(p->gp, SDL_GAMEPAD_AXIS_RIGHTX);
    s.stick[1][1] = stick_y(SDL_GetGamepadAxis(p->gp, SDL_GAMEPAD_AXIS_RIGHTY));
    if (memcmp(&s, &p->s, sizeof(s)) != 0) {
        if (any_pressed(&s) && !any_pressed(&p->s))
            t_style = p->style;
        p->s = s;
        t_packets++;
    }
}

/* ── Test only: SDL virtual pads (RECOMP_DEV_PAD_FILE) ──────── */

/*
 * The file's one line: a kind, then input=value pairs, e.g.
 *     ps5 a=1 lx=-32768
 *     steam paddle1=1 touchpad=1
 *     none
 * Kinds: none xbox ps4 ps5 switch steam deck generic. Inputs: the ini words
 * of controls.c (a b x y lb rb lt rt start back ls rs up down left right,
 * black/white for lb/rb, and the extra buttons guide paddle1-4 touchpad
 * misc1-6; buttons 0/1, triggers 0-255) and lx ly rx ry (Xbox-style, y up).
 * A new kind unplugs the old pad and plugs in the new one. The pad's rumble
 * is logged ([DEV] lines) as SDL hands it over.
 */
static const char *g_dev_pad_file;
static SDL_JoystickID t_vid;
static SDL_Joystick *t_vjoy;
static char t_vkind[16];
static Uint32 t_vmask;                  /* the virtual pad's buttons */

static bool SDLCALL dev_rumble(void *userdata, Uint16 low, Uint16 high)
{
    static unsigned last = 0xFFFFFFFFu;
    unsigned v = (unsigned)low << 16 | high;
    (void)userdata;
    if (v != last) {
        last = v;
        fprintf(stderr, "[DEV] virtual pad rumble: low %u high %u\n", low, high);
    }
    return true;
}

static void dev_pad_attach(const char *kind)
{
    static const struct {
        const char *kind, *name;
        unsigned short vid, pid;
        Uint32 extra;                   /* buttons past the Xbox layout it has */
    } kinds[] = {
        { "xbox", "Virtual Xbox pad (test)", 0x045E, 0x0B12, 1u << SDL_GAMEPAD_BUTTON_MISC1 },
        { "ps4", "Virtual DualShock 4 (test)", 0x054C, 0x09CC, 1u << SDL_GAMEPAD_BUTTON_TOUCHPAD },
        { "ps5", "Virtual DualSense (test)", 0x054C, 0x0CE6,
          1u << SDL_GAMEPAD_BUTTON_TOUCHPAD | 1u << SDL_GAMEPAD_BUTTON_MISC1 },
        { "switch", "Virtual Switch Pro (test)", 0x057E, 0x2009, 1u << SDL_GAMEPAD_BUTTON_MISC1 },
        { "steam", "Virtual Steam Controller (test)", VID_VALVE, 0x1302, 0xFFFFFFFFu },
        { "deck", "Virtual Steam Deck (test)", VID_VALVE, PID_STEAM_DECK, 0xFFFFFFFFu },
        { "generic", "Virtual gamepad (test)", 0x1234, 0x5678, 0 },
    };
    SDL_VirtualJoystickDesc d;
    int k;

    for (k = 0; k < (int)(sizeof(kinds) / sizeof(kinds[0])) && _stricmp(kind, kinds[k].kind) != 0; k++)
        ;
    if (k == (int)(sizeof(kinds) / sizeof(kinds[0]))) {
        fprintf(stderr, "[DEV] virtual pad: no kind '%s'\n", kind);
        return;
    }
    SDL_INIT_INTERFACE(&d);
    d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    d.vendor_id = kinds[k].vid;
    d.product_id = kinds[k].pid;
    d.naxes = SDL_GAMEPAD_AXIS_COUNT;
    d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    d.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    d.button_mask = ((1u << (SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1)) - 1) | kinds[k].extra;
    d.name = kinds[k].name;
    d.Rumble = dev_rumble;
    t_vmask = d.button_mask;
    t_vid = SDL_AttachVirtualJoystick(&d);
    t_vjoy = t_vid ? SDL_OpenJoystick(t_vid) : NULL;
    fprintf(stderr, "[DEV] virtual pad: %s (%04X:%04X) plugged in%s%s\n", kinds[k].name, kinds[k].vid, kinds[k].pid,
            t_vjoy ? "" : ": ", t_vjoy ? "" : SDL_GetError());
}

static void dev_pad_detach(void)
{
    if (!t_vid)
        return;
    if (t_vjoy)
        SDL_CloseJoystick(t_vjoy);
    SDL_DetachVirtualJoystick(t_vid);
    fprintf(stderr, "[DEV] virtual pad: %s unplugged\n", t_vkind);
    t_vjoy = NULL;
    t_vid = 0;
}

/* A virtual pad's buttons are numbered in SDL's gamepad-button order over
 * the ones its mask has (SDL_virtualjoystick.c), so gamepad button b is the
 * count of mask bits below b; -1 if it has none. Its axes are SDL's gamepad
 * axes (all there). */
static int dev_pad_button(int b)
{
    Uint32 below = t_vmask & ((1u << b) - 1);
    int n = 0;

    if (!(t_vmask & (1u << b)))
        return -1;
    for (; below; below &= below - 1)
        n++;
    return n;
}

static char t_vline[512];               /* the file's line last acted on; "" again when SDL stops */

static void dev_pad_poll(void)
{
    static ULONGLONG last;
    char *last_line = t_vline;
    ULONGLONG now = GetTickCount64();
    char line[512] = "", buf[512], *tok, *ctx = NULL;
    FILE *f;
    int first = 1, i;

    if (!g_dev_pad_file || now - last < 50)
        return;
    last = now;
    if ((f = fopen(g_dev_pad_file, "r")) != NULL) {
        if (!fgets(line, sizeof(line), f))
            line[0] = 0;
        fclose(f);
    }
    line[strcspn(line, "\r\n")] = 0;
    if (strcmp(line, last_line) == 0)
        return;
    snprintf(last_line, sizeof(t_vline), "%s", line);
    snprintf(buf, sizeof(buf), "%s", line);
    for (tok = strtok_s(buf, " \t", &ctx); tok; tok = strtok_s(NULL, " \t", &ctx), first = 0) {
        char *eq = strchr(tok, '=');
        long v;
        if (first && !eq) {
            if (_stricmp(tok, t_vkind) != 0 || !t_vid) {
                dev_pad_detach();
                snprintf(t_vkind, sizeof(t_vkind), "%s", tok);
                if (_stricmp(tok, "none") != 0)
                    dev_pad_attach(tok);
            }
            if (t_vjoy) {               /* every input not named is let go */
                for (i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++)
                    SDL_SetJoystickVirtualButton(t_vjoy, i, false);
                for (i = 0; i < SDL_GAMEPAD_AXIS_COUNT; i++)
                    SDL_SetJoystickVirtualAxis(t_vjoy, i, i >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? -32768 : 0);
            }
            continue;
        }
        if (!eq || !t_vjoy)
            continue;
        *eq = 0;
        v = strtol(eq + 1, NULL, 0);
        if (_stricmp(tok, "lx") == 0)
            SDL_SetJoystickVirtualAxis(t_vjoy, SDL_GAMEPAD_AXIS_LEFTX, (Sint16)v);
        else if (_stricmp(tok, "ly") == 0)
            SDL_SetJoystickVirtualAxis(t_vjoy, SDL_GAMEPAD_AXIS_LEFTY, (Sint16)(v >= 32767 ? -32768 : -v));
        else if (_stricmp(tok, "rx") == 0)
            SDL_SetJoystickVirtualAxis(t_vjoy, SDL_GAMEPAD_AXIS_RIGHTX, (Sint16)v);
        else if (_stricmp(tok, "ry") == 0)
            SDL_SetJoystickVirtualAxis(t_vjoy, SDL_GAMEPAD_AXIS_RIGHTY, (Sint16)(v >= 32767 ? -32768 : -v));
        else if (_stricmp(tok, "lt") == 0 || _stricmp(tok, "rt") == 0)   /* 0-255 onto the joystick's full range */
            SDL_SetJoystickVirtualAxis(t_vjoy, _stricmp(tok, "lt") == 0 ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER
                                                                       : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
                                       (Sint16)(v * 257 - 32768));
        else {
            const char *w = _stricmp(tok, "lb") == 0 ? "black" : _stricmp(tok, "rb") == 0 ? "white" : tok;
            for (i = 0; i < SRC_COUNT; i++)
                if (g_sdl_button[i] >= 0 &&
                    (_stricmp(w, controls_xin_ini(i)) == 0 || _stricmp(w, controls_extra_ini(i)) == 0)) {
                    if (dev_pad_button(g_sdl_button[i]) >= 0)
                        SDL_SetJoystickVirtualButton(t_vjoy, dev_pad_button(g_sdl_button[i]), v != 0);
                    else
                        fprintf(stderr, "[DEV] virtual pad: a %s has no %s\n", t_vkind, tok);
                    break;
                }
            if (i == SRC_COUNT)
                fprintf(stderr, "[DEV] virtual pad: no input '%s'\n", tok);
        }
    }
}

static void publish(void)
{
    int i;

    AcquireSRWLockExclusive(&g_lock);
    for (i = 0; i < t_npads; i++) {
        g_pads[i] = t_pads[i];
        g_pads[i].gp = NULL;
    }
    g_npads = t_npads;
    memcpy(g_xi, t_xi, sizeof(g_xi));
    g_xi_known = 1;
    g_packets = t_packets;
    if (t_style >= 0) {
        g_style = t_style;
        t_style = -1;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

/* Steam Input's virtual pad left out or read again: said once each way. */
static void log_virtual(void)
{
    static int was[XUSER_MAX_COUNT];
    unsigned u;

    AcquireSRWLockShared(&g_lock);
    for (u = 0; u < XUSER_MAX_COUNT; u++) {
        int out = g_xi[u].present && g_xi[u].virt && !xi_read(u);
        if (out != was[u])
            fprintf(stderr, out ? "[PAD] XInput #%u (Steam Input's virtual pad): left out, another pad is read\n"
                                : "[PAD] XInput #%u (Steam Input's virtual pad): read, no other pad is\n", u);
        was[u] = out;
    }
    ReleaseSRWLockShared(&g_lock);
}

static void rumble_apply(void)
{
    static LONG sent = 0;
    static ULONGLONG sent_ms;
    static int sent_to;
    LONG r = g_rumble;
    ULONGLONG now = GetTickCount64();
    int i;

    /* SDL wants a duration: 1 s, sent again every half second while it lasts,
     * and to a pad that came meanwhile. */
    if (r == sent && sent_to == t_npads && (!r || now - sent_ms < 500))
        return;
    for (i = 0; i < t_npads; i++)
        SDL_RumbleGamepad(t_pads[i].gp, (Uint16)((unsigned)r >> 16), (Uint16)(r & 0xFFFF), r ? 1000 : 0);
    sent = r;
    sent_ms = now;
    sent_to = t_npads;
}

static DWORD WINAPI pads_thread(void *arg)
{
    static const Uint32 quiet[] = {
        SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_JOYSTICK_BALL_MOTION, SDL_EVENT_JOYSTICK_HAT_MOTION,
        SDL_EVENT_JOYSTICK_BUTTON_DOWN, SDL_EVENT_JOYSTICK_BUTTON_UP, SDL_EVENT_JOYSTICK_BATTERY_UPDATED,
        SDL_EVENT_GAMEPAD_AXIS_MOTION, SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_EVENT_GAMEPAD_BUTTON_UP,
        SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN, SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION, SDL_EVENT_GAMEPAD_TOUCHPAD_UP,
        SDL_EVENT_GAMEPAD_SENSOR_UPDATE,
    };
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
                                          TIMER_ALL_ACCESS);
    ULONGLONG xi_ms = 0;
    int v, i;

    (void)arg;
    if (!timer)
        timer = CreateWaitableTimerW(NULL, FALSE, NULL);
    /* Leave the process's timer resolution alone (SDL raises it to 1 ms by
     * default, which would change every Sleep in the game), and Xbox pads
     * to SDL's XInput driver, whose devices are left to XInput here. */
    SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_XINPUT_ENABLED, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_XBOX, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_WGI, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_GAMEINPUT, "0");
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "[PAD] SDL didn't start: %s; XInput only\n", SDL_GetError());
        if (timer)
            CloseHandle(timer);
        return 0;
    }
    for (i = 0; i < (int)(sizeof(quiet) / sizeof(quiet[0])); i++)
        SDL_SetEventEnabled(quiet[i], false);   /* read as state; the queue only carries plug and unplug */
    v = SDL_GetVersion();
    AcquireSRWLockExclusive(&g_lock);
    snprintf(g_version, sizeof(g_version), "%d.%d.%d (%s)", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v),
             SDL_VERSIONNUM_MICRO(v), SDL_GetRevision());
    ReleaseSRWLockExclusive(&g_lock);
    fprintf(stderr, "[PAD] SDL %s started (built with %d.%d.%d): pads through SDL, Xbox pads through XInput\n",
            g_version, SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);

    while (WaitForSingleObject(g_stop, 0) != WAIT_OBJECT_0) {
        SDL_Event e;
        LARGE_INTEGER due;
        HANDLE h[2];
        int xi_changed = 0;
        SDL_PumpEvents();
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_GAMEPAD_ADDED)
                sdl_pad_added(e.gdevice.which);
            else if (e.type == SDL_EVENT_GAMEPAD_REMOVED)
                sdl_pad_removed(e.gdevice.which);
            else if (e.type == SDL_EVENT_JOYSTICK_ADDED || e.type == SDL_EVENT_JOYSTICK_REMOVED)
                xi_changed = 1;
        }
        dev_pad_poll();
        if (xi_changed || GetTickCount64() - xi_ms >= 1000) {
            xi_refresh();
            xi_ms = GetTickCount64();
        }
        for (i = 0; i < t_npads; i++)
            sdl_pad_read(&t_pads[i]);
        rumble_apply();
        publish();
        log_virtual();
        /* 2 ms with a pad to read (the game polls once a frame), 50 ms
         * waiting for one. */
        due.QuadPart = t_npads ? -20000LL : -500000LL;
        h[0] = g_stop;
        h[1] = timer;
        if (timer && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE))
            WaitForMultipleObjects(2, h, FALSE, INFINITE);
        else
            WaitForSingleObject(g_stop, t_npads ? 2 : 50);
    }

    dev_pad_detach();
    t_vline[0] = 0;                     /* a restart plugs the file's pad in again */
    for (i = 0; i < t_npads; i++) {
        SDL_RumbleGamepad(t_pads[i].gp, 0, 0, 0);
        SDL_CloseGamepad(t_pads[i].gp);
    }
    t_npads = 0;
    memset(t_xi, 0, sizeof(t_xi));
    AcquireSRWLockExclusive(&g_lock);
    g_npads = 0;
    g_xi_known = 0;
    memset(g_xi, 0, sizeof(g_xi));
    ReleaseSRWLockExclusive(&g_lock);
    SDL_Quit();
    if (timer)
        CloseHandle(timer);
    fprintf(stderr, "[PAD] SDL stopped\n");
    return 0;
}

static void start_sdl(void)
{
    if (g_thread)
        return;
    if (!g_stop)
        g_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    ResetEvent(g_stop);
    g_thread = CreateThread(NULL, 0, pads_thread, NULL, 0, NULL);
}

static void stop_sdl(void)
{
    if (!g_thread)
        return;
    SetEvent(g_stop);
    if (WaitForSingleObject(g_thread, 3000) != WAIT_OBJECT_0)
        fprintf(stderr, "[PAD] SDL's thread didn't stop within 3 s\n");
    CloseHandle(g_thread);
    g_thread = NULL;
}

void pads_init(int mode)
{
    {
        const char *f = getenv("RECOMP_DEV_FAKE_XINPUT");
        unsigned u;
        if (f && sscanf(f, "%u:%x:%x", &u, &g_fake_vid, &g_fake_pid) == 3 && u < XUSER_MAX_COUNT) {
            g_fake_xi = (int)u;
            fprintf(stderr, "[DEV] XInput user %u faked as %04X:%04X\n", u, g_fake_vid, g_fake_pid);
        }
    }
    g_dev_pad_file = getenv("RECOMP_DEV_PAD_FILE");
    if (g_dev_pad_file && !*g_dev_pad_file)
        g_dev_pad_file = NULL;
    if (g_dev_pad_file)
        fprintf(stderr, "[DEV] virtual pads from %s (SDL mode only)\n", g_dev_pad_file);
    if (mode == PADS_XINPUT_ONLY)
        fprintf(stderr, "[PAD] controller input: XInput only (SDL not started)\n");
    pads_set_mode(mode);
}

void pads_set_mode(int mode)
{
    AcquireSRWLockExclusive(&g_mode_lock);
    if (mode == PADS_SDL) {
        InterlockedExchange(&g_mode, PADS_SDL);
        start_sdl();
    } else if (g_mode != PADS_XINPUT_ONLY || g_thread) {
        InterlockedExchange(&g_mode, PADS_XINPUT_ONLY);
        stop_sdl();
        fprintf(stderr, "[PAD] controller input: XInput only\n");
    }
    ReleaseSRWLockExclusive(&g_mode_lock);
}
