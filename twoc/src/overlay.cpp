/**
 * The settings overlay (step 54, notes 18.57). See overlay.h.
 *
 * Dear ImGui (third_party/imgui, v1.92.9b) with its DX11 renderer on the
 * bridge's device, and no platform backend: the host window's messages are
 * turned into ImGui input events here (ovl_input), on the window thread,
 * under g_cs; ovl_frame builds and draws a frame under the same lock. The
 * keyboard and mouse only ever reach ImGui: the game reads its controller
 * through XInput and no key, and it is paused while the overlay is open.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"

#include "overlay.h"

/* xboxrecomp apu.h: the output's channels (2 or 6) and the device's */
extern "C" int mcpx_apu_output_channels(int *device_channels);

static CRITICAL_SECTION g_cs;
static volatile LONG g_cs_ready;
static bool g_ready;
static float g_scale = 1.0f;
static ImGuiStyle g_style_base;

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
    if (g_ready) {
        ImGui::GetIO().ClearInputKeys();
        ImGui::GetIO().ClearInputMouse();
    }
    LeaveCriticalSection(&g_cs);
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
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
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

static int build(twoc_settings_t *s, const ovl_info_t *in)
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
    {
        bool w = s->widescreen != 0;
        snprintf(line, sizeof(line), "Widescreen (%g:%g)", s->wide_w, s->wide_h);
        if (ImGui::Checkbox(line, &w)) {
            s->widescreen = w;
            r |= OVL_CHANGED;
        }
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
        more("The size the host draws the game at; the game itself draws 640x480. With widescreen the picture "
             "is the widest of that aspect that fits, without it 4:3. A window opened without a render size is "
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
    else if (in->interval && in->frame_hz <= 0)
        snprintf(line, sizeof(line), "Now: display %.2f Hz / %u = %.2f fps.", in->display_hz, in->interval,
                 in->interval ? in->display_hz / in->interval : 0.0);
    else
        snprintf(line, sizeof(line), "Now: limiter %.2f fps%s.", in->frame_hz, in->interval ? ", on vsync" : "");
    help(line);

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

    ImGui::SeparatorText("Audio");
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

    ImGui::Spacing();
    if (ImGui::Button("Resume  (Esc)"))
        r |= OVL_CLOSE;
    if (in->save_note) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", in->save_note);
    }
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
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 120));
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30, 0), ImGuiCond_Always);
    ImGui::Begin("Crash TWOC - settings (paused)", NULL,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);
    r = build(s, info);
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
