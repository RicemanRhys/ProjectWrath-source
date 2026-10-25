/**
 * The settings overlay (step 54, notes 18.57). See overlay.h.
 *
 * Dear ImGui (third_party/imgui, v1.92.9b) with its DX11 renderer on the
 * bridge's device, and no platform backend: the host window's messages are
 * turned into ImGui input events here (ovl_input), on the window thread,
 * under g_cs; ovl_frame builds and draws a frame under the same lock. While
 * the overlay is open the keyboard and mouse only reach ImGui, and the game
 * is held or (step 57, "Pause the game" off) gets an idle pad.
 *
 * Step 57: three tabs (Display, Audio, Controls). Controls lists the Xbox
 * controller's inputs with the PC controller's input and the keys that press
 * each (controls.c); clicking one waits for the new button or key, polled
 * here each frame (controls_capture_*), and ImGui gets no keys meanwhile.
 *
 * Step 70: Controls also chooses the controller input (SDL, every pad; or
 * XInput only), lists the pads there are with a live view of their inputs
 * (a controller test), names the PC controller's inputs as the pad in use
 * does (Cross, Circle; B at the bottom of a Switch pad; R4 on a Steam
 * Controller), and binds the extra buttons (paddles, trackpad clicks) each
 * to an Xbox button.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"

#include "overlay.h"
#include "pads.h"
#include "saves.h"
#include "gpu.h"
#include "setup.h"
#include "names.h"

/* xboxrecomp apu.h: the output's channels (2 or 6) and the device's */
extern "C" int mcpx_apu_output_channels(int *device_channels);

static CRITICAL_SECTION g_cs;
static volatile LONG g_cs_ready;
static bool g_ready;
static float g_scale = 1.0f;
static ImGuiStyle g_style_base;

/* Step 57: a binding waiting for its button or key (under g_cs). */
enum { CAP_NONE, CAP_PAD, CAP_KEY, CAP_FCKEY, CAP_FCPAD };  /* step 104: the free camera's keys and buttons */
static int g_cap, g_cap_idx, g_cap_slot;
static int g_fc_refused;                /* step 104: a key the game has, refused for a toggle (its VK) */

/* Step 82: the controller drives the overlay (pads.h's OVL_PAD_* bits from
 * the frame's info): the D-pad or the left stick move, A chooses, B closes a
 * list or the overlay, LB and RB change tabs. A button held when the
 * overlay opens or a binding ends counts once it is let go. Under g_cs. */
static unsigned g_pad_mask, g_pad_last;
static int g_pad_fresh = 1;
static int g_tab_cur, g_tab_want = -1;
/* Display, Audio, Controls, Saves (step 83), About (step 93); step 104: Graphics after Display, Extras before
 * About */
enum { TAB_DISPLAY, TAB_GRAPHICS, TAB_AUDIO, TAB_CONTROLS, TAB_SAVES, TAB_EXTRAS, TAB_ABOUT, N_TABS };
#define TWOC_FAST_TITLE_SAVES "20 seconds" /* step 90: the menu at 5.9 s from the launch instead of 27.6 */
static int g_sv_shown;                  /* step 83: the Saves tab shown since the overlay opened */
#define RESET_ASK "Reset the Display settings?"     /* step 93: the reset button's question */
#define XRESET_ASK "Reset the Extras?"  /* step 104: the Extras tab's */
static bool g_reset_shut;               /* step 93: the overlay opened again: the question isn't left open */
static bool g_reset_open;               /* ... a reset question is up (last frame) */
static int g_reset_req;                 /* step 104: a tab's reset button pressed: 1 Display, 2 Extras (opened by
                                         * build, in the window the questions live in) */

/* Step 97: Quit game's and Restart now's questions (the reset's way: the safe answer first, B closes), and
 * the window's steady height. */
#define QUIT_ASK "Quit the game?"
#define RESTART_ASK "Restart the game?"
static bool g_quit_open, g_restart_open;   /* up (last frame) */
#define SETUP_ASK "Choose another disc image?"  /* step 107: the About tab's button */
static bool g_setup_open;               /* ... up (last frame) */
static int g_setup_req;                 /* ... pressed (opened by build) */
static bool g_q_shut;                   /* the overlay opened again: neither is left open */
static char g_restart_text[320];        /* the restart question's words */
static unsigned g_pend_seen;            /* the settings waiting for a restart when last looked at */
static char g_gpu_seen[128];            /* ... the graphics card chosen then */
static int g_pend_init;                 /* 0: not looked at since the overlay opened */
static char g_pend_list[160];           /* "Graphics card, Speakers" (ovl_restart_why) */
static bool g_measure;                  /* the tabs drawn off-screen, every optional row shown, to measure them */
static float g_tab_h[N_TABS];           /* ... their heights */
static int g_meas_need = 1;             /* measure them again (opened, another scale or size) */
static float g_child_h;                 /* the tabs' height in the window: the tallest's, or the room there is */
static float g_fixed_h;                 /* the window's height outside the tabs' child, from the frame before */
#define TAB_MARGIN_ROWS 2.0f            /* rows the measure may have missed (a line that wraps one more) */
#define TAB_MIN_ROWS 8.0f               /* the least the tabs get before the whole menu is made smaller */
static int g_end_kind;                  /* OVL_QUIT or OVL_RESTART once asked for (the status line's word) */

/* Step 104: Menu scale. The player's percentage multiplies the menu's own scale (the DPI, 0.8 over the running
 * game, the cap to the picture's width, the fit to its height), capped to what the window holds. */
#define MS_STEP 10                      /* percent a D-pad or arrow press moves it */
#define MS_MIN_SCALE 0.5f               /* the smallest the menu is drawn at: 9 px text */
static int g_ms_shown = 100;            /* the percentage shown, of what the menu is at 100% */
static bool g_ms_limited;               /* ... less than chosen: the window holds no more */
static int g_ms_drag = -1;              /* the mouse dragging the slider: its value, applied when let go */
static int g_ms_warp;                   /* the scale changed by the mouse: the pointer back onto the slider */
#define MS_WARP_READ 6                  /* ... in this many shown frames */
static int g_scroll_focus;              /* frames left to keep the focused item in view after a scale change */
static ImVec2 g_ms_warp_at;             /* ... where, in the picture's pixels */

static void capture_end(void)
{
    g_cap = CAP_NONE;
    g_pad_fresh = 1;                    /* step 82: the button just bound navigates once let go */
    if (g_ready)
        ImGui::GetIO().ClearInputKeys();    /* the keys held meanwhile never reached ImGui */
}

/* Step 82: the frame's pad into ImGui's gamepad navigation. Returns
 * OVL_CLOSE when B asks to close (no list open). */
static int pad_nav(const ovl_info_t *in)
{
    ImGuiIO &io = ImGui::GetIO();
    unsigned pad = in->pad, nav = 0, press;
    float x = in->pad_lx / 32767.0f, y = in->pad_ly / 32767.0f;
    const float dz = 0.35f;
    int r = 0;

    if (pad)
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    else
        io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    if (pad && g_cap == CAP_NONE) {
        if (g_pad_fresh) {
            g_pad_mask = pad;
            g_pad_fresh = 0;
        }
        g_pad_mask &= pad;              /* let go: no longer masked */
        nav = pad & ~g_pad_mask;
    } else {
        x = y = 0.0f;
    }
    press = nav & ~g_pad_last;
    g_pad_last = nav;
    io.AddKeyEvent(ImGuiKey_GamepadDpadUp, (nav & OVL_PAD_UP) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, (nav & OVL_PAD_DOWN) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, (nav & OVL_PAD_LEFT) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, (nav & OVL_PAD_RIGHT) != 0);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, x < -dz, x < -dz ? -x : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, x > dz, x > dz ? x : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, y > dz, y > dz ? y : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, y < -dz, y < -dz ? -y : 0.0f);
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, (nav & OVL_PAD_A) != 0);
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        io.AddKeyEvent(ImGuiKey_GamepadFaceRight, (nav & OVL_PAD_B) != 0);     /* closes the list */
    else {
        io.AddKeyEvent(ImGuiKey_GamepadFaceRight, false);
        if (press & OVL_PAD_B)
            r |= OVL_CLOSE;
    }
    if (g_reset_open || g_quit_open || g_restart_open || g_setup_open)
        press &= ~(OVL_PAD_LB | OVL_PAD_RB);    /* step 93: the question answered first, on its own tab */
    if (press & OVL_PAD_LB)
        g_tab_want = (g_tab_cur + N_TABS - 1) % N_TABS;
    if (press & OVL_PAD_RB)
        g_tab_want = (g_tab_cur + 1) % N_TABS;
    return r;
}

static ImGuiKey vk_key(WPARAM vk)
{
    switch (vk) {
    case VK_TAB:    return ImGuiKey_Tab;
    case VK_LEFT:   return ImGuiKey_LeftArrow;
    case VK_RIGHT:  return ImGuiKey_RightArrow;
    case VK_UP:     return ImGuiKey_UpArrow;
    case VK_DOWN:   return ImGuiKey_DownArrow;
    case VK_PRIOR:  return ImGuiKey_PageUp;
    case VK_NEXT:   return ImGuiKey_PageDown;
    case VK_HOME:   return ImGuiKey_Home;
    case VK_END:    return ImGuiKey_End;
    case VK_SPACE:  return ImGuiKey_Space;
    case VK_RETURN: return ImGuiKey_Enter;
    case VK_BACK:   return ImGuiKey_Backspace;
    case VK_DELETE: return ImGuiKey_Delete;
    case VK_SHIFT:  return ImGuiKey_LeftShift;
    case VK_CONTROL: return ImGuiKey_LeftCtrl;
    case VK_MENU:   return ImGuiKey_LeftAlt;
    case 'A':       return ImGuiKey_A;      /* step 91: Ctrl+A, C, V, X, Z in the custom size's fields */
    case 'C':       return ImGuiKey_C;
    case 'V':       return ImGuiKey_V;
    case 'X':       return ImGuiKey_X;
    case 'Z':       return ImGuiKey_Z;
    default:        return ImGuiKey_None;
    }
}

extern "C" void ovl_input(UINT msg, WPARAM wp, LPARAM lp, float sx, float sy)
{
    if (!g_cs_ready)
        return;
    EnterCriticalSection(&g_cs);
    if (g_ready) {
        ImGuiIO &io = ImGui::GetIO();
        float x = (float)(short)LOWORD(lp) * sx, y = (float)(short)HIWORD(lp) * sy;
        if (g_cap != CAP_NONE) {        /* waiting for a binding: no keys to ImGui; a click ends the wait */
            if (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) {
                LeaveCriticalSection(&g_cs);
                return;
            }
            if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN)
                capture_end();
        }
        switch (msg) {
        case WM_MOUSEMOVE:
            io.AddMousePosEvent(x, y);
            break;
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(0, msg != WM_LBUTTONUP);
            break;
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(1, msg != WM_RBUTTONUP);
            break;
        case WM_MOUSEWHEEL:
            io.AddMouseWheelEvent(0.0f, (float)GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA);
            break;
        case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP: {
            bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
            ImGuiKey k = vk_key(wp);
            if (wp == VK_CONTROL)
                io.AddKeyEvent(ImGuiMod_Ctrl, down);
            else if (wp == VK_SHIFT)
                io.AddKeyEvent(ImGuiMod_Shift, down);
            else if (wp == VK_MENU)
                io.AddKeyEvent(ImGuiMod_Alt, down);
            if (k != ImGuiKey_None)
                io.AddKeyEvent(k, down);
            break;
        }
        case WM_CHAR:                   /* step 91: typing in the custom render size's fields */
            if (wp >= 0x20 && wp != 0x7F)
                io.AddInputCharacter((unsigned)wp);
            break;
        case WM_KILLFOCUS:
            io.AddFocusEvent(false);
            break;
        case WM_SETFOCUS:
            io.AddFocusEvent(true);
            break;
        }
    }
    LeaveCriticalSection(&g_cs);
}

extern "C" void ovl_opened(void)
{
    if (!g_cs_ready)
        return;
    EnterCriticalSection(&g_cs);
    g_cap = CAP_NONE;
    g_pad_fresh = 1;                    /* step 82: buttons held now (the chord's) count once let go */
    g_sv_shown = 0;                     /* step 83: the backups listed again */
    g_reset_shut = true;                /* step 93 */
    g_reset_open = false;
    g_q_shut = true;                    /* step 97 */
    g_quit_open = g_restart_open = g_setup_open = false;
    g_setup_req = 0;
    g_pend_init = 0;
    g_meas_need = 1;
    if (g_ready) {
        ImGui::GetIO().ClearInputKeys();
        ImGui::GetIO().ClearInputMouse();
    }
    LeaveCriticalSection(&g_cs);
}

extern "C" int ovl_cancel_capture(void)
{
    int was;

    if (!g_cs_ready)
        return 0;
    EnterCriticalSection(&g_cs);
    was = g_cap != CAP_NONE;
    if (was)
        capture_end();
    LeaveCriticalSection(&g_cs);
    return was;
}

extern "C" int ovl_init(ID3D11Device *dev, ID3D11DeviceContext *ctx)
{
    static const char *const fonts[] = { "C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\arial.ttf" };
    unsigned i;

    if (!InterlockedExchange(&g_cs_ready, 1))
        InitializeCriticalSection(&g_cs);
    EnterCriticalSection(&g_cs);
    if (g_ready) {
        LeaveCriticalSection(&g_cs);
        return 0;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;              /* no imgui.ini in the game's folder */
    io.LogFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad |   /* step 82 */
                      ImGuiConfigFlags_NoMouseCursorChange;
    for (i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++)
        if (GetFileAttributesA(fonts[i]) != INVALID_FILE_ATTRIBUTES && io.Fonts->AddFontFromFileTTF(fonts[i], 18.0f))
            break;
    if (i == sizeof(fonts) / sizeof(fonts[0]))
        io.Fonts->AddFontDefault();
    ImGui::StyleColorsDark();
    ImGuiStyle &st = ImGui::GetStyle();
    st.WindowRounding = 6.0f;
    st.FrameRounding = 4.0f;
    st.WindowPadding = ImVec2(16, 14);
    st.ItemSpacing = ImVec2(10, 8);
    st.Colors[ImGuiCol_WindowBg].w = 0.94f;
    g_style_base = st;
    g_ready = ImGui_ImplDX11_Init(dev, ctx);
    if (!g_ready)
        ImGui::DestroyContext();
    LeaveCriticalSection(&g_cs);
    fprintf(stderr, "[OVL] settings overlay %s (Dear ImGui %s)\n", g_ready ? "ready" : "NOT available", IMGUI_VERSION);
    return g_ready ? 0 : -1;
}

/* Step 91: the render sizes by aspect ratio, each the box the picture fits in
 * (the largest of the chosen aspect inside it: twoc_render_fit). */
static const char *const g_sz_43[] = { "1280x960", "1600x1200", "2048x1536" };
static const char *const g_sz_1610[] = { "1280x800", "1440x900", "1680x1050", "1920x1200", "2560x1600", "3840x2400" };
static const char *const g_sz_169[] = { "1280x720", "1600x900", "1920x1080", "2560x1440", "3840x2160" };
static const char *const g_sz_219[] = { "2560x1080", "3440x1440", "3840x1600", "5120x2160" };
static const struct {
    const char *name;
    double w, h;
    const char *const *sizes;
    int n;
} g_size_groups[] = {
    { "4:3", 4, 3, g_sz_43, 3 }, { "16:10", 16, 10, g_sz_1610, 6 },
    { "16:9", 16, 9, g_sz_169, 5 }, { "21:9", 21, 9, g_sz_219, 4 },
};
#define N_SIZE_GROUPS (int)(sizeof(g_size_groups) / sizeof(g_size_groups[0]))

/* The group a listed size is in, or -1. */
static int size_listed(const char *v)
{
    int g, i;

    for (g = 0; g < N_SIZE_GROUPS; g++)
        for (i = 0; i < g_size_groups[g].n; i++)
            if (_stricmp(v, g_size_groups[g].sizes[i]) == 0)
                return g;
    return -1;
}

/* The group whose aspect w x h is (within 4%; 21:9's monitors are 2.37-2.4), or -1. */
static int size_aspect(unsigned w, unsigned h)
{
    double r = h ? (double)w / h : 0, best = 1.04;
    int g, at = -1;

    for (g = 0; r > 0 && g < N_SIZE_GROUPS; g++) {
        double q = r / (g_size_groups[g].w / g_size_groups[g].h);
        q = q < 1 ? 1 / q : q;
        if (q < best) {
            best = q;
            at = g;
        }
    }
    return at;
}

/* The monitor's mode for "native", read again at most every 2 s. */
static void native_size(unsigned *w, unsigned *h)
{
    static unsigned nw, nh;
    static ULONGLONG at;
    ULONGLONG now = GetTickCount64();

    if (!at || now - at > 2000) {
        twoc_render_size_parse("native", &nw, &nh);
        at = now;
    }
    *w = nw;
    *h = nh;
}

static const char *size_label(const char *v, char *buf, size_t cap)
{
    int g;

    if (!v[0])
        return "Game (480 lines)";
    if (_stricmp(v, "native") == 0) {
        unsigned w, h;
        native_size(&w, &h);
        if (!w)
            return "Monitor (native)";
        snprintf(buf, cap, "Monitor (native, %ux%u)", w, h);
        return buf;
    }
    if ((g = size_listed(v)) >= 0)
        snprintf(buf, cap, "%s  (%s)", v, g_size_groups[g].name);
    else
        snprintf(buf, cap, "%s  (custom)", v);
    return buf;
}

static void help(const char *text)
{
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX());
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
}

/* Step 97: a line in the menu's amber, wrapped, for something to notice. */
static const ImVec4 k_amber(1.0f, 0.8f, 0.3f, 1.0f);
static const ImVec4 k_green(0.55f, 0.9f, 0.55f, 1.0f);  /* step 107: a check that passed */

static void amber(const char *text)
{
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX());
    ImGui::TextColored(k_amber, "%s", text);
    ImGui::PopTextWrapPos();
}

/* A "(?)" after the last item, with `text` on hover. */
static void more(const char *text)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

/* An Xbox input's name, and the game's own use of it dimmed after it. */
static void input_name(const char *label, const char *hint)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (hint && hint[0]) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", hint);
    }
}

/* Step 70: one pad's inputs now, each name lit while it is pressed, and its
 * sticks and triggers as numbers: the controller test. */
static void pad_view(const pads_info_t *p)
{
    static const int order[] = { XIN_A, XIN_B, XIN_X, XIN_Y, XIN_BLACK, XIN_WHITE, XIN_START, XIN_BACK, XIN_LS,
                                 XIN_RS, XIN_UP, XIN_DOWN, XIN_LEFT, XIN_RIGHT, SRC_GUIDE, SRC_PADDLE1, SRC_PADDLE2,
                                 SRC_PADDLE3, SRC_PADDLE4, SRC_TOUCHPAD, SRC_MISC1, SRC_MISC2, SRC_MISC3, SRC_MISC4,
                                 SRC_MISC5, SRC_MISC6 };
    const ImVec4 on = ImVec4(0.45f, 1.0f, 0.45f, 1.0f);
    float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    bool first = true;
    int i;

    ImGui::Indent();
    for (i = 0; i < (int)(sizeof(order) / sizeof(order[0])); i++) {
        int x = order[i];
        const char *l = pads_src_label(x, p->style);
        bool down = p->s.v[x] >= 30;
        if (!p->has[x])
            continue;
        if (!first) {                   /* wrap at the panel's edge */
            ImGui::SameLine();
            if (ImGui::GetCursorScreenPos().x + ImGui::CalcTextSize(l).x > right)
                ImGui::NewLine();
        }
        first = false;
        if (down)
            ImGui::TextColored(on, "%s", l);
        else
            ImGui::TextDisabled("%s", l);
    }
    ImGui::Text("%s %3d  %s %3d   L stick %6d %6d   R stick %6d %6d", pads_src_label(XIN_LT, p->style),
                p->s.v[XIN_LT], pads_src_label(XIN_RT, p->style), p->s.v[XIN_RT], p->s.stick[0][0], p->s.stick[0][1],
                p->s.stick[1][0], p->s.stick[1][1]);
    ImGui::Unindent();
}

/* Step 57: the Controls tab. Step 97: it scrolls in the window's tabs'
 * child, as every tab does (it had a scrolling child of its own). */
static int tab_controls(twoc_settings_t *s)
{
    static const int pad_order[XIN_COUNT] = { XIN_A, XIN_B, XIN_X, XIN_Y, XIN_BLACK, XIN_WHITE, XIN_LT, XIN_RT,
                                              XIN_START, XIN_BACK, XIN_LSTICK, XIN_RSTICK, XIN_LS, XIN_RS,
                                              XIN_UP, XIN_DOWN, XIN_LEFT, XIN_RIGHT };
    static const int key_order[KIN_COUNT] = { KIN_LUP, KIN_LDOWN, KIN_LLEFT, KIN_LRIGHT, XIN_A, XIN_B, XIN_X,
                                              XIN_Y, XIN_START, XIN_BACK, XIN_BLACK, XIN_WHITE, XIN_LT, XIN_RT,
                                              XIN_LS, XIN_RS, XIN_UP, XIN_DOWN, XIN_LEFT, XIN_RIGHT,
                                              KIN_RUP, KIN_RDOWN, KIN_RLEFT, KIN_RRIGHT };
    float fs = ImGui::GetFontSize();
    int r = 0, i, slot, got, style, npads;
    pads_info_t pads[12];
    unsigned char has[SRC_COUNT] = { 0 };
    char lbl[160];

    if (g_measure) {                    /* step 97: measured, not used: a binding's wait isn't touched */
    } else if (g_cap == CAP_PAD && (got = controls_capture_pad(g_cap_idx >= XIN_BUTTONS)) >= 0) {
        if (got >= XIN_COUNT)           /* step 70: an extra button presses this Xbox button */
            controls_assign_extra(&s->controls, got, g_cap_idx);
        else
            controls_assign_pad(&s->controls, g_cap_idx, got);
        r |= OVL_CHANGED;
        capture_end();
    } else if (g_cap == CAP_KEY && (got = controls_capture_key()) > 0) {
        controls_assign_key(&s->controls, g_cap_idx, g_cap_slot, got);
        r |= OVL_CHANGED;
        capture_end();
    } else if (g_cap == CAP_FCKEY && (got = controls_capture_key()) > 0) {     /* step 104 */
        if (freecam_assign_key(&s->fc, &s->controls, g_cap_idx, g_cap_slot, got) == 0) {
            g_fc_refused = 0;
            r |= OVL_CHANGED;
        } else {
            g_fc_refused = got;
        }
        capture_end();
    } else if (g_cap == CAP_FCPAD && (got = controls_capture_pad(0)) >= 0) {
        if (got < XIN_BUTTONS) {
            freecam_assign_pad(&s->fc, g_cap_idx, got);
            r |= OVL_CHANGED;
        }
        capture_end();
    }
    npads = pads_list(pads, (int)(sizeof(pads) / sizeof(pads[0])));
    style = pads_style();
    for (i = 0; i < npads; i++)
        if (pads[i].read)
            for (got = 0; got < SRC_COUNT; got++)
                has[got] |= pads[i].has[got];

    /* Step 70: which pads, and how they read. */
    ImGui::SeparatorText("Controllers");
    {
        static const char *const modes[2] = { "XInput only", "SDL (all pads)" };
        int m = s->controller_input == PADS_SDL ? 1 : 0;
        ImGui::SetNextItemWidth(fs * 10);
        if (ImGui::Combo("Controller input", &m, modes, 2)) {
            s->controller_input = m ? PADS_SDL : PADS_XINPUT_ONLY;
            r |= OVL_CHANGED;
        }
        more("SDL (all pads): PlayStation, Switch, Steam Controller, Steam Deck and other pads through SDL "
             "3, Xbox pads through XInput as before, all of them together as the game's one controller, "
             "plugged in or out while it runs. XInput only: the first Xbox pad (XInput's user 0), as before.");
    }
    if (npads == 0)
        help(s->controller_input == PADS_SDL && !pads_sdl_version()[0] ? "Starting SDL..."
                                                                       : "No controller is connected.");
    for (i = 0; i < npads; i++) {
        const pads_info_t *p = &pads[i];
        if (p->vid || p->pid)
            snprintf(lbl, sizeof(lbl), "%s  (%s, %s, %04X:%04X)", p->name, p->type, p->via, p->vid, p->pid);
        else
            snprintf(lbl, sizeof(lbl), "%s  (%s, %s)", p->name, p->type, p->via);
        if (p->read)
            ImGui::TextUnformatted(lbl);
        else {
            ImGui::TextDisabled("%s", lbl);
            ImGui::SameLine();
            ImGui::TextDisabled("- not read: %s", p->why);
        }
        if (p->read)
            pad_view(p);
    }
    if (s->controller_input == PADS_SDL && pads_sdl_version()[0]) {
        snprintf(lbl, sizeof(lbl), "SDL %s", pads_sdl_version());
        help(lbl);
    }

    ImGui::SeparatorText("Controller");
    help("The game reads an Xbox controller. Click one of its inputs, then press the PC controller's button "
         "(or move a stick) for it; Esc cancels. A button another input had is swapped with it; an extra "
         "button (a paddle, a trackpad click) presses it as well. Start held about a second opens and closes "
         "this menu; a shorter press reaches the game when it is let go.");
    snprintf(lbl, sizeof(lbl), "PC controller (%s)", pads_style_name(style));
    if (ImGui::BeginTable("##pad", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Xbox controller", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(lbl, ImGuiTableColumnFlags_WidthFixed, fs * 11);
        ImGui::TableHeadersRow();
        for (i = 0; i < XIN_COUNT; i++) {
            int x = pad_order[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            input_name(controls_xin_label(x), controls_xin_hint(x));
            ImGui::TableNextColumn();
            snprintf(lbl, sizeof(lbl), "%s##pad%d",
                     g_cap == CAP_PAD && g_cap_idx == x ? (x >= XIN_BUTTONS ? "Move a stick..." : "Press a button...")
                                                       : pads_src_label(s->controls.pad[x], style), x);
            if (ImGui::Button(lbl, ImVec2(-FLT_MIN, 0))) {
                g_cap = CAP_PAD;
                g_cap_idx = x;
                controls_capture_begin();
                ImGui::GetIO().ClearInputKeys();
            }
        }
        ImGui::EndTable();
    }

    /* Step 70: the extra buttons the pads there have, and any bound. */
    {
        static const char *xb_names[XIN_BUTTONS + 1];
        int shown = 0, e;
        if (!xb_names[0]) {
            xb_names[0] = "Nothing";
            for (e = 0; e < XIN_BUTTONS; e++)
                xb_names[e + 1] = controls_xin_label(e);
        }
        for (e = XIN_COUNT; e < SRC_COUNT; e++) {
            int x = s->controls.extra[e - XIN_COUNT], sel = x < XIN_BUTTONS ? x + 1 : 0;
            if (!has[e])
                continue;
            if (!shown++) {
                ImGui::SeparatorText("Extra buttons");
                help("A pad's buttons past the Xbox layout, each pressing an Xbox button of its own.");
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(pads_src_label(e, style));
            ImGui::SameLine(fs * 13);
            ImGui::SetNextItemWidth(fs * 11);
            snprintf(lbl, sizeof(lbl), "##extra%d", e);
            if (ImGui::Combo(lbl, &sel, xb_names, XIN_BUTTONS + 1)) {
                controls_assign_extra(&s->controls, e, sel ? sel - 1 : XIN_NONE);
                r |= OVL_CHANGED;
            }
        }
    }
    if (ImGui::Button("Reset controller to defaults")) {
        controls_default_pad(&s->controls);
        r |= OVL_CHANGED;
    }
    more("The default is the Xbox layout by position on every pad: each button as itself on an Xbox pad, LB as "
         "Black, RB as White; Cross as A, Circle as B, Square as X, Triangle as Y on a PlayStation pad; the "
         "bottom button as A on a Switch pad. A Steam Controller's grips: L4 as B, R4 as X, L5 as Y, R5 as A.");

    ImGui::SeparatorText("Keyboard");
    {
        bool k = s->keyboard != 0;
        if (ImGui::Checkbox("Play with the keyboard too", &k)) {
            s->keyboard = k;
            r |= OVL_CHANGED;
        }
        more("These keys press the Xbox controller's inputs on top of the PC controller, while this window is in "
             "front; no controller is needed. Movement keys push the stick all the way. Keys do nothing while Alt "
             "is held (Alt+Enter, Alt+Tab).");
    }
    help("Click a key, then press the new one; Esc cancels. Right-click a key to clear it. F1 (this menu) and "
         "Esc can't be bound.");
    if (ImGui::BeginTable("##keys", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Xbox controller", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, fs * 6.5f);
        ImGui::TableSetupColumn("Second key", ImGuiTableColumnFlags_WidthFixed, fs * 6.5f);
        ImGui::TableHeadersRow();
        for (i = 0; i < KIN_COUNT; i++) {
            int k = key_order[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            input_name(controls_kin_label(k), k < XIN_BUTTONS ? controls_xin_hint(k) : k == KIN_LUP ? "Move" : "");
            for (slot = 0; slot < KIN_SLOTS; slot++) {
                int vk = s->controls.key[k][slot];
                ImGui::TableNextColumn();
                snprintf(lbl, sizeof(lbl), "%s##key%d.%d",
                         g_cap == CAP_KEY && g_cap_idx == k && g_cap_slot == slot ? "Press a key..."
                         : vk ? controls_key_label(vk) : "-", k, slot);
                if (ImGui::Button(lbl, ImVec2(-FLT_MIN, 0))) {
                    g_cap = CAP_KEY;
                    g_cap_idx = k;
                    g_cap_slot = slot;
                    controls_capture_begin();
                    ImGui::GetIO().ClearInputKeys();
                }
                if (vk && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    controls_assign_key(&s->controls, k, slot, 0);
                    r |= OVL_CHANGED;
                }
            }
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Reset keys to defaults")) {
        controls_default_keys(&s->controls);
        r |= OVL_CHANGED;
    }
    more("WASD and the arrows move; Space or K jumps, J or E spins, L or Left Shift crouches and slides (and "
         "body slams in the air), Enter or P pauses.");

    /* Step 104: photo mode's free camera and the freeze (the Extras tab turns them on) */
    ImGui::SeparatorText("Freecam controls");
    help("Photo mode's free camera and the freeze (Extras tab). The first two keys work any time; the rest only "
         "while photo mode is on, when the keyboard flies the camera and doesn't play the game (so they may be "
         "the game's own keys). The right mouse button held turns the camera, the mouse wheel changes the field "
         "of view. The controller drives the camera only while the game is frozen: left stick to move, right "
         "stick to look, and the buttons below. Click a key or button, then press the new one; Esc cancels; "
         "right-click clears.");
    if (g_fc_refused) {
        char w[160];
        snprintf(w, sizeof(w), "%s plays the game (Keyboard above): the two toggles need a key of their own.",
                 controls_key_label(g_fc_refused));
        amber(w);
    }
    if (ImGui::BeginTable("##fckeys", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Free camera", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, fs * 6.5f);
        ImGui::TableSetupColumn("Second key", ImGuiTableColumnFlags_WidthFixed, fs * 6.5f);
        ImGui::TableSetupColumn("Controller", ImGuiTableColumnFlags_WidthFixed, fs * 7.5f);
        ImGui::TableHeadersRow();
        for (i = 0; i < FC_COUNT; i++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            input_name(freecam_label(i), "");
            for (slot = 0; slot < KIN_SLOTS; slot++) {
                int vk = s->fc.key[i][slot];
                ImGui::TableNextColumn();
                snprintf(lbl, sizeof(lbl), "%s##fck%d.%d",
                         g_cap == CAP_FCKEY && g_cap_idx == i && g_cap_slot == slot ? "Press a key..."
                         : vk ? controls_key_label(vk) : "-", i, slot);
                if (ImGui::Button(lbl, ImVec2(-FLT_MIN, 0))) {
                    g_cap = CAP_FCKEY;
                    g_cap_idx = i;
                    g_cap_slot = slot;
                    controls_capture_begin();
                    ImGui::GetIO().ClearInputKeys();
                }
                if (vk && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    freecam_assign_key(&s->fc, NULL, i, slot, 0);
                    r |= OVL_CHANGED;
                }
            }
            ImGui::TableNextColumn();
            if (s->fc.pad[i] == FC_PAD_STICK) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s", i == FC_FWD || i == FC_BACK || i == FC_LEFT || i == FC_RIGHT ? "Left stick"
                                                                                                      : "Right stick");
                continue;
            }
            snprintf(lbl, sizeof(lbl), "%s##fcp%d",
                     g_cap == CAP_FCPAD && g_cap_idx == i ? "Press a button..."
                     : s->fc.pad[i] < XIN_BUTTONS ? pads_src_label(s->fc.pad[i], style) : "-", i);
            if (ImGui::Button(lbl, ImVec2(-FLT_MIN, 0))) {
                g_cap = CAP_FCPAD;
                g_cap_idx = i;
                controls_capture_begin();
                ImGui::GetIO().ClearInputKeys();
            }
            if (s->fc.pad[i] < XIN_BUTTONS && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                freecam_assign_pad(&s->fc, i, XIN_NONE);
                r |= OVL_CHANGED;
            }
        }
        ImGui::EndTable();
    }
    {
        int v = s->fc.speed;
        ImGui::SetNextItemWidth(fs * 12);
        if (ImGui::SliderInt("Camera speed", &v, 1, 100)) {
            s->fc.speed = v;
            r |= OVL_CHANGED;
        }
        more("How fast the free camera flies (Faster held: four times, Slower held: a quarter).");
        v = s->fc.look;
        ImGui::SetNextItemWidth(fs * 12);
        if (ImGui::SliderInt("Look sensitivity", &v, 1, 100)) {
            s->fc.look = v;
            r |= OVL_CHANGED;
        }
        more("How fast it turns: with the mouse, the look keys and the right stick.");
        v = s->fc.fov ? s->fc.fov : FC_FOV_MIN - 1;
        ImGui::SetNextItemWidth(fs * 12);
        if (ImGui::SliderInt("Field of view", &v, FC_FOV_MIN - 1, FC_FOV_MAX, v < FC_FOV_MIN ? "The game's" : "%d deg")) {
            s->fc.fov = v < FC_FOV_MIN ? 0 : v;
            r |= OVL_CHANGED;
        }
        more("Up and down the picture, in degrees; the far left is the game's own. The field-of-view keys and the "
             "mouse wheel change it while flying; the camera's reset puts it back here.");
        bool inv = s->fc.invert != 0;
        if (ImGui::Checkbox("Invert look up / down", &inv)) {
            s->fc.invert = inv;
            r |= OVL_CHANGED;
        }
    }
    if (ImGui::Button("Reset freecam controls to defaults")) {
        freecam_defaults(&s->fc);
        g_fc_refused = 0;
        r |= OVL_CHANGED;
    }
    more("F2 freezes and lets go, F3 turns photo mode on and off. W A S D move, E or Space up, Q or C down, the "
         "arrows (or J L I K) look, Left Shift held faster, Left Ctrl held slower, Z or Num - a wider view, X or "
         "Num + a narrower one, R or Home the game's camera again. Controller (frozen): RT up, LT down, RB faster, "
         "LB slower, D-pad down / up the field of view, Y the game's camera. Speed 40, sensitivity 50, the "
         "game's field of view, not inverted.");
    return r;
}

/* Step 82: the game runs slow under this many updates a second (its own
 * rate is 59.94). */
#define GAME_SLOW_UPS 57.0

extern "C" void ovl_rates_text(int ok, double game_ups, double shown_fps, int brief, char *buf, size_t cap)
{
    int slow = game_ups < GAME_SLOW_UPS;

    if (brief)                          /* the corner counter */
        snprintf(buf, cap, ok ? "Game %.0f/s%s\nShown %.0f/s" : "Game -/s\nShown -/s", game_ups,
                 slow ? " (slow)" : "", shown_fps);
    else if (!ok)
        snprintf(buf, cap, "Measured: not yet (the game hasn't run a second since).");
    else
        snprintf(buf, cap, "Measured: the game %.0f updates a second%s; %.0f frames shown a second.", game_ups,
                 slow ? ": slow" : " (full speed)", shown_fps);
}

/* Step 86: the startup message, centred near the bottom of the picture (pic: x, y, w, h in the target, then
 * the message's own scale, by the picture's height), its lines at `alpha`; made smaller if a line would
 * leave the picture's middle 4:3. `scale` is the frame's (the counter's). Step 104: at the top of the picture
 * instead, below the corner counter's box (`avoid`: x0, y0, x1, y1; NULL none) where the two would meet. */
static void startup_lines(const float *pic, float scale, const char *msg, float alpha, const float *avoid)
{
    const char *nl = strchr(msg, '\n');
    char a[128], b[128];
    float room = (pic[2] < pic[3] * 4.0f / 3.0f ? pic[2] : pic[3] * 4.0f / 3.0f) * 0.92f;
    float fs = pic[4] / scale, wa, wb, wide, padx, pady;

    snprintf(a, sizeof(a), "%.*s", nl ? (int)(nl - msg) : (int)strlen(msg), msg);
    snprintf(b, sizeof(b), "%s", nl ? nl + 1 : "");
    wa = ImGui::CalcTextSize(a).x;      /* at the frame's scale */
    wb = b[0] ? ImGui::CalcTextSize(b).x : 0.0f;
    wide = (wa > wb ? wa : wb) * fs + 2.0f * 12.0f * pic[4];
    if (wide > room && wide > 0)
        fs *= room / wide;
    padx = 12.0f * scale * fs;
    pady = 6.0f * scale * fs;
    {   /* step 104: the top of the picture; the box's size as ImGui will make it, to keep off the counter */
        float bw = (wa > wb ? wa : wb) * fs + 2.0f * padx;
        float x0 = pic[0] + pic[2] * 0.5f - bw * 0.5f, y = pic[1] + pic[3] * 0.015f;
        if (avoid && x0 < avoid[2] && x0 + bw > avoid[0] && y < avoid[3])
            y = avoid[3] + pic[3] * 0.01f;
        ImGui::SetNextWindowPos(ImVec2(pic[0] + pic[2] * 0.5f, y), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    }
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padx, pady));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * scale * fs, 3.0f * scale * fs));
    ImGui::Begin("##startup", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav |
                                        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(fs);
    {
        float inner = (wa > wb ? wa : wb) * fs;
        ImGui::SetCursorPosX(padx + (inner - wa * fs) * 0.5f);
        ImGui::TextUnformatted(a);
        if (b[0]) {
            ImGui::SetCursorPosX(padx + (inner - wb * fs) * 0.5f);
            ImGui::TextUnformatted(b);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

extern "C" void ovl_counter(ID3D11RenderTargetView *rtv, unsigned w, unsigned h, float scale, const char *lines,
                            const char *msg, float msg_alpha, const float *pic)
{
    ID3D11DeviceContext *ctx = NULL;
    ID3D11RenderTargetView *old_rtv = NULL;
    ID3D11DepthStencilView *old_dsv = NULL;

    if (!g_ready)
        return;
    EnterCriticalSection(&g_cs);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);
    io.DeltaTime = 1.0f / 60.0f;
    if (scale != g_scale) {             /* the overlay's own scale is set again when it opens (ovl_frame) */
        ImGuiStyle &st = ImGui::GetStyle();
        st = g_style_base;
        st.ScaleAllSizes(scale);
        st.FontScaleDpi = scale;
        g_scale = scale;
    }
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    float box[4], *avoid = NULL;        /* step 104: the counter's box, kept clear by the startup message */
    if (lines) {                        /* step 86: NULL with only the startup message to draw */
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.01f, io.DisplaySize.y * 0.01f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.55f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * scale, 5.0f * scale));    /* a small box */
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * scale, 2.0f * scale));
        ImGui::Begin("##fps", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav |
                                        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted(lines);
        {
            ImVec2 p = ImGui::GetWindowPos(), z = ImGui::GetWindowSize();
            box[0] = p.x;
            box[1] = p.y;
            box[2] = p.x + z.x;
            box[3] = p.y + z.y;
            avoid = box;
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
    if (msg && msg_alpha > 0.0f && pic)
        startup_lines(pic, scale, msg, msg_alpha, avoid);
    ImGui::Render();
    {
        ID3D11Device *dev = NULL;
        rtv->GetDevice(&dev);
        dev->GetImmediateContext(&ctx);
        dev->Release();
    }
    ctx->OMGetRenderTargets(1, &old_rtv, &old_dsv);
    ctx->OMSetRenderTargets(1, &rtv, NULL);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    ctx->OMSetRenderTargets(1, &old_rtv, old_dsv);
    if (old_rtv)
        old_rtv->Release();
    if (old_dsv)
        old_dsv->Release();
    ctx->Release();
    LeaveCriticalSection(&g_cs);
}

static int tab_display(twoc_settings_t *s, const ovl_info_t *in)
{
    static const char *const pacing[TWOC_PACING_COUNT] = { "Auto", "Display (vsync)", "Limiter (59.94 Hz)",
                                                            "Limiter + vsync" };
    int r = 0, i;
    char buf[64], line[160];

    ImGui::SeparatorText("Display");
    {   /* step 97: the graphics card the game draws on, from the next start (gpu.h) */
        int n = twoc_gpu_count(), found = s->gpu[0] ? twoc_gpu_find(s->gpu) : -1;
        char lbl[192], cur[192];
        if (!s->gpu[0])
            snprintf(cur, sizeof(cur), "Automatic");
        else if (found >= 0)
            twoc_gpu_label(twoc_gpu_at(found), cur, sizeof(cur));
        else
            snprintf(cur, sizeof(cur), "%s (not on this PC)", s->gpu);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20);
        if (ImGui::BeginCombo("Graphics card", cur)) {
            if (ImGui::Selectable("Automatic", !s->gpu[0]) && s->gpu[0]) {
                s->gpu[0] = 0;
                r |= OVL_CHANGED;
            }
            if (s->gpu[0] && found < 0)     /* the file's card, not here: listed so it shows chosen */
                ImGui::Selectable(cur, true);
            for (i = 0; i < n; i++) {
                const twoc_gpu_t *g = twoc_gpu_at(i);
                twoc_gpu_label(g, lbl, sizeof(lbl));
                snprintf(lbl + strlen(lbl), sizeof(lbl) - strlen(lbl), "##gpu%d", i);
                if (ImGui::Selectable(lbl, found == i) && found != i) {
                    snprintf(s->gpu, sizeof(s->gpu), "%s", g->name);
                    r |= OVL_CHANGED;
                }
            }
            ImGui::EndCombo();
        }
        {
            static char gh[640];        /* step 109: the exe's real name (ProjectWrath.exe; Windows keys it by path) */
            snprintf(gh, sizeof(gh), "Which graphics card draws the game. Automatic: the one Windows picks for it "
                     "(its own Graphics setting for %s, under Settings > System > Display > Graphics, else the "
                     "driver's choice), and the game asks for the high-performance card, so a laptop's NVIDIA or AMD "
                     "GPU rather than its Intel one. A card chosen here is used whatever Windows says; it is "
                     "remembered by its name. On a laptop the screen may hang off another card than the one "
                     "drawing: Windows copies each frame across. From the next start.", twoc_exe_name());
            more(gh);
        }
        snprintf(line, sizeof(line), "Now: %s.", twoc_gpu_in_use()[0] ? twoc_gpu_in_use() : "not known");
        if (_stricmp(s->gpu, twoc_gpu_asked()) != 0)
            snprintf(line + strlen(line), sizeof(line) - strlen(line), " %s from the next start.",
                     s->gpu[0] ? s->gpu : "Automatic");
        help(line);
        if ((twoc_gpu_note()[0] && _stricmp(s->gpu, twoc_gpu_asked()) == 0) || g_measure)
            amber(twoc_gpu_note()[0] ? twoc_gpu_note() : "The graphics card chosen before isn't on this PC now: "
                  "Automatic is used.");
    }
    {
        bool f = s->fullscreen != 0;
        if (ImGui::Checkbox("Fullscreen (borderless)    Alt+Enter", &f)) {
            s->fullscreen = f;
            r |= OVL_CHANGED;
        }
    }
    {   /* step 64: the aspect ratio; 4:3 is widescreen off, any other is widescreen = W:H */
        static const double ratios[][2] = { { 4, 3 }, { 16, 10 }, { 16, 9 }, { 21, 9 } };
        static const char *const ratio_names[] = { "4:3 (the game's own)", "16:10", "16:9", "21:9" };
        int cur = -1, n = (int)(sizeof(ratios) / sizeof(ratios[0]));
        int wide = s->widescreen && s->wide_h > 0 && s->wide_w / s->wide_h > 4.0 / 3.0 + 1e-6;
        for (i = 0; i < n; i++)
            if (!wide ? i == 0 : s->wide_w * ratios[i][1] == s->wide_h * ratios[i][0])
                cur = i;
        snprintf(buf, sizeof(buf), "%g:%g", s->wide_w, s->wide_h);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
        if (ImGui::BeginCombo("Aspect ratio", cur >= 0 ? ratio_names[cur] : buf)) {
            if (cur < 0)                /* another ratio, from the command line or the file */
                ImGui::Selectable(buf, true);
            for (i = 0; i < n; i++)
                if (ImGui::Selectable(ratio_names[i], i == cur) && i != cur) {
                    s->widescreen = i != 0;
                    s->wide_w = i ? ratios[i][0] : 16.0;
                    s->wide_h = i ? ratios[i][1] : 9.0;
                    r |= OVL_CHANGED;
                }
            ImGui::EndCombo();
        }
        more("The picture's shape. 4:3 is the game's own; the wider ones show more of the world at the sides, "
             "while the HUD, menus and text keep their 4:3 shape in the middle. A picture of another shape than "
             "the monitor gets black bars. Applied at once.");
    }
    {   /* step 65: the HUD at the screen edges */
        static const char *const hud_names[2] = { "Centre (4:3)", "Screen edges" };
        int wide = s->widescreen && s->wide_h > 0 && s->wide_w / s->wide_h > 4.0 / 3.0 + 1e-6;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
        if (ImGui::Combo("HUD position", &s->hud, hud_names, 2))
            r |= OVL_CHANGED;
        more("Screen edges: in a picture wider than 4:3, the counters on the left (wumpa fruit, health) move to "
             "the left edge and those on the right (lives, the time trial clock) to the right edge, each keeping "
             "its shape and its distance from the edge. Title cards, the pause menu and anything centred stay "
             "where they are. Applied at once.");
        if (s->hud && (!wide || g_measure))
            help("Now: as the game draws it (the picture is 4:3).");
    }
    {   /* step 91: the render size by aspect ratio, a custom one, and what it draws */
        static const ImVec4 mark(1.0f, 0.8f, 0.3f, 1.0f);
        static bool custom;
        static int cw = 1920, ch = 1080;
        static char seen[32] = "\x01";
        char lbl[64], an[24];
        float fs = ImGui::GetFontSize();
        double wide = s->widescreen && s->wide_h > 0 && s->wide_w / s->wide_h > 4.0 / 3.0 + 1e-6
                          ? s->wide_w / s->wide_h : 0;
        int asp = -1, g, listed = size_listed(s->render_size), sg = listed;
        int is_game = !s->render_size[0], is_native = _stricmp(s->render_size, "native") == 0;
        unsigned bw = 0, bh = 0, pw, ph, dw, dh;
        unsigned gw = in->guest_w ? in->guest_w : 640, gh = in->guest_h ? in->guest_h : 480;

        for (g = 0; g < N_SIZE_GROUPS; g++)    /* the chosen aspect's group, as the Aspect ratio list tells it */
            if (!wide ? g == 0 : s->wide_w * g_size_groups[g].h == s->wide_h * g_size_groups[g].w)
                asp = g;
        if (asp >= 0)
            snprintf(an, sizeof(an), "%s", g_size_groups[asp].name);
        else
            snprintf(an, sizeof(an), "%g:%g", s->wide_w, s->wide_h);
        if (is_native)
            native_size(&bw, &bh);
        else if (!is_game)
            twoc_render_size_parse(s->render_size, &bw, &bh);
        if (sg < 0 && bw)
            sg = size_aspect(bw, bh);
        twoc_render_fit(bw, bh, gw, gh, wide, &pw, &ph);
        twoc_render_scaled(pw, ph, s->render_scale, &dw, &dh);
        if (strcmp(seen, s->render_size) != 0) {    /* a new size (or the first frame): the custom fields follow */
            snprintf(seen, sizeof(seen), "%s", s->render_size);
            custom = !is_game && !is_native && listed < 0;
            if (custom && bw) {
                cw = (int)bw;
                ch = (int)bh;
            }
        }

        ImGui::SetNextItemWidth(fs * 12);
        {   /* the whole list at once where the picture is tall enough (ImGui's default shows 8 rows) */
            float maxh = ImGui::GetFrameHeightWithSpacing() * 27, room = ImGui::GetIO().DisplaySize.y * 0.9f;
            ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, maxh < room ? maxh : room));
        }
        if (ImGui::BeginCombo("Render size", size_label(s->render_size, lbl, sizeof(lbl)))) {
            if (ImGui::Selectable("Game (480 lines)", is_game) && !is_game) {
                s->render_size[0] = 0;
                r |= OVL_CHANGED;
            }
            if (ImGui::Selectable(size_label("native", lbl, sizeof(lbl)), is_native) && !is_native) {
                snprintf(s->render_size, sizeof(s->render_size), "native");
                r |= OVL_CHANGED;
            }
            for (g = 0; g < N_SIZE_GROUPS; g++) {
                snprintf(lbl, sizeof(lbl), g == asp ? "%s  - the aspect ratio chosen" : "%s", g_size_groups[g].name);
                if (g == asp)
                    ImGui::PushStyleColor(ImGuiCol_Text, mark);
                ImGui::SeparatorText(lbl);
                if (g == asp)
                    ImGui::PopStyleColor();
                for (i = 0; i < g_size_groups[g].n; i++) {
                    const char *v = g_size_groups[g].sizes[i];
                    bool sel = _stricmp(v, s->render_size) == 0;
                    if (ImGui::Selectable(v, sel) && !sel) {
                        snprintf(s->render_size, sizeof(s->render_size), "%s", v);
                        r |= OVL_CHANGED;
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::Selectable("Custom...", custom && listed < 0) && !custom) {
                custom = true;
                cw = (int)pw;           /* from the picture drawn now */
                ch = (int)ph;
            }
            ImGui::EndCombo();
        }
        more("The box the game is drawn in; the game itself draws 640x480. The picture is the largest of the "
             "chosen aspect ratio that fits in it, so a size of another shape gets bars (the sizes are grouped "
             "by shape, the chosen one marked). In a window the window takes that size in real pixels; "
             "fullscreen shows it over the whole screen, keeping its shape. A window opened without a render "
             "size is scaled by Windows until the next start. Applied at once.");
        if (custom || g_measure) {      /* step 97: measured with the fields */
            ImGui::SetNextItemWidth(fs * 6.5f);
            if (ImGui::InputInt("##cw", &cw, 16, 160))
                cw = cw < TWOC_RENDER_SIZE_MIN ? TWOC_RENDER_SIZE_MIN : cw > TWOC_RENDER_SIZE_MAX ? TWOC_RENDER_SIZE_MAX : cw;
            ImGui::SameLine();
            ImGui::TextUnformatted("x");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fs * 6.5f);
            if (ImGui::InputInt("##ch", &ch, 16, 160))
                ch = ch < TWOC_RENDER_SIZE_MIN ? TWOC_RENDER_SIZE_MIN : ch > TWOC_RENDER_SIZE_MAX ? TWOC_RENDER_SIZE_MAX : ch;
            ImGui::SameLine();
            snprintf(lbl, sizeof(lbl), "%dx%d", cw, ch);
            ImGui::BeginDisabled(_stricmp(lbl, s->render_size) == 0);
            if (ImGui::Button("Apply##size")) {
                snprintf(s->render_size, sizeof(s->render_size), "%s", lbl);
                r |= OVL_CHANGED;
            }
            ImGui::EndDisabled();
            more("Any width and height from 64 to 8192 pixels: type them (or use - and +), then Apply.");
        }
        if (dw == pw && dh == ph)
            snprintf(line, sizeof(line), "Renders %ux%u at %s.", pw, ph, an);
        else
            snprintf(line, sizeof(line), "Renders %ux%u at %s: render scale %d%% of %ux%u, shown at %ux%u.", dw, dh,
                     an, s->render_scale, pw, ph, pw, ph);
        help(line);
        if (sg >= 0 && sg != asp)
            snprintf(line, sizeof(line), "%ux%u is a %s size: at %s the picture fits inside it with bars "
                     "(Aspect ratio %s fills it).", bw, bh, g_size_groups[sg].name, an, g_size_groups[sg].name);
        else if (bw && (bw > pw + 2 || bh > ph + 2))
            snprintf(line, sizeof(line), "The picture fits inside %ux%u, with thin bars.", bw, bh);
        else
            line[0] = 0;
        if (line[0])
            help(line);
        if (in->view_w && (in->view_w != pw || in->view_h != ph))
            snprintf(line, sizeof(line), "Now %ux%u.", in->view_w, in->view_h);
        else
            line[0] = 0;
        if (line[0])
            help(line);
    }
    {   /* step 90: the render scale, a share of the render size */
        static const int sc[] = { 25, 50, 67, 75, 85, 100, 125, 150, 175, 200 };
        int n = (int)(sizeof(sc) / sizeof(sc[0])), cur = -1;
        char lbl[32];
        for (i = 0; i < n; i++)
            if (sc[i] == s->render_scale)
                cur = i;
        snprintf(buf, sizeof(buf), "%d%%%s", s->render_scale, s->render_scale == 100 ? " (the render size)" : "");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
        if (ImGui::BeginCombo("Render scale", buf)) {
            if (cur < 0)                /* another percentage, from the command line or the file */
                ImGui::Selectable(buf, true);
            for (i = 0; i < n; i++) {
                snprintf(lbl, sizeof(lbl), "%d%%%s", sc[i], sc[i] == 100 ? " (the render size)" : "");
                if (ImGui::Selectable(lbl, i == cur) && i != cur) {
                    s->render_scale = sc[i];
                    r |= OVL_CHANGED;
                }
            }
            ImGui::EndCombo();
        }
        more("Draws the picture at this share of the render size and shows it at the render size. Under 100% "
             "the graphics card has less to draw (a handheld on battery: softer, cooler); over 100% it draws "
             "more pixels than are shown and scales them down (supersampling: smoother edges, at a cost). Works "
             "with anti-aliasing and the frame rate. Applied at once.");
        if (in->view_w && (in->view_w != in->host_w || in->view_h != in->host_h))
            snprintf(line, sizeof(line), "Now: drawn at %ux%u, shown at %ux%u.", in->host_w, in->host_h,
                     in->view_w, in->view_h);
        else
            snprintf(line, sizeof(line), "Now: drawn at the render size, %ux%u.", in->host_w, in->host_h);
        help(line);
    }

    ImGui::SeparatorText("Frame pacing");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    if (ImGui::Combo("Pacing", &s->pacing, pacing, TWOC_PACING_COUNT))
        r |= OVL_CHANGED;
    more("Auto: the display's vsync if its rate is a multiple of 59.94 Hz, else the limiter. The game runs one "
         "update a frame, so under vsync a display that isn't a multiple runs it at the wrong speed. The limiter "
         "keeps 59.94 frames a second on any display; with vsync each frame also waits for a refresh.");
    if (in->executor_paces)
        snprintf(line, sizeof(line), "The NV2A executor paces this run (-Bridge).");
    else if (in->interp_note && in->interp_note[0] && in->frame_hz > 0)     /* step 77: the frame-rate plan */
        snprintf(line, sizeof(line), "Now: limiter %.2f fps for the frame rate below (variable refresh); the game "
                 "59.94 fps.", in->frame_hz);
    else if (in->interp_note && in->interp_note[0])
        snprintf(line, sizeof(line), "Now: display %.2f Hz vsync for the frame rate below; the game 59.94 fps.",
                 in->display_hz);
    else if (in->interval && in->frame_hz <= 0)
        snprintf(line, sizeof(line), "Now: display %.2f Hz / %u = %.2f fps.", in->display_hz, in->interval,
                 in->interval ? in->display_hz / in->interval : 0.0);
    else
        snprintf(line, sizeof(line), "Now: limiter %.2f fps%s.", in->frame_hz, in->interval ? ", on vsync" : "");
    help(line);

    {   /* step 61 */
        /* step 77: the game's, the display's, or a rate (the file's own other N listed too) */
        /* step 93: 72, half of 144 Hz (the Legion Go's even rate) */
        static const int fr_vals[9] = { 0, 1, 72, 90, 120, 144, 165, 180, 240 };
        static const char *const fr_names[9] = { "Game (60)", "Match display", "72 fps", "90 fps", "120 fps",
                                                 "144 fps", "165 fps", "180 fps", "240 fps" };
        const char *names[10];
        char other[24];
        int n = 9, cur = -1;
        for (i = 0; i < 9; i++) {
            names[i] = fr_names[i];
            if (fr_vals[i] == s->framerate)
                cur = i;
        }
        if (cur < 0) {
            snprintf(other, sizeof(other), "%d fps", s->framerate);
            names[n] = other;
            cur = n++;
        }
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
        if (ImGui::Combo("Frame rate", &cur, names, n, n) && cur < 9 && fr_vals[cur] != s->framerate) {
            s->framerate = fr_vals[cur];
            r |= OVL_CHANGED;
        }
        more("Above Game (60) the game still runs 59.94 updates a second, and each of its frames is drawn again "
             "in between with the camera and objects moved partway from the frame before, so turning and "
             "following Crash look smooth. A camera cut shows at once. Each frame shown is drawn at its own "
             "moment between two game frames, so motion is even at any rate the display shows evenly: its "
             "refresh rate (Match display), a whole fraction of it (120 on 240 Hz: each frame held 2 "
             "refreshes), or any rate with variable refresh (G-SYNC, FreeSync). On another rate it falls back "
             "to the highest even one below (90 on 240 Hz: 80); the line below says which. Needs vsync pacing "
             "(auto or vsync). Applied at once.");
        if (!s->framerate)
            snprintf(line, sizeof(line), "Now: the game's 59.94 fps.");
        else if (in->interp_on && in->interp_held)  /* step 96 */
            snprintf(line, sizeof(line), "Now: 59.94 fps, each game frame shown as it is: %s.", in->interp_held);
        else if (in->interp_on && in->interp_now > 0 && in->interp_now < in->interp_fps - 0.01)
            snprintf(line, sizeof(line), "Now: %.2f fps of %.2f (fewer while game frames run late).",
                     in->interp_now, in->interp_fps);
        else if (in->interp_on)
            snprintf(line, sizeof(line), "Now: %.2f fps, %.3g frames each game frame.", in->interp_fps,
                     in->interp_per);
        else if (in->interp_fps > 0)
            snprintf(line, sizeof(line), "Now: %.2f fps from the next frame.", in->interp_fps);
        else
            snprintf(line, sizeof(line), "Now: 59.94 fps, one frame each game frame: %s.",
                     in->interp_why ? in->interp_why : "this pacing");
        help(line);
        if (s->framerate == 72)         /* step 93: what 72 does on the displays it is for and against */
            help("72 fps: on a 144 Hz display (the Legion Go) even, each frame held 2 refreshes; on a 240 Hz or "
                 "180 Hz display 60 fps instead (each frame held 4 or 3 refreshes), unless it has variable "
                 "refresh (G-SYNC, FreeSync), which shows 72 evenly.");
        if (s->framerate && in->interp_note && in->interp_note[0])
            help(in->interp_note);      /* step 77: even or not on this display, and why */
        ovl_rates_text(in->meas_ok, in->game_ups, in->shown_fps, 0, line, sizeof(line));
        if (!in->running && in->meas_ok && strlen(line) > 1)    /* step 82: held, the rates from before it opened */
            snprintf(line + strlen(line) - 1, sizeof(line) - strlen(line) + 1, ", before this menu opened.");
        help(line);                     /* step 82: what is measured, beside the plan above */
        {   /* step 93: Off / Simple / Detailed */
            static const char *const sf_names[3] = { "Off", "Simple", "Detailed" };
            int cur = s->show_fps >= 0 && s->show_fps <= 2 ? s->show_fps : 1;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
            if (ImGui::Combo("Show frame rate", &cur, sf_names, 3) && cur != s->show_fps) {
                s->show_fps = cur;
                r |= OVL_CHANGED;
            }
            more("In the picture's top left corner while this menu is closed. Simple: the game's updates a second "
                 "(59.94 is its full speed; under it the game itself runs slow) and the frames shown a second "
                 "(with a frame rate above Game (60), the game's frames and those drawn in between). Detailed "
                 "adds the game thread's and the render thread's milliseconds a game frame and the adaptive "
                 "frame rate's step (both with a frame rate above Game (60)), the processor's speed, the battery's "
                 "watts or \"Plugged in\", Windows' power mode and a temperature if Windows has one (from the "
                 "power log, read once a second). Applied at once.");
        }
        if (s->framerate) {             /* step 75 */
            static const char *const ad_names[2] = { "On", "Off" };
            int cur = s->adaptive ? 0 : 1;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
            if (ImGui::Combo("Adaptive frame rate", &cur, ad_names, 2)) {
                s->adaptive = cur == 0;
                r |= OVL_CHANGED;
            }
            more("On: where a frame is too heavy to draw at every refresh, fewer frames are drawn in between "
                 "(half, or one) until there is room again, so the game keeps its 59.94 updates a second. "
                 "Off: always a frame at every refresh; in the heaviest places the game itself then runs "
                 "slower than it should. Applied at once.");
        }
        if (s->framerate) {             /* step 79 */
            static const char *const rt_names[3] = { "Auto", "On", "Off" };
            static const int rt_vals[3] = { TWOC_RT_AUTO, TWOC_RT_ON, TWOC_RT_OFF };
            int cur = 2;
            for (i = 0; i < 3; i++)
                if (rt_vals[i] == s->render_thread)
                    cur = i;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
            if (ImGui::Combo("Render thread", &cur, rt_names, 3) && rt_vals[cur] != s->render_thread) {
                s->render_thread = rt_vals[cur];
                r |= OVL_CHANGED;
            }
            more("On: the frames shown between game frames are drawn on a thread of their own, beside the game's, "
                 "so heavy places keep the full frame rate (one thread had to do both). Auto: on when this PC "
                 "has enough processor cores for the game's thread, this one and the emulated hardware's. Off: "
                 "everything on the game's thread, as before. From the next frame.");
            if (s->render_thread == TWOC_RT_OFF)
                snprintf(line, sizeof(line), "Now: off, the game's thread draws every frame.");
            else if (in->rt_on)
                snprintf(line, sizeof(line), "Now: on, beside the game's thread (this PC: %u cores).", in->rt_cores);
            else if (s->render_thread == TWOC_RT_AUTO && in->rt_cores < in->rt_auto_cores)
                snprintf(line, sizeof(line), "Now: off: Auto needs %u processor cores, this PC has %u.",
                         in->rt_auto_cores, in->rt_cores);
            else
                snprintf(line, sizeof(line), "Now: on from the next frame.");
            help(line);
        }
    }

    ImGui::SeparatorText("Menu");       /* step 104 */
    {
        int v = g_ms_drag >= 0 ? g_ms_drag : s->menu_scale, nv = v;
        bool changed;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
        changed = ImGui::SliderInt("Menu scale", &nv, TWOC_MENU_SCALE_MIN, TWOC_MENU_SCALE_MAX, "%d%%",
                                   ImGuiSliderFlags_AlwaysClamp);
        if (!g_measure) {
            bool mouse = ImGui::IsItemActive() && ImGui::GetIO().MouseDown[0];
            int d = 0;
            if (mouse) {                /* dragged: shown as it goes in whole steps of 5%, applied when let go
                                         * (the slider would move under the pointer at every step) */
                g_ms_drag = (nv + 2) / 5 * 5;
            } else if (g_ms_drag >= 0) {
                if (g_ms_drag != s->menu_scale) {
                    s->menu_scale = g_ms_drag;
                    r |= OVL_CHANGED;
                    g_ms_warp = 1;
                    g_scroll_focus = 8;
                }
                g_ms_drag = -1;
            } else if (changed && nv != v) {    /* the pad or the keys with the slider active (A): a step */
                d = nv > v ? MS_STEP : -MS_STEP;
            } else if (ImGui::IsItemFocused() && !ImGui::IsItemActive()) {
                /* focused: the D-pad or the arrows left and right move it too, no A needed */
                if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
                    d = -MS_STEP;
                if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, true) || ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
                    d = MS_STEP;
            }
            if (d) {                    /* to the next whole step that way */
                int m = d > 0 ? (v / MS_STEP + 1) * MS_STEP : (v - 1) / MS_STEP * MS_STEP;
                m = m < TWOC_MENU_SCALE_MIN ? TWOC_MENU_SCALE_MIN : m > TWOC_MENU_SCALE_MAX ? TWOC_MENU_SCALE_MAX : m;
                if (m != s->menu_scale) {
                    s->menu_scale = m;
                    r |= OVL_CHANGED;
                    g_scroll_focus = 8;
                }
            }
            if (g_scroll_focus && (ImGui::IsItemFocused() || g_ms_warp))
                ImGui::SetScrollHereY(0.5f);    /* step 104: the slider stays in view where the tab scrolls */
            {   /* test only (RECOMP_DEV_OVL_RECT): where the slider is, in the picture's pixels, when it moves */
                static ImVec2 seen;
                ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                if ((a.x != seen.x || a.y != seen.y) && getenv("RECOMP_DEV_OVL_RECT") && *getenv("RECOMP_DEV_OVL_RECT"))
                    fprintf(stderr, "[OVL] menu scale slider at %.0f,%.0f - %.0f,%.0f (%d%%)\n", a.x, a.y, b.x, b.y,
                            s->menu_scale);
                seen = a;
            }
            if (g_ms_warp == MS_WARP_READ) {  /* the new size and its scroll settled: the pointer onto it */
                ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                float f = (float)(s->menu_scale - TWOC_MENU_SCALE_MIN) / (TWOC_MENU_SCALE_MAX - TWOC_MENU_SCALE_MIN);
                g_ms_warp_at = ImVec2(a.x + (b.x - a.x) * (0.04f + 0.92f * f), (a.y + b.y) * 0.5f);
                g_ms_warp = MS_WARP_READ + 1;
            }
        }
        more("How big this menu's text and boxes are: 100% is its own size, which already follows the window's "
             "height and Windows' display scale. Smaller leaves more of the game in view; larger is easier to read "
             "from a distance. Over the window's size it is held to the most that fits. The frame-rate counter "
             "and the startup message keep their own size. D-pad or arrow keys left and right change it. Applied "
             "at once.");
        if (g_ms_limited || g_measure) {
            char line[96];
            snprintf(line, sizeof(line), "Limited by the window: showing %d%%.", g_ms_limited ? g_ms_shown : 100);
            help(line);
        }
    }

    ImGui::Spacing();                   /* step 93: everything above back to its default, after a question */
    ImGui::Separator();
    if (ImGui::Button("Reset display settings to defaults"))
        g_reset_req = 1;                /* step 104: asked in build() */
    if (g_scroll_focus && ImGui::IsItemFocused() && !g_measure)
        ImGui::SetScrollHereY(0.5f);    /* step 104: after the reset's new menu scale */
    more("Sets every setting on this tab and the Graphics tab back to what the game starts with when it has no "
         "settings file: Render size Game (480 lines), Render scale 100%, Pacing Auto, Frame rate Game (60), "
         "Adaptive On, Render thread Auto, Show frame rate Off, HUD in the centre, Menu scale 100%; Anisotropic "
         "filtering Off, "
         "Anti-aliasing Off, Ambient occlusion Off. Fullscreen, the aspect ratio, the graphics card, the Extras, "
         "sound, controls and saves stay as they are. Asks first.");
    return r;
}

/* Step 104: the Graphics tab: what was the Display tab's textures, anti-aliasing and ambient occlusion, as
 * they were (the same keys and defaults; the Display tab's reset covers them). */
static int tab_graphics(twoc_settings_t *s, const ovl_info_t *in)
{
    static const int aniso[] = { 1, 2, 4, 8, 16 };
    static const char *const aniso_names[] = { "Off (the game's own)", "2x", "4x", "8x", "16x" };
    int r = 0, i;
    char line[160];

    ImGui::SeparatorText("Textures");
    {
        int cur = 0;
        for (i = 0; i < 5; i++)
            if (aniso[i] == s->anisotropy)
                cur = i;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
        if (ImGui::Combo("Anisotropic filtering", &cur, aniso_names, 5)) {
            s->anisotropy = aniso[cur];
            r |= OVL_CHANGED;
        }
    }

    ImGui::SeparatorText("Anti-aliasing");      /* step 58 */
    {
        static const char *const aa_names[TWOC_AA_COUNT] = { "Off", "FXAA", "SMAA", "MSAA 2x", "MSAA 4x",
                                                             "MSAA 8x", "MSAA 2x + SMAA", "MSAA 4x + SMAA",
                                                             "MSAA 8x + SMAA" };
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
        if (ImGui::Combo("Anti-aliasing", &s->antialiasing, aa_names, TWOC_AA_COUNT))
            r |= OVL_CHANGED;
        more("Smooths jagged edges. FXAA and SMAA filter the finished picture: cheap, but the HUD and text are "
             "softened a little with the rest. MSAA draws the 3D with 2, 4 or 8 samples a pixel: sharper, and "
             "the HUD stays crisp, but it costs more, most at a large render size. MSAA + SMAA does both: SMAA "
             "also smooths the edges MSAA leaves inside see-through textures (fences, leaves). Applied at once.");
        if (twoc_aa_samples(in->aa) > 1 && in->ms_samples != twoc_aa_samples(in->aa))
            snprintf(line, sizeof(line), in->ms_samples ? "Now: MSAA %ux, the most this graphics card takes."
                                                        : "Now: off; this graphics card takes no MSAA here.",
                     in->ms_samples);
        else
            snprintf(line, sizeof(line), "Now: %s.", aa_names[in->aa >= 0 && in->aa < TWOC_AA_COUNT ? in->aa : 0]);
        help(line);
    }
    ImGui::SeparatorText("Ambient occlusion");  /* step 92 */
    {
        static const char *const ao_names[TWOC_AO_COUNT] = { "Off", "SSAO", "GTAO" };
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
        if (ImGui::Combo("Ambient occlusion", &s->ao, ao_names, TWOC_AO_COUNT))
            r |= OVL_CHANGED;
        more("Darkens corners, creases and the ground under and around things, as soft shadow from the light "
             "all around, worked out from the 3D picture's depth. It goes in before the game's see-through "
             "things, particles and HUD, and fades in fog. SSAO is the classic way; GTAO (Intel's XeGTAO) is "
             "closer to how light really behaves and costs a little more. Not part of the game's own look. "
             "Applied at once.");
        if (s->ao != TWOC_AO_OFF) {
            int v = s->ao_strength;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
            if (ImGui::SliderInt("Strength", &v, 0, 100, "%d%%")) {
                s->ao_strength = v;
                r |= OVL_CHANGED;
            }
            more("How much of the darkening is applied.");
            bool hf = s->ao_half != 0;
            if (ImGui::Checkbox("Half resolution", &hf)) {
                s->ao_half = hf;
                r |= OVL_CHANGED;
            }
            more("Works the occlusion out at half the picture's width and height, then fills it in by depth: "
                 "about a third of the cost, a little softer. For a handheld or a large render size.");
        }
        if (s->ao != TWOC_AO_OFF && in->ao == TWOC_AO_OFF) {
            help("Now: off; it couldn't be set up on this graphics card (see the log's [AO] lines).");
        }
    }
    return r;
}

static int tab_audio(twoc_settings_t *s)
{
    int r = 0;
    char line[160];

    ImGui::SeparatorText("Volume");
    {
        int v = s->volume < 0 ? 100 : s->volume;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
        if (ImGui::SliderInt("Volume", &v, 0, 100, "%d%%")) {
            s->volume = v;
            r |= OVL_CHANGED;
        }
        more("The game's level in Windows' volume mixer (Windows remembers it). It scales everything below.");
        if (s->volume < 0)
            help("Not set: Windows' own level.");
    }
    {
        int m = s->music < 0 ? 100 : s->music, e = s->effects < 0 ? 100 : s->effects;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
        if (ImGui::SliderInt("Music", &m, 0, 100, "%d%%")) {
            s->music = m;
            r |= OVL_CHANGED;
        }
        more("Music, ambience and speech: everything the game streams. 100% is the game's own mix.");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
        if (ImGui::SliderInt("Effects", &e, 0, 100, "%d%%")) {
            s->effects = e;
            r |= OVL_CHANGED;
        }
        more("The sound effects: everything the game plays from memory. 100% is the game's own mix.");
    }
    {
        bool h = s->hrtf != 0;
        if (ImGui::Checkbox("3D sound placement (HRTF)", &h)) {
            s->hrtf = h ? 1 : 0;
            r |= OVL_CHANGED;
        }
        more("The Xbox's HRTF filter on the game's 3D sounds: an enemy or a crate on Crash's left is heard on "
             "the left, as on the console. Off, 3D sounds are centred left-right (front-back still pans in "
             "5.1). Applies at once.");
    }
    {   /* step 102 */
        bool m = s->mute_drone != 0;
        if (ImGui::Checkbox("Mute Droid Void's background drone", &m)) {
            s->mute_drone = m;
            r |= OVL_CHANGED;
        }
        more("Droid Void plays a droning ambience loop under its music for the whole level, as on the Xbox. "
             "This silences that one sound and nothing else: the music, the sound effects, the speech and every "
             "other level stay as they are. From the next time the level starts (a level being played keeps "
             "the sound it started with).");
    }
    {
        static const char *const spk[] = { "Stereo", "5.1 surround" };
        int cur = s->speakers > 0, devch = 0, out = mcpx_apu_output_channels(&devch);
        const char *e = getenv("RECOMP_SPEAKERS");
        int at_start = e && strcmp(e, "5.1") == 0;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
        if (ImGui::Combo("Speakers", &cur, spk, 2)) {
            s->speakers = cur;
            r |= OVL_CHANGED;
        }
        more("5.1 sends the game's six speaker channels (front left/right, center, LFE, rear left/right) to a "
             "device that has those speakers, and tells the game it has surround speakers, as the Xbox "
             "dashboard did, so its 3D sounds pan to the rear. A device without them stays in stereo. From the "
             "next start.");
        if (out == 6)
            snprintf(line, sizeof(line), "Now: 5.1 (the device has %d channels).", devch);
        else if (at_start && devch > 0)
            snprintf(line, sizeof(line), "Now: stereo: the default device takes %d channel%s.", devch,
                     devch == 1 ? "" : "s");
        else
            snprintf(line, sizeof(line), "Now: stereo.");
        if (cur != at_start)
            snprintf(line + strlen(line), sizeof(line) - strlen(line), " %s from the next start.",
                     cur ? "5.1" : "Stereo");
        help(line);
    }
    return r;
}

/* Step 83: the save's backups (saves.c). The list is saves.c's, read again
 * on disk each time the tab opens. */
static char g_sv_sel[32];

static void tab_saves(void)
{
    static saves_entry_t list[64];
    saves_state_t st;
    int n, i, sel = -1;
    float fs = ImGui::GetFontSize();
    char lbl[96];

    if (!g_sv_shown && !g_measure) {    /* the tab just opened (step 97: not when measured) */
        saves_refresh();
        g_sv_shown = 1;
    }
    saves_state(&st);
    n = saves_list(list, (int)(sizeof(list) / sizeof(list[0])));
    for (i = 0; i < n; i++)
        if (strcmp(list[i].name, g_sv_sel) == 0)
            sel = i;
    if (sel < 0 && n) {                 /* nothing chosen (or it went): the newest */
        sel = 0;
        snprintf(g_sv_sel, sizeof(g_sv_sel), "%s", list[0].name);
    }

    ImGui::SeparatorText("Save backups");
    if (!st.on) {
        help("Backups are off for this run (RECOMP_SAVE_BACKUPS=0, or the backups folder couldn't be used: the "
             "log says why).");
        return;
    }
    help("Copies of the game's save are taken at each start and a few seconds after the game saves, when "
         "something changed. The newest 10 are kept, and the newest of each day for 14 days.");
    more(st.dir);

    /* No scrolling: a scrolling table is a window of its own, which the
     * controller's navigation passes over. There are at most about 24 rows
     * (the newest 10, then one a day for 14 days), and the panel shrinks to
     * fit the picture. */
    if (!n)
        help("No backups yet: the game has no save (save at the hub's Load/Save console), or none was taken.");
    else if (ImGui::BeginTable("##saves", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, fs * 6.2f);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, fs * 4.4f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, fs * 4.4f);
        ImGui::TableSetupColumn("Taken", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (i = 0; i < n; i++) {
            const saves_entry_t *e = &list[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            snprintf(lbl, sizeof(lbl), "%.10s##sv%d", e->when, i);
            if (ImGui::Selectable(lbl, i == sel, ImGuiSelectableFlags_SpanAllColumns)) {
                sel = i;
                snprintf(g_sv_sel, sizeof(g_sv_sel), "%s", e->name);
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e->when + 11);
            ImGui::TableNextColumn();
            if (e->size)
                ImGui::Text("%.1f KB", e->size / 1024.0);
            else
                ImGui::TextDisabled("no save");
            ImGui::TableNextColumn();
            if (strcmp(e->name, st.pending) == 0)
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s; restored at the next start", e->why);
            else
                ImGui::TextUnformatted(e->why);
        }
        ImGui::EndTable();
    }

    ImGui::BeginDisabled(st.busy != 0);
    if (ImGui::Button("Back up now"))
        saves_backup_now();
    more("Copies the save as it is now (when it differs from the newest backup).");
    ImGui::SameLine();
    ImGui::BeginDisabled(sel < 0 || !list[sel >= 0 ? sel : 0].size);
    if (ImGui::Button("Restore this one") && sel >= 0)
        saves_restore_at_next_start(list[sel].name);
    ImGui::EndDisabled();
    more("The game has its save loaded while it runs, so the chosen backup replaces the save at the next start: "
         "quit and start the game again, then load the game at the Load/Save console. The save in use now is "
         "backed up first and kept.");
    ImGui::SameLine();
    if (ImGui::Button("Open backups folder"))
        saves_open_folder();
    ImGui::EndDisabled();

    if (st.pending[0]) {
        snprintf(lbl, sizeof(lbl), "At the next start the save becomes the backup of %.10s %.2s:%.2s:%.2s.",
                 st.pending, st.pending + 11, st.pending + 14, st.pending + 17);
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", lbl);
        if (ImGui::Button("Cancel the restore"))
            saves_cancel_restore();
    }
    if (st.status[0])
        help(st.status);
}

/* Step 104: the Extras tab: the look and behaviour options that were on the Display tab and at the top of
 * the menu, as they were (the same keys and defaults), with a reset of their own. */
static int tab_extras(twoc_settings_t *s, const ovl_info_t *in)
{
    int r = 0;
    char line[160];

    ImGui::SeparatorText("Photo mode");     /* step 104 */
    {
        bool p = in->photo_on != 0, f = in->freeze_on != 0;
        char k1[24], k2[24];
        snprintf(k1, sizeof(k1), "%s", s->fc.key[FC_PHOTO][0] ? controls_key_label(s->fc.key[FC_PHOTO][0]) : "no key");
        snprintf(k2, sizeof(k2), "%s", s->fc.key[FC_FREEZE][0] ? controls_key_label(s->fc.key[FC_FREEZE][0]) : "no key");
        snprintf(line, sizeof(line), "Photo mode    %s", k1);
        if (ImGui::Checkbox(line, &p))
            r |= OVL_PHOTO;
        more("A free camera instead of the game's: fly anywhere around the scene while the game runs (Crash and "
             "the rest keep moving) or while it is frozen. The game's HUD, the counter and the startup message "
             "go. The keyboard flies the camera (W A S D, E / Q, the arrows; Freecam controls on the Controls "
             "tab) and stops playing the game; the controller still plays it, and flies the camera while the "
             "game is frozen. The right mouse button held turns it. The game only draws what is near its own "
             "view: wider than usual while photo mode is on, but not what is behind its camera, nor crates, fruit "
             "and enemies far from Crash. Reflections still show the game's view. Turns itself off at a level "
             "load, a loading screen, a respawn or a cutscene's cut. Never saved: off at every start.");
        snprintf(line, sizeof(line), "Freeze the game (pause)    %s", k2);
        if (ImGui::Checkbox(line, &f))
            r |= OVL_FREEZE;
        more("Holds the game where it is, silent, its last picture drawn again at the display's rate: with photo "
             "mode on, fly around the frozen scene. Works with photo mode off too (it simply holds the picture). "
             "This menu's own pause is separate.");
        if (!in->photo_on && in->photo_why && in->photo_why[0]) {
            snprintf(line, sizeof(line), "Photo mode went off: %s.", in->photo_why);
            help(line);
        }
    }

    ImGui::SeparatorText("Look");
    {   /* step 101: Crash's fur; step 102: three ways */
        static const char *const fur[] = { "On", "Off for Crash only", "Off for everyone" };
        static const int val[] = { TWOC_FUR_ON, TWOC_FUR_CRASH, TWOC_FUR_OFF };
        int cur = s->fur == TWOC_FUR_CRASH ? 1 : s->fur == TWOC_FUR_OFF ? 2 : 0;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
        if (ImGui::Combo("Character fur", &cur, fur, 3)) {
            s->fur = val[cur];
            r |= OVL_CHANGED;
        }
        more("The layer of fur the Xbox draws over its furry characters. On: the Xbox's look. Off for Crash only: "
             "Crash without it, as on the PlayStation 2; Coco, Crunch "
             "and the others keep theirs (Coco's green look in Crashteroids stays). Off for everyone: no fur on "
             "anyone. Reflections and shadows have none either way. Applied at once.");
    }
    {   /* step 102: the hub pads' crystals and gems as a level draws one collected */
        bool g = s->hub_seethrough != 0;
        if (ImGui::Checkbox("See-through gems and crystals in the hub", &g)) {
            s->hub_seethrough = g;
            r |= OVL_CHANGED;
        }
        more("On: the crystals and gems on the hub's pads drawn see-through, the way a level draws one you have "
             "already collected (glass that bends what is behind it, a gem in its colour over that). Off: solid, "
             "the game's own look. The relics are glass already and stay as they are; so do the outline icons of "
             "what is still to find. Applied at once.");
    }

    ImGui::SeparatorText("Menu and start");
    {   /* step 86 */
        bool m = s->startup_message != 0;
        if (ImGui::Checkbox("Show the startup message", &m)) {
            s->startup_message = m;
            r |= OVL_CHANGED;
        }
        more("\"Press F1 to open the settings menu\" (and, with a controller, \"or hold Start\") at the top "
             "of the picture for the first seconds of each start. Off also takes it away if it is showing.");
    }
    {   /* step 57; step 104: from the top of the menu */
        bool p = s->pause != 0;
        if (ImGui::Checkbox("Pause the game while this menu is open", &p)) {
            s->pause = p;
            r |= OVL_CHANGED;
        }
        more("Off: the game keeps running and drawing behind this menu, so a change to the picture shows at "
             "once, and its sound plays on. It gets no controller or keyboard input until the menu closes, and "
             "a button still held then counts only once it is let go. Applied at once.");
    }
    {   /* step 90 */
        bool p = s->pause_unfocused != 0;
        if (ImGui::Checkbox("Pause when in the background", &p)) {
            s->pause_unfocused = p;
            r |= OVL_CHANGED;
        }
        more("The game waits, silent, while its window is minimised or another window is in front, as it waits "
             "under this menu, and goes on when it is in front again. Not while it is starting or switching to "
             "fullscreen. Applied at once.");
    }
    {   /* step 90: -FastTitle's switch, from the next start */
        bool t = s->fast_title != 0;
        if (ImGui::Checkbox("Skip the title animation", &t)) {
            s->fast_title = t;
            r |= OVL_CHANGED;
        }
        more("At each start, after the logos, the game plays its title animation (Crash and the title "
             "music) before the main menu opens. On, the animation runs many times faster, so the main menu "
             "opens about " TWOC_FAST_TITLE_SAVES " sooner, and its sound track (Cortex's lines and the "
             "animation's sounds) is left out instead of playing on over the main menu; the logos, the title "
             "music and the menu are as before. From the next start (a launcher's -FastTitle wins over this).");
        snprintf(line, sizeof(line), "Now: %s this run.%s", in->fast_title_now ? "on" : "off",
                 !s->fast_title != !in->fast_title_now ? (s->fast_title ? " On from the next start."
                                                                         : " Off from the next start.") : "");
        help(line);
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Reset extras to defaults"))
        g_reset_req = 2;                /* asked in build() */
    more("Sets every setting on this tab back to its default: Photo mode off, Character fur On, See-through gems "
         "and crystals off, the startup message on, Pause the game while this menu is open on, Pause when in the "
         "background off, Skip the title animation off. The freeze is left as it is. The other tabs stay as "
         "they are. Asks first.");
    return r;
}

/* Step 93: what this build is and what it runs on (about.c). */
static void about_game_files(void);
static void about_report(const twoc_settings_t *s, const ovl_info_t *in);

static void tab_about(const twoc_settings_t *s, const ovl_info_t *in)
{
    static const char *const names[] = { "Build", "Link-time build", "SDL", "Graphics card", "Driver",
                                         "Chosen by", "Display's card", "Display", "Processor",
                                         "Power throttling", "Windows", "Game files" };
    const twoc_about_t *a = in->about;
    const char *vals[12];
    int i;

    about_game_files();                 /* step 107 */
    about_report(s, in);                /* step 109 */
    ImGui::SeparatorText(twoc_app_name());     /* step 109: Project Wrath (names.h) */
    help("An unofficial fan port. It is not affiliated with, endorsed by or sponsored by the owners of the game.");
    if (!a) {
        help("Not collected.");
        return;
    }
    vals[0] = a->build;
    vals[1] = a->ltcg;
    vals[2] = a->sdl;
    vals[3] = a->gpu;
    vals[4] = a->driver;
    vals[5] = a->gpu_choice;            /* step 97 */
    vals[6] = a->gpu_display;
    vals[7] = a->display;
    vals[8] = a->cpu;                   /* step 97 */
    vals[9] = a->cpu_power;
    vals[10] = a->windows;
    vals[11] = a->files;
    if (ImGui::BeginTable("##about", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("##k", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.5f);
        ImGui::TableSetupColumn("##v", ImGuiTableColumnFlags_WidthStretch);
        for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", names[i]);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", vals[i]);
        }
        ImGui::EndTable();
    }
    help("The same lines are in the log ([ABOUT]). Third-party parts and their licences: THIRD_PARTY_LICENSES.txt next to the game.");
}

/* Step 107 (notes 18.110): the About tab's first part, the game files: a full check, or another disc image through
 * the setup screen. First, with its buttons at the top, because the tab's information below scrolls and holds
 * nothing the controller can stop on: Down from the tabs reaches these buttons, not the menu's footer. */
static void about_game_files(void)
{
    {
        setup_status_t st;
        bool busy = twoc_setup_busy() != 0;
        twoc_setup_status(&st);
        ImGui::SeparatorText("Game files");
        ImGui::BeginDisabled(busy);
        if (ImGui::Button(busy ? "Checking..." : "Verify game files")) {
            fprintf(stderr, "[OVL] About: Verify game files\n");
            twoc_setup_verify();
        }
        ImGui::SameLine();
        if (ImGui::Button("Choose another disc image...")) {
            fprintf(stderr, "[OVL] About: Choose another disc image...\n");
            g_setup_req = 1;
        }
        ImGui::EndDisabled();
        more("Verify reads every game file and compares its size and SHA-256 with the disc's (about 1 GB; the game "
             "keeps running). Choose another disc image restarts the game into its setup screen; the current files "
             "stay until the new ones are copied and checked, and your saves are kept.");
        ImGui::TextWrapped("%s\\game_files%s", twoc_data_dir_utf8(), twoc_data_dir_fallback() ?
                           " (in your user folder: the game's own folder can't be written to)" : "");
        if (g_measure || (st.verify_only && st.state == SETUP_VERIFYING)) {
            char l[64];
            snprintf(l, sizeof(l), "%.0f of %.0f MB, %u of %u files", (double)st.bytes_done / 1048576.0,
                     (double)st.bytes_total / 1048576.0, st.files_done, st.files_total);
            ImGui::ProgressBar(st.bytes_total ? (float)((double)st.bytes_done / (double)st.bytes_total) : 0.0f,
                               ImVec2(-FLT_MIN, 0), l);
        }
        if (g_measure || (st.verify_only && (st.state == SETUP_DONE || st.state == SETUP_FAILED))) {
            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX());
            if (st.state == SETUP_FAILED)
                ImGui::TextColored(k_amber, "%s %s", st.why, st.hint);
            else
                ImGui::TextColored(k_green, "%s", g_measure ? "All 2267 game files are the disc's (every size and "
                                   "SHA-256), checked in 9.9 s." : st.note);
            ImGui::PopTextWrapPos();
        }
    }
}

/* Step 109 (notes 18.112): problem reports, under the game files (where the controller reaches them): Copy build
 * info (a text block for a report on the clipboard: no user name, PC name or folders, about.c), Open logs folder,
 * and Report a problem only when ISSUES_URL is set (twoc/ISSUES_URL.txt). The game sends nothing by itself. */
static void about_report(const twoc_settings_t *s, const ovl_info_t *in)
{
    static char note[200];
    const char *url = twoc_issues_url();

    ImGui::SeparatorText("Problems");
    ImGui::BeginDisabled(!in->about);
    if (ImGui::Button("Copy build info")) {
        char picture[320], fr[320], changed[2048], rate[32];
        int n;
        if (s->framerate == 0)
            snprintf(rate, sizeof(rate), "Game (60)");
        else if (s->framerate == 1)
            snprintf(rate, sizeof(rate), "Match display");
        else
            snprintf(rate, sizeof(rate), "%d fps", s->framerate);
        snprintf(picture, sizeof(picture), "%s; aspect ratio %s; render size %s (%u x %u shown, %u x %u drawn)%s",
                 s->fullscreen ? "full screen (borderless)" : "in a window", !s->widescreen ? "4:3" :
                 s->wide_w == 16.0 && s->wide_h == 9.0 ? "16:9" : s->wide_w == 16.0 && s->wide_h == 10.0 ? "16:10" :
                 s->wide_w == 21.0 && s->wide_h == 9.0 ? "21:9" : "custom", s->render_size[0] ? s->render_size :
                 "game (480 lines)", in->view_w, in->view_h, in->host_w, in->host_h,
                 in->display_hz > 0 ? "" : "; the display's rate not measured");
        snprintf(fr, sizeof(fr), "%s, adaptive %s; render thread %s (%s now)%s", rate, s->adaptive ? "on" : "off",
                 twoc_rt_name(s->render_thread), in->rt_on ? "on" : "off", in->meas_ok ? "" : "");
        if (in->meas_ok)
            snprintf(fr + strlen(fr), sizeof(fr) - strlen(fr), "; measured: the game %.1f updates and %.1f frames "
                     "shown a second", in->game_ups, in->shown_fps);
        twoc_settings_changed_text(s, changed, sizeof(changed));
        n = twoc_about_copy(in->about, picture, fr, changed, in->hwnd, NULL, 0);
        if (n >= 0)
            snprintf(note, sizeof(note), "Copied: %d lines, ready to paste into a problem report (no user name, PC "
                     "name or folders in it).", n);
        else
            snprintf(note, sizeof(note), "The clipboard couldn't be written: try again.");
        fprintf(stderr, "[OVL] About: Copy build info (%s)\n", n >= 0 ? "copied" : "the clipboard refused");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Open logs folder")) {
        fprintf(stderr, "[OVL] About: Open logs folder\n");
        twoc_about_open_logs();
    }
    if (url[0]) {                       /* only with ISSUES_URL set: no link while it is the placeholder */
        ImGui::SameLine();
        if (ImGui::Button("Report a problem")) {
            fprintf(stderr, "[OVL] About: Report a problem\n");
            twoc_about_open_issues();
        }
    }
    more("Copy build info puts a text block on the clipboard to paste into a problem report: this build, Windows, "
         "the processor, the graphics card and driver, the display and its refresh rate, the frame rate setting, "
         "the settings changed from the defaults and the game files' check. No user name, no PC name and no folders "
         "are in it. Open logs folder shows the game's log files (the last run's, and earlier runs' in logs). The "
         "game makes no network connections and sends nothing by itself.");
    if (g_measure || note[0])
        help(g_measure ? "Copied: 15 lines, ready to paste into a problem report (no user name, PC name or folders "
                         "in it)." : note);
}

/* Step 97: one tab's contents, into the window's child for it or (g_measure) the measuring window. */
static int tab_body(int t, twoc_settings_t *s, const ovl_info_t *in)
{
    switch (t) {
    case TAB_DISPLAY:
        return tab_display(s, in);
    case TAB_GRAPHICS:                  /* step 104 */
        return tab_graphics(s, in);
    case TAB_AUDIO:
        return tab_audio(s);
    case TAB_CONTROLS:
        return tab_controls(s);
    case TAB_SAVES:
        tab_saves();
        return 0;
    case TAB_EXTRAS:                    /* step 104 */
        return tab_extras(s, in);
    default:
        tab_about(s, in);
        return 0;
    }
}

/* Step 97: every tab drawn off-screen into a window as wide as the menu, each with the rows its settings
 * can add shown (the frame rate's rows at 72 fps, ambient occlusion's Strength and Half
 * resolution, the custom render size, the notes), to find the tallest. */
static void measure(const twoc_settings_t *s, const ovl_info_t *in, float width)
{
    twoc_settings_t m = *s;
    int t;

    m.hud = 1;
    if (!m.framerate)
        m.framerate = 72;
    if (m.ao == TWOC_AO_OFF)
        m.ao = TWOC_AO_GTAO;
    g_measure = true;
    ImGui::SetNextWindowPos(ImVec2(-100000.0f, -100000.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
    ImGui::Begin("##measure", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                                        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
    for (t = 0; t < N_TABS; t++) {
        float y0;
        ImGui::PushID(t);
        y0 = ImGui::GetCursorPosY();
        tab_body(t, &m, in);
        g_tab_h[t] = ImGui::GetCursorPosY() - y0;
        ImGui::PopID();
    }
    ImGui::End();
    g_measure = false;
}

/* Step 97: the settings that apply only at the next start and differ from this run's (the Graphics card,
 * Skip the title animation, Speakers, a save backup to restore), as a mask; their names into g_pend_list. */
enum { PEND_GPU = 1, PEND_TITLE = 2, PEND_SPEAKERS = 4, PEND_RESTORE = 8 };

static unsigned pending(const twoc_settings_t *s, const ovl_info_t *in, char *restore_when, size_t cap)
{
    static const char *const names[4] = { "Graphics card", "Skip the title animation", "Speakers",
                                          "Save restore" };
    const char *e = getenv("RECOMP_SPEAKERS");
    saves_state_t st;
    unsigned m = 0, i;

    if (_stricmp(s->gpu, twoc_gpu_asked()) != 0)
        m |= PEND_GPU;
    if (!s->fast_title != !in->fast_title_now)
        m |= PEND_TITLE;
    if ((s->speakers > 0) != (e && strcmp(e, "5.1") == 0))
        m |= PEND_SPEAKERS;
    saves_state(&st);
    restore_when[0] = 0;
    if (st.pending[0]) {
        m |= PEND_RESTORE;
        snprintf(restore_when, cap, "%.10s %.2s:%.2s", st.pending, st.pending + 11, st.pending + 14);
    }
    g_pend_list[0] = 0;
    for (i = 0; i < 4; i++)
        if (m & (1u << i))
            snprintf(g_pend_list + strlen(g_pend_list), sizeof(g_pend_list) - strlen(g_pend_list), "%s%s",
                     g_pend_list[0] ? ", " : "", names[i]);
    return m;
}

extern "C" const char *ovl_restart_why(void)
{
    return g_pend_list;
}

/* Step 97: a question as the reset's: `yes` / `no`, the second focused first, B (or the overlay closing)
 * for it. Returns 1 the frame `yes` is chosen; *open: it is up. */
static int ask(const char *id, const char *text, const char *yes, const char *no, bool *open)
{
    int r = 0;

    *open = false;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(id, NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        bool done = g_q_shut;           /* the overlay closed with it open */
        float bw = ImGui::GetFontSize() * 7;
        *open = !g_q_shut;
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button(yes, ImVec2(bw, 0))) {
            r = 1;
            done = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(no, ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
            done = true;                /* B on the pad, as it closes a list */
        ImGui::SetItemDefaultFocus();   /* the safe answer first: the other is a step away */
        if (done)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return r;
}

/* Step 93: a reset's Yes / No (No focused, B or the overlay closing for it); step 104: one for each tab with a
 * reset. Returns 1 the frame Yes is chosen; *open: it is up. */
static int reset_ask(const char *id, const char *text, bool *open)
{
    int r = 0;

    *open = false;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(id, NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        bool no = g_reset_shut;         /* the overlay closed with the question open */
        *open = !g_reset_shut;
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Yes", ImVec2(ImGui::GetFontSize() * 6, 0))) {
            r = 1;
            no = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(ImGui::GetFontSize() * 6, 0)) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
            no = true;                  /* B on the pad, as it closes a list */
        ImGui::SetItemDefaultFocus();   /* No first: Yes is a step away */
        if (no)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return r;
}

/* Step 97: the rows at the bottom, in the same place on every tab: Resume and Quit game (how to drive the
 * menu in a "(?)" after them), and a status row (a restart waiting, with its button; why one didn't happen;
 * the last save's outcome). */
static int footer(const ovl_info_t *in, unsigned pend)
{
    float fh = ImGui::GetFrameHeightWithSpacing();
    int r = 0;

    ImGui::Spacing();
    ImGui::BeginDisabled(in->ending != 0);
    if (ImGui::Button(in->pad ? "Resume  (F1, B, Start held)" : "Resume  (F1 or Esc)"))
        r |= OVL_CLOSE;
    ImGui::SameLine();
    if (ImGui::Button("Quit game"))
        ImGui::OpenPopup(QUIT_ASK);
    ImGui::EndDisabled();
    more("Controller: D-pad or left stick to move, A to choose, B to close a list or this menu, LB / RB for the "
         "tabs; Start (Menu, Options) held about a second, or View + Menu held, opens and closes this menu. "
         "Keyboard: F1 opens and closes it, the arrows move, Enter chooses, Esc closes. A long tab scrolls: the "
         "D-pad goes down it, the mouse wheel scrolls it. Quit game asks first, then closes the game as "
         "closing its window does, after saving these settings.");
    ImGui::BeginChild("##status", ImVec2(0, fh), ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::AlignTextToFramePadding();
    if (in->ending) {
        ImGui::TextColored(k_amber, "%s", g_end_kind == OVL_RESTART ? "Restarting..." : "Closing the game...");
    } else if (in->end_note) {
        ImGui::TextColored(k_amber, "%s", in->end_note);
    } else if (pend && !twoc_settings_ignored()) {
        if (ImGui::Button("Restart now"))
            r |= OVL_RESTART;
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(k_amber, "Changes waiting for a restart: %s", g_pend_list);
        if (ImGui::IsItemHovered())     /* a long list runs past the edge: all of it on hover */
            ImGui::SetTooltip("Changes waiting for a restart: %s", g_pend_list);
    } else if (in->save_note) {
        ImGui::TextDisabled("%s", in->save_note);
    } else {
        ImGui::TextUnformatted("");
    }
    ImGui::EndChild();
    return r;
}

static int build(twoc_settings_t *s, const ovl_info_t *in)
{
    static const char *const tab_names[N_TABS] = { "Display", "Graphics", "Audio", "Controls", "Saves", "Extras",
                                                   "About" };
    char restore_when[32], id[16];
    unsigned pend;
    float y0;
    int r = 0, t;

    if (twoc_settings_ignored()) {      /* step 93: the safe-settings launcher */
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX());
        ImGui::TextColored(k_amber, "Safe settings: %s is ignored this run and "
                           "won't be written. Every setting starts at its default; changes here last until the "
                           "game closes. Start the game the usual way for your own settings again.", twoc_ini_name());
        ImGui::PopTextWrapPos();
    }
    /* Step 104: the two pause settings that were here are on the Extras tab. Step 97: the tab bar picks the tab only; its contents go in a child of the same height on every tab
     * (g_child_h), each tab with its own (so its own scroll), flattened into the window for the controller's
     * and the keyboard's navigation. */
    if (ImGui::BeginTabBar("##tabs")) {
        /* step 82: LB and RB pick the tab before or after */
        for (t = 0; t < N_TABS; t++)
            if (ImGui::BeginTabItem(tab_names[t], NULL,
                                    g_tab_want == t ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
                g_tab_cur = t;
                ImGui::EndTabItem();
            }
        g_tab_want = -1;
        ImGui::EndTabBar();
    }
    if (g_tab_cur != TAB_SAVES)
        g_sv_shown = 0;                 /* step 83: the backups listed again when the tab is next shown */
    y0 = ImGui::GetCursorPosY();
    snprintf(id, sizeof(id), "##tab%d", g_tab_cur);
    ImGui::BeginChild(id, ImVec2(0, g_child_h), ImGuiChildFlags_NavFlattened);
    r |= tab_body(g_tab_cur, s, in);
    ImGui::EndChild();
    (void)y0;

    /* Step 93: the Display tab's reset; step 104: and the Extras tab's, both asked here (the safe answer
     * focused, B or the overlay closing for it) */
    if (g_reset_req == 1)
        ImGui::OpenPopup(RESET_ASK);
    else if (g_reset_req == 2)
        ImGui::OpenPopup(XRESET_ASK);
    g_reset_req = 0;
    {
        bool d_open, x_open;
        if (reset_ask(RESET_ASK, "Every setting on the Display and Graphics tabs goes back to its default. "
                      "Fullscreen, the aspect ratio, the graphics card, the Extras, sound, controls and saves stay "
                      "as they are.", &d_open)) {
            twoc_settings_display_defaults(s);
            r |= OVL_CHANGED;
            g_scroll_focus = 12;        /* step 104: Menu scale back to 100%: the button kept in view */
        }
        if (reset_ask(XRESET_ASK, "Every setting on the Extras tab goes back to its default, and photo mode "
                      "goes off. The other tabs stay as they are.", &x_open)) {
            twoc_settings_extras_defaults(s);
            r |= OVL_CHANGED | OVL_PHOTO_OFF;   /* step 104 */
        }
        g_reset_open = d_open || x_open;
    }
    g_reset_shut = false;

    /* Step 97: a setting from the next start just changed: ask whether to restart now (never with the safe
     * settings, which can't keep the change). */
    pend = twoc_settings_ignored() ? 0 : pending(s, in, restore_when, sizeof(restore_when));
    if (!g_pend_init) {
        g_pend_seen = pend;
        snprintf(g_gpu_seen, sizeof(g_gpu_seen), "%s", s->gpu);
        g_pend_init = 1;
    } else if (!in->ending) {
        unsigned fresh = pend & ~g_pend_seen;
        if ((pend & PEND_GPU) && _stricmp(g_gpu_seen, s->gpu) != 0)
            fresh |= PEND_GPU;          /* another card than the one asked about */
        g_pend_seen = pend;
        snprintf(g_gpu_seen, sizeof(g_gpu_seen), "%s", s->gpu);
        if (fresh && !g_quit_open && !g_reset_open && !g_restart_open) {
            char what[200];
            if (fresh & PEND_GPU) {
                int k = s->gpu[0] ? twoc_gpu_find(s->gpu) : -1;
                snprintf(what, sizeof(what), "use %s", !s->gpu[0] ? "Automatic (the card Windows picks)"
                                                      : k >= 0 ? twoc_gpu_at(k)->name : s->gpu);
            } else if (fresh & PEND_TITLE) {
                snprintf(what, sizeof(what), s->fast_title ? "skip the title animation"
                                                           : "play the title animation again");
            } else if (fresh & PEND_SPEAKERS) {
                snprintf(what, sizeof(what), "use %s", s->speakers > 0 ? "5.1 surround" : "stereo");
            } else {
                snprintf(what, sizeof(what), "restore the save backup of %s", restore_when);
            }
            snprintf(g_restart_text, sizeof(g_restart_text), "Restart the game to %s? Unsaved progress will be "
                     "lost.", what);
            ImGui::OpenPopup(RESTART_ASK);
            fprintf(stderr, "[OVL] asks: %s\n", g_restart_text);
        }
    }
    r |= footer(in, pend);
    if (ask(QUIT_ASK, "Quit the game? Unsaved progress will be lost.", "Yes", "No", &g_quit_open))
        r |= OVL_QUIT;
    if (ask(RESTART_ASK, g_restart_text, "Restart now", "Later", &g_restart_open))
        r |= OVL_RESTART;
    if (g_setup_req && !g_measure) {    /* step 107 */
        ImGui::OpenPopup(SETUP_ASK);
        fprintf(stderr, "[OVL] asks: %s\n", SETUP_ASK);
    }
    g_setup_req = 0;
    if (ask(SETUP_ASK, "The game closes and starts again with its setup screen, where you choose the disc image or "
            "folder. Your current game files stay until the new ones are copied and checked, and your saves are "
            "kept. Unsaved progress will be lost.", "Restart", "No", &g_setup_open)) {
        if (twoc_setup_request_write() == 0)
            r |= OVL_RESTART;
        else
            fprintf(stderr, "[OVL] the setup request couldn't be written: no restart\n");
    }
    g_q_shut = false;
    if (r & (OVL_QUIT | OVL_RESTART))
        g_end_kind = r & OVL_RESTART ? OVL_RESTART : OVL_QUIT;
    return r;
}

extern "C" int ovl_frame(ID3D11RenderTargetView *rtv, unsigned w, unsigned h, float scale, float dt,
                         twoc_settings_t *s, const ovl_info_t *info)
{
    ID3D11DeviceContext *ctx = NULL;
    ID3D11RenderTargetView *old_rtv = NULL;
    ID3D11DepthStencilView *old_dsv = NULL;
    int r;

    if (!g_ready)
        return 0;
    EnterCriticalSection(&g_cs);
    if (info->running)                  /* step 57: smaller over the running game, to leave more of it in view */
        scale *= 0.8f;
    /* Step 104: hcap, the scale at which the window's height holds the menu with TAB_MIN_ROWS rows of tabs
     * (measured on a shown frame; the menu's height grows in step with its scale), again from scratch when
     * the picture's height changes. */
    static float hcap = 1e9f;
    static unsigned hcap_h;
    float s100, s0, user = (float)(s->menu_scale >= TWOC_MENU_SCALE_MIN && s->menu_scale <= TWOC_MENU_SCALE_MAX
                                       ? s->menu_scale : 100) / 100.0f;
    if (h != hcap_h) {
        hcap = 1e9f;
        hcap_h = h;
    }
    {   /* step 93: never wider than the picture (34 font sizes of 18 px and the padding): a small window in real
         * pixels, as the reset's Game (480 lines) on a scaled display gives, had the panel cut off at the right.
         * Step 104: the player's Menu scale times the menu's own (s0: as wide and as tall as it is at 100%),
         * held to the most the window takes */
        float wmax = (float)w * 0.98f / (34.0f * 18.0f + 32.0f);
        s100 = scale < wmax ? scale : wmax;
        s0 = s100 < hcap ? s100 : hcap;
        scale = s0 * user;
        if (scale > wmax)
            scale = wmax;
        if (scale > hcap)
            scale = hcap;
        if (scale < MS_MIN_SCALE && s0 >= MS_MIN_SCALE)
            scale = MS_MIN_SCALE;
    }
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);
    io.DeltaTime = dt > 0.0f ? dt : 1.0f / 60.0f;
    /* The panel shrinks to fit the picture's height (the game's 480 lines in a window), from the last frame's
     * measure, starting again at 1 when the size or the scale changes. Step 97: only as far as the tabs keep
     * TAB_MIN_ROWS rows; a taller tab scrolls inside the window instead, which stays the same size on every
     * tab: the tallest tab's height, or all the picture has room for. */
    static float fit = 1.0f, last_scale = -1.0f;
    static unsigned last_h;
    if (scale != last_scale || h != last_h) {
        fit = 1.0f;
        last_scale = scale;
        last_h = h;
        g_meas_need = 1;
        g_fixed_h = 0;
    }
    if (scale * fit != g_scale) {
        ImGuiStyle &st = ImGui::GetStyle();
        st = g_style_base;
        st.ScaleAllSizes(scale * fit);
        st.FontScaleDpi = scale * fit;
        g_scale = scale * fit;
        g_meas_need = 1;
        g_fixed_h = 0;
    }
    r = pad_nav(info);                  /* step 82 */
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    bool settling = g_fixed_h == 0;     /* step 97: this frame measures the window: not shown (it would jump) */
    {   /* step 97: the tabs' height */
        float fs = ImGui::GetFontSize(), fh = ImGui::GetFrameHeightWithSpacing(), room = (float)h * 0.96f;
        float want = 0, avail;
        int t;
        if (g_meas_need) {
            measure(s, info, fs * 34);
            g_meas_need = 0;
        }
        for (t = 0; t < N_TABS; t++)
            want = g_tab_h[t] > want ? g_tab_h[t] : want;
        want += fh * TAB_MARGIN_ROWS;
        if (!g_fixed_h)                 /* before the first frame at this scale: the header, tab row and footer */
            g_fixed_h = fh * 9.0f;
        avail = room - g_fixed_h;
        g_child_h = want < avail ? want : avail;
        if (g_child_h < fh * TAB_MIN_ROWS)
            g_child_h = fh * TAB_MIN_ROWS;
        g_child_h = floorf(g_child_h);  /* whole pixels: a fraction moved the window a pixel a frame */
    }
    if (info->running)                  /* step 57: the running game at the left, undimmed, to watch a change */
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.02f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.0f, 0.5f));
    else {
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 120));
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
    }
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 34, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(info->running ? 0.8f : g_style_base.Colors[ImGuiCol_WindowBg].w);
    char title[96];                     /* step 109: "Project Wrath - settings (paused)" (names.h); the same id */
    snprintf(title, sizeof(title), "%s - settings (%s)###settings", twoc_app_name(),
             info->running ? "the game runs" : "paused");
    ImGui::Begin(title, NULL,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);
    r |= build(s, info);
    {
        float wh = ImGui::GetWindowSize().y, room = (float)h * 0.96f, fh = ImGui::GetFrameHeightWithSpacing();
        float fixed = ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y - g_child_h;
        if (fixed > 0 && fabsf(fixed - g_fixed_h) > 1.5f) {
            g_fixed_h = ceilf(fixed);   /* the window's height outside the tabs, for the next frame (only a
                                         * real change: rounding alone had it and the tabs swap a pixel) */
            settling = true;            /* this frame was drawn at the old one: not shown */
        }
        {   /* or its contents changed height (the auto-fit follows a frame later), or its size is catching up */
            static float last_content, last_wh;
            float content = fixed + g_child_h;
            if (ImGui::IsWindowAppearing() || fabsf(content - last_content) > 0.5f || fabsf(wh - last_wh) > 0.5f)
                settling = true;
            last_content = content;
            last_wh = wh;
        }
        if (g_fixed_h + fh * TAB_MIN_ROWS > room && fit > 0.4f)
            fit *= room / (g_fixed_h + fh * TAB_MIN_ROWS);
        if (!settling) {                /* step 104: the scale the window's height holds, and what is shown */
            float hc = g_scale * room / (g_fixed_h + fh * TAB_MIN_ROWS);
            if (hcap > 1e8f || fabsf(hc - hcap) > hcap * 0.02f)
                hcap = hc;              /* a real change only: rounding alone would have the menu re-measured */
            g_ms_shown = (int)(g_scale / s0 * 100.0f + 0.5f);
            if (g_ms_shown < 100 && s->menu_scale >= 100)
                g_ms_shown = 100;       /* the height's estimate a hair under the menu's own size: that size */
            g_ms_limited = g_ms_shown + 1 < s->menu_scale;
        }
        {   /* test only: the window's place and size whenever they or the tab change */
            static int on = -1, tab = -1;
            static ImVec2 pos, size;
            ImVec2 p = ImGui::GetWindowPos(), z = ImGui::GetWindowSize();
            if (on < 0)
                on = getenv("RECOMP_DEV_OVL_RECT") && *getenv("RECOMP_DEV_OVL_RECT");
            if (on && (tab != g_tab_cur || p.x != pos.x || p.y != pos.y || z.x != size.x || z.y != size.y)) {
                float tallest = 0;
                int t;
                for (t = 0; t < N_TABS; t++)
                    tallest = g_tab_h[t] > tallest ? g_tab_h[t] : tallest;
                fprintf(stderr, "[OVL] rect: tab %d at %.1f,%.1f size %.1f x %.1f%s (picture %ux%u, scale %.3f, "
                        "tabs' child %.1f, tallest tab %.1f)\n", g_tab_cur, p.x, p.y, z.x, z.y,
                        settling ? " not shown" : "", w, h, g_scale, g_child_h, tallest);
                tab = g_tab_cur;
                pos = p;
                size = z;
            }
        }
        (void)wh;
    }
    ImGui::End();
    ImGui::Render();
    if (!settling) {                    /* step 104: the steps after a menu scale change, on shown frames */
        if (g_scroll_focus)
            g_scroll_focus--;
        if (g_ms_warp >= 1 && g_ms_warp < MS_WARP_READ)
            g_ms_warp++;                /* shown frames for the new size and the scroll to settle */
        else if (g_ms_warp == MS_WARP_READ + 1) {
            POINT pt, c;
            RECT cr;
            g_ms_warp = 0;
            if (info->hwnd && GetCursorPos(&pt) && GetClientRect(info->hwnd, &cr) && cr.right > 0 && cr.bottom > 0) {
                c = pt;
                ScreenToClient(info->hwnd, &c);
                if (c.x >= 0 && c.y >= 0 && c.x < cr.right && c.y < cr.bottom) {  /* only a pointer in the window */
                    c.x = (LONG)(g_ms_warp_at.x * cr.right / (float)w);
                    c.y = (LONG)(g_ms_warp_at.y * cr.bottom / (float)h);
                    ClientToScreen(info->hwnd, &c);
                    SetCursorPos(c.x, c.y);
                    io.AddMousePosEvent(g_ms_warp_at.x, g_ms_warp_at.y);
                    fprintf(stderr, "[OVL] menu scale %d%%: the pointer back on its slider\n", s->menu_scale);
                }
            }
        }
    }

    {
        ID3D11Device *dev = NULL;
        rtv->GetDevice(&dev);
        dev->GetImmediateContext(&ctx);
        dev->Release();
    }
    ctx->OMGetRenderTargets(1, &old_rtv, &old_dsv);
    ctx->OMSetRenderTargets(1, &rtv, NULL);
    if (!settling)
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    ctx->OMSetRenderTargets(1, &old_rtv, old_dsv);
    if (old_rtv)
        old_rtv->Release();
    if (old_dsv)
        old_dsv->Release();
    ctx->Release();
    LeaveCriticalSection(&g_cs);
    return r;
}
