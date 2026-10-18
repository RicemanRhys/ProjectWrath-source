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

#include "imgui.h"
#include "imgui_impl_dx11.h"

#include "overlay.h"
#include "pads.h"
#include "saves.h"

/* xboxrecomp apu.h: the output's channels (2 or 6) and the device's */
extern "C" int mcpx_apu_output_channels(int *device_channels);

static CRITICAL_SECTION g_cs;
static volatile LONG g_cs_ready;
static bool g_ready;
static float g_scale = 1.0f;
static ImGuiStyle g_style_base;

/* Step 57: a binding waiting for its button or key (under g_cs). */
enum { CAP_NONE, CAP_PAD, CAP_KEY };
static int g_cap, g_cap_idx, g_cap_slot;

/* Step 82: the controller drives the overlay (pads.h's OVL_PAD_* bits from
 * the frame's info): the D-pad or the left stick move, A chooses, B closes a
 * list or the overlay, LB and RB change tabs. A button held when the
 * overlay opens or a binding ends counts once it is let go. Under g_cs. */
static unsigned g_pad_mask, g_pad_last;
static int g_pad_fresh = 1;
static int g_tab_cur, g_tab_want = -1;
#define N_TABS 4                        /* Display, Audio, Controls, Saves (step 83) */
static int g_sv_shown;                  /* step 83: the Saves tab shown since the overlay opened */

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

static const char *const g_sizes[] = { "", "1280x720", "1600x900", "1920x1080", "2560x1440", "3840x2160", "native" };
#define N_SIZES (int)(sizeof(g_sizes) / sizeof(g_sizes[0]))

static const char *size_label(const char *v, char *buf, size_t cap)
{
    if (!v[0])
        return "Game (480 lines)";
    if (_stricmp(v, "native") == 0)
        return "Monitor (native)";
    snprintf(buf, cap, "%s", v);
    return buf;
}

static void help(const char *text)
{
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX());
    ImGui::TextDisabled("%s", text);
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

/* Step 57: the Controls tab. The child scrolls when the lists don't fit
 * above `room`, the picture's height the window may take. */
static int tab_controls(twoc_settings_t *s, float room)
{
    static const int pad_order[XIN_COUNT] = { XIN_A, XIN_B, XIN_X, XIN_Y, XIN_BLACK, XIN_WHITE, XIN_LT, XIN_RT,
                                              XIN_START, XIN_BACK, XIN_LSTICK, XIN_RSTICK, XIN_LS, XIN_RS,
                                              XIN_UP, XIN_DOWN, XIN_LEFT, XIN_RIGHT };
    static const int key_order[KIN_COUNT] = { KIN_LUP, KIN_LDOWN, KIN_LLEFT, KIN_LRIGHT, XIN_A, XIN_B, XIN_X,
                                              XIN_Y, XIN_START, XIN_BACK, XIN_BLACK, XIN_WHITE, XIN_LT, XIN_RT,
                                              XIN_LS, XIN_RS, XIN_UP, XIN_DOWN, XIN_LEFT, XIN_RIGHT,
                                              KIN_RUP, KIN_RDOWN, KIN_RLEFT, KIN_RRIGHT };
    const ImGuiStyle &st = ImGui::GetStyle();
    float fs = ImGui::GetFontSize(), maxh;
    int r = 0, i, slot, got, style, npads;
    pads_info_t pads[12];
    unsigned char has[SRC_COUNT] = { 0 };
    char lbl[160];

    if (g_cap == CAP_PAD && (got = controls_capture_pad(g_cap_idx >= XIN_BUTTONS)) >= 0) {
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
    }
    npads = pads_list(pads, (int)(sizeof(pads) / sizeof(pads[0])));
    style = pads_style();
    for (i = 0; i < npads; i++)
        if (pads[i].read)
            for (got = 0; got < SRC_COUNT; got++)
                has[got] |= pads[i].has[got];

    maxh = room - ImGui::GetCursorPosY() - ImGui::GetFrameHeightWithSpacing() - st.WindowPadding.y -
           st.ItemSpacing.y * 2;
    if (maxh < ImGui::GetFrameHeightWithSpacing() * 4)
        maxh = ImGui::GetFrameHeightWithSpacing() * 4;
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, maxh));
    ImGui::BeginChild("##controls", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened);

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
    ImGui::EndChild();
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
 * leave the picture's middle 4:3. `scale` is the frame's (the counter's). */
static void startup_lines(const float *pic, float scale, const char *msg, float alpha)
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
    ImGui::SetNextWindowPos(ImVec2(pic[0] + pic[2] * 0.5f, pic[1] + pic[3] * 0.985f), ImGuiCond_Always,
                            ImVec2(0.5f, 1.0f));
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
    if (lines) {                        /* step 86: NULL with only the startup message to draw */
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.01f, io.DisplaySize.y * 0.01f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.55f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * scale, 5.0f * scale));    /* a small box */
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * scale, 2.0f * scale));
        ImGui::Begin("##fps", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav |
                                        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted(lines);
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
    if (msg && msg_alpha > 0.0f && pic)
        startup_lines(pic, scale, msg, msg_alpha);
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
    static const int aniso[] = { 1, 2, 4, 8, 16 };
    static const char *const aniso_names[] = { "Off (the game's own)", "2x", "4x", "8x", "16x" };
    int r = 0, i;
    char buf[64], line[160];

    ImGui::SeparatorText("Display");
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
        if (s->hud) {
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
            if (ImGui::SliderInt("HUD margin", &s->hud_margin, 0, 10, "%d%%"))
                r |= OVL_CHANGED;
            more("Pulls the HUD at the edges in from them by this share of the picture's width.");
        }
        if (s->hud && !wide)
            help("Now: as the game draws it (the picture is 4:3).");
    }
    {   /* step 69: the draw distance */
        static const double dists[] = { 1.0, 1.5, 2.0, 3.0 };
        static const char *const dist_names[] = { "Game (1x)", "1.5x", "2x", "3x" };
        int cur = -1, n = (int)(sizeof(dists) / sizeof(dists[0]));
        for (i = 0; i < n; i++)
            if (s->draw_distance == dists[i])
                cur = i;
        snprintf(buf, sizeof(buf), "%gx", s->draw_distance);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
        if (ImGui::BeginCombo("Draw distance", cur >= 0 ? dist_names[cur] : buf)) {
            if (cur < 0)                /* another multiplier, from the command line or the file */
                ImGui::Selectable(buf, true);
            for (i = 0; i < n; i++)
                if (ImGui::Selectable(dist_names[i], i == cur) && i != cur) {
                    s->draw_distance = dists[i];
                    r |= OVL_CHANGED;
                }
            ImGui::EndCombo();
        }
        more("How far the world is drawn. Each level ends its view at a set distance and fogs what is near "
             "it; this multiplies both, so open levels show more of themselves under a lighter haze. Crates and "
             "fruit still appear 45 units out and enemies 25 from Crash, as the game decides, which shows more "
             "at 3x. Applied at once.");
    }
    {
        int cur = -1;
        char lbl[64];
        for (i = 0; i < N_SIZES; i++)
            if (_stricmp(g_sizes[i], s->render_size) == 0)
                cur = i;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
        if (ImGui::BeginCombo("Render size", size_label(s->render_size, lbl, sizeof(lbl)))) {
            if (cur < 0)                /* a size given on the command line or in the file */
                ImGui::Selectable(size_label(s->render_size, lbl, sizeof(lbl)), true);
            for (i = 0; i < N_SIZES; i++)
                if (ImGui::Selectable(size_label(g_sizes[i], buf, sizeof(buf)), i == cur) && i != cur) {
                    snprintf(s->render_size, sizeof(s->render_size), "%s", g_sizes[i]);
                    r |= OVL_CHANGED;
                }
            ImGui::EndCombo();
        }
        more("The size the host draws the game at; the game itself draws 640x480. The picture is the largest "
             "of its aspect ratio that fits. A window opened without a render size is "
             "scaled by Windows until the next start; fullscreen is unaffected.");
        snprintf(line, sizeof(line), "Now %ux%u.", in->host_w, in->host_h);
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
        static const int fr_vals[8] = { 0, 1, 90, 120, 144, 165, 180, 240 };
        static const char *const fr_names[8] = { "Game (60)", "Match display", "90 fps", "120 fps", "144 fps",
                                                 "165 fps", "180 fps", "240 fps" };
        const char *names[9];
        char other[24];
        int n = 8, cur = -1;
        for (i = 0; i < 8; i++) {
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
        if (ImGui::Combo("Frame rate", &cur, names, n) && cur < 8 && fr_vals[cur] != s->framerate) {
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
        if (s->framerate && in->interp_note && in->interp_note[0])
            help(in->interp_note);      /* step 77: even or not on this display, and why */
        ovl_rates_text(in->meas_ok, in->game_ups, in->shown_fps, 0, line, sizeof(line));
        if (!in->running && in->meas_ok && strlen(line) > 1)    /* step 82: held, the rates from before it opened */
            snprintf(line + strlen(line) - 1, sizeof(line) - strlen(line) + 1, ", before this menu opened.");
        help(line);                     /* step 82: what is measured, beside the plan above */
        {
            bool f = s->show_fps != 0;
            if (ImGui::Checkbox("Show frame rate", &f)) {
                s->show_fps = f;
                r |= OVL_CHANGED;
            }
            more("In the picture's top left corner while this menu is closed: the game's updates a second (59.94 "
                 "is its full speed; under it the game itself runs slow) and the frames shown a second (with a "
                 "frame rate above Game (60), the game's frames and those drawn in between). Applied at once.");
        }
        {   /* step 86 */
            bool m = s->startup_message != 0;
            if (ImGui::Checkbox("Show the startup message", &m)) {
                s->startup_message = m;
                r |= OVL_CHANGED;
            }
            more("\"Press F1 to open the settings menu\" (and, with a controller, \"or hold Start\") at the bottom "
                 "of the picture for the first seconds of each start. Off also takes it away if it is showing.");
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

    if (!g_sv_shown) {                  /* the tab just opened */
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

static int build(twoc_settings_t *s, const ovl_info_t *in, float room)
{
    int r = 0;

    {
        bool p = s->pause != 0;
        if (ImGui::Checkbox("Pause the game while this menu is open", &p)) {
            s->pause = p;
            r |= OVL_CHANGED;
        }
        more("Off: the game keeps running and drawing behind this menu, so a change to the picture shows at "
             "once, and its sound plays on. It gets no controller or keyboard input until the menu closes, and "
             "a button still held then counts only once it is let go.");
    }
    if (ImGui::BeginTabBar("##tabs")) {
        /* step 82: LB and RB pick the tab before or after */
#define TAB_FLAGS(i) (g_tab_want == (i) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)
        if (ImGui::BeginTabItem("Display", NULL, TAB_FLAGS(0))) {
            g_tab_cur = 0;
            r |= tab_display(s, in);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Audio", NULL, TAB_FLAGS(1))) {
            g_tab_cur = 1;
            r |= tab_audio(s);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Controls", NULL, TAB_FLAGS(2))) {
            g_tab_cur = 2;
            r |= tab_controls(s, room);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Saves", NULL, TAB_FLAGS(3))) {     /* step 83 */
            g_tab_cur = 3;
            tab_saves();
            ImGui::EndTabItem();
        } else
            g_sv_shown = 0;
#undef TAB_FLAGS
        g_tab_want = -1;
        ImGui::EndTabBar();
    }

    ImGui::Spacing();
    if (ImGui::Button(in->pad ? "Resume  (F1, B, Start held)" : "Resume  (F1 or Esc)"))
        r |= OVL_CLOSE;
    if (in->save_note) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", in->save_note);
    }
    if (in->pad)                        /* step 82 */
        help("Controller: D-pad or left stick to move, A to choose, B to close, LB / RB for the tabs. "
             "Start (Menu, Options) held about a second, or View + Menu held, opens and closes this menu. "
             "Keyboard: F1.");
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
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);
    io.DeltaTime = dt > 0.0f ? dt : 1.0f / 60.0f;
    /* The panel shrinks to fit the picture's height (the game's 480 lines in
     * a window): g_fit, measured from the last frame, starts again at 1 when
     * the size or the scale changes. */
    static float fit = 1.0f, last_scale = -1.0f;
    static unsigned last_h;
    if (scale != last_scale || h != last_h) {
        fit = 1.0f;
        last_scale = scale;
        last_h = h;
    }
    if (scale * fit != g_scale) {
        ImGuiStyle &st = ImGui::GetStyle();
        st = g_style_base;
        st.ScaleAllSizes(scale * fit);
        st.FontScaleDpi = scale * fit;
        g_scale = scale * fit;
    }
    r = pad_nav(info);                  /* step 82 */
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
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
    ImGui::Begin(info->running ? "Crash TWOC - settings (the game runs)###settings"
                               : "Crash TWOC - settings (paused)###settings", NULL,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);
    r |= build(s, info, (float)h * 0.96f);
    {
        float wh = ImGui::GetWindowSize().y, room = (float)h * 0.96f;
        if (wh > room && fit > 0.4f)
            fit *= room / wh;
    }
    ImGui::End();
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
    return r;
}
