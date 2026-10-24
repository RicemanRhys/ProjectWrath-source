/**
 * The first-run setup screen (step 107, notes 18.110). See setup_ui.h.
 *
 * One window of its own, a D3D11 device and a flip-model swap chain (blt-model
 * presents never reach the screen on some PCs, notes 18.33), Dear ImGui with
 * the F1 menu's font and style, per-monitor DPI aware. Input: the window's
 * messages into ImGui (as the overlay does it, ovl_input), XInput user 0 and
 * every other pad through pads.c (SDL) as ImGui's gamepad navigation, the
 * Windows file dialogs, a dropped file or folder (WM_DROPFILES), a typed or
 * pasted path, and a short list of disc images found in the usual folders
 * (the game's folder, Desktop, Downloads, Documents and one folder down), so
 * a controller alone can set the game up.
 *
 * Test aids (inert unless set): RECOMP_DEV_SETUP_DROP=PATH drops PATH onto the
 * window 1 s after it opens (the real WM_DROPFILES handler, an HDROP made in
 * this process); RECOMP_DEV_SETUP_SIZE=WxH the client size in pixels; each
 * screen change, button and choice is logged as [SETUP-UI].
 */
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <xinput.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"

#include "setup_ui.h"
#include "setup.h"
#include "xdvdfs.h"
#include "pads.h"

#define WIN_TITLE L"Crash TWOC - Setup"

enum { SCR_HOME, SCR_WORK, SCR_CONFIRM };

static struct {
    HWND hwnd;
    ID3D11Device *dev;
    ID3D11DeviceContext *ctx;
    IDXGISwapChain1 *sc;
    ID3D11RenderTargetView *rtv;
    UINT w, h;
    float dpi;
    int mode, files_ok, screen, last_screen, result, closing, done_shown;
    int installing;                     /* the install was started for this choice (the job's state lags a frame) */
    ULONGLONG done_at;
    char why[1400];
    char typed[2048];
    int focus;                          /* set focus on the screen's first button */
    unsigned pad_mask, pad_last;
    int pad_fresh;
    ImGuiStyle base;
    wchar_t drop[SETUP_PATH_MAX];       /* a dropped path waiting (the window procedure takes it) */
    int has_drop;
    ULONGLONG t_open;
    int dev_drop_done;
    char free_text[160];
    ULONGLONG free_at;
    int last_state;
} g;

/* ── Disc images found nearby (a thread; for a controller-only player) ── */

#define MAX_FOUND 12
typedef struct {
    wchar_t path[SETUP_PATH_MAX];
    char path8[SETUP_PATH_MAX * 3];
    char label[400];
    int ok;                             /* the supported disc (default.xbe's certificate and hash aside: title id) */
} found_t;
static found_t g_found[MAX_FOUND];
static volatile LONG g_nfound, g_scan_done;

static void u8(const wchar_t *w, char *out, size_t cap)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL))
        out[0] = 0;
}

static void probe_image(const wchar_t *path)
{
    xdvd_t *x = NULL;
    char err[600], title[128] = "", region[160] = "";
    unsigned tid = 0, reg = 0, ver = 0;
    const xdvd_entry_t *e;
    LONG i;

    if (g_nfound >= MAX_FOUND || xdvd_open(path, &x, err, sizeof(err)))
        return;
    e = xdvd_find(x, "default.xbe");
    if (e && !e->dir) {
        uint32_t n = e->size < (1u << 20) ? e->size : (1u << 20);
        unsigned char *b = (unsigned char *)malloc(n);
        if (b && xdvd_read(x, e, 0, b, n, err, sizeof(err)) == 0 &&
            twoc_xbe_describe(b, n, title, sizeof(title), &tid, &reg, &ver) == 0) {
            i = InterlockedIncrement(&g_nfound) - 1;
            if (i < MAX_FOUND) {
                found_t *f = &g_found[i];
                const wchar_t *name = wcsrchr(path, L'\\');
                char n8[600];
                wcsncpy_s(f->path, SETUP_PATH_MAX, path, _TRUNCATE);
                u8(path, f->path8, sizeof(f->path8));
                u8(name ? name + 1 : path, n8, sizeof(n8));
                twoc_region_name(reg, region, sizeof(region));
                f->ok = tid == 0x56550003u && (reg & 1);
                snprintf(f->label, sizeof(f->label), "%s  (%s%s)", n8, f->ok ? "Crash Bandicoot: The Wrath of Cortex, "
                         "USA" : title, f->ok ? "" : ": another game or region");
            }
        }
        free(b);
    }
    xdvd_close(x);
}

static void scan_dir(const wchar_t *dir, int depth)
{
    wchar_t pat[SETUP_PATH_MAX + 4], p[SETUP_PATH_MAX];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int sub = 0;

    if (!dir[0] || g_nfound >= MAX_FOUND)
        return;
    _snwprintf_s(pat, sizeof(pat) / sizeof(pat[0]), _TRUNCATE, L"%s\\*", dir);
    h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        size_t l = wcslen(fd.cFileName);
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
            continue;
        _snwprintf_s(p, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth > 0 && sub++ < 200 && !(fd.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT |
                                                                    FILE_ATTRIBUTE_HIDDEN)))
                scan_dir(p, depth - 1);
        } else if (l > 4 && (_wcsicmp(fd.cFileName + l - 4, L".iso") == 0) &&
                   ((uint64_t)fd.nFileSizeHigh << 32 | fd.nFileSizeLow) >= (100ull << 20)) {
            LONG i;
            int dup = 0;
            for (i = 0; i < g_nfound && i < MAX_FOUND; i++)
                if (!_wcsicmp(g_found[i].path, p))
                    dup = 1;
            if (!dup)
                probe_image(p);
        }
    } while (FindNextFileW(h, &fd) && g_nfound < MAX_FOUND);
    FindClose(h);
}

static unsigned __stdcall scan_thread(void *arg)
{
    static const KNOWNFOLDERID *const kf[] = { &FOLDERID_Downloads, &FOLDERID_Desktop, &FOLDERID_Documents };
    wchar_t exe[SETUP_PATH_MAX], *s;
    unsigned i;
    (void)arg;
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
    scan_dir(twoc_data_dir(), 1);
    if (GetModuleFileNameW(NULL, exe, SETUP_PATH_MAX) && (s = wcsrchr(exe, L'\\')) != NULL) {
        *s = 0;
        if (_wcsicmp(exe, twoc_data_dir()))
            scan_dir(exe, 1);
    }
    for (i = 0; i < sizeof(kf) / sizeof(kf[0]); i++) {
        PWSTR p = NULL;
        if (SUCCEEDED(SHGetKnownFolderPath(*kf[i], 0, NULL, &p)))
            scan_dir(p, 1);
        CoTaskMemFree(p);
    }
    fprintf(stderr, "[SETUP-UI] disc images found nearby: %ld\n", g_nfound < MAX_FOUND ? g_nfound : MAX_FOUND);
    InterlockedExchange(&g_scan_done, 1);
    return 0;
}

/* ── D3D ───────────────────────────────────────────────────────────────── */

static void rtv_make(void)
{
    ID3D11Texture2D *bb = NULL;
    if (g.rtv) {
        g.rtv->Release();
        g.rtv = NULL;
    }
    if (SUCCEEDED(g.sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb))) {
        g.dev->CreateRenderTargetView(bb, NULL, &g.rtv);
        bb->Release();
    }
}

static int d3d_make(void)
{
    static const D3D_FEATURE_LEVEL lv[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    IDXGIDevice *xd = NULL;
    IDXGIAdapter *ad = NULL;
    IDXGIFactory2 *f = NULL;
    DXGI_SWAP_CHAIN_DESC1 sd;
    HRESULT hr;
    int k;

    for (k = 0; k < 2; k++) {           /* the GPU, else WARP (the screen must show) */
        hr = D3D11CreateDevice(NULL, k ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, NULL, 0, lv, 2,
                               D3D11_SDK_VERSION, &g.dev, NULL, &g.ctx);
        if (SUCCEEDED(hr))
            break;
    }
    if (FAILED(hr))
        return -1;
    if (FAILED(g.dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&xd)) || FAILED(xd->GetAdapter(&ad)) ||
        FAILED(ad->GetParent(__uuidof(IDXGIFactory2), (void **)&f))) {
        if (xd) xd->Release();
        if (ad) ad->Release();
        return -1;
    }
    memset(&sd, 0, sizeof(sd));
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = f->CreateSwapChainForHwnd(g.dev, g.hwnd, &sd, NULL, NULL, &g.sc);
    if (SUCCEEDED(hr))
        f->MakeWindowAssociation(g.hwnd, DXGI_MWA_NO_ALT_ENTER);
    f->Release();
    ad->Release();
    xd->Release();
    if (FAILED(hr))
        return -1;
    rtv_make();
    return 0;
}

static void d3d_free(void)
{
    if (g.rtv) g.rtv->Release();
    if (g.sc) g.sc->Release();
    if (g.ctx) g.ctx->Release();
    if (g.dev) g.dev->Release();
    g.rtv = NULL;
    g.sc = NULL;
    g.ctx = NULL;
    g.dev = NULL;
}

/* ── Input ─────────────────────────────────────────────────────────────── */

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
    case VK_ESCAPE: return ImGuiKey_Escape;
    case VK_BACK:   return ImGuiKey_Backspace;
    case VK_DELETE: return ImGuiKey_Delete;
    case VK_SHIFT:  return ImGuiKey_LeftShift;
    case VK_CONTROL: return ImGuiKey_LeftCtrl;
    case VK_MENU:   return ImGuiKey_LeftAlt;
    case 'A':       return ImGuiKey_A;
    case 'C':       return ImGuiKey_C;
    case 'V':       return ImGuiKey_V;
    case 'X':       return ImGuiKey_X;
    case 'Z':       return ImGuiKey_Z;
    default:        return ImGuiKey_None;
    }
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (ImGui::GetCurrentContext()) {
        ImGuiIO &io = ImGui::GetIO();
        switch (msg) {
        case WM_MOUSEMOVE:
            io.AddMousePosEvent((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));
            break;
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
            io.AddMousePosEvent((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));
            io.AddMouseButtonEvent(0, msg != WM_LBUTTONUP);
            if (msg == WM_LBUTTONDOWN)
                SetCapture(h);
            else if (msg == WM_LBUTTONUP)
                ReleaseCapture();
            break;
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
            io.AddMouseButtonEvent(1, msg == WM_RBUTTONDOWN);
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
            if (wp == VK_RETURN && !io.WantTextInput)
                io.AddKeyEvent(ImGuiKey_Space, down);   /* Enter presses a button too (ImGui keeps it for text) */
            if (msg == WM_SYSKEYDOWN && wp == VK_F4)
                break;                  /* Alt+F4 closes, below */
            if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP)
                return 0;
            break;
        }
        case WM_CHAR:
            if (wp >= 0x20 && wp != 0x7F)
                io.AddInputCharacterUTF16((ImWchar16)wp);
            return 0;
        case WM_SETFOCUS:
            io.AddFocusEvent(true);
            break;
        case WM_KILLFOCUS:
            io.AddFocusEvent(false);
            break;
        }
    }
    switch (msg) {
    case WM_SIZE:
        if (g.sc && wp != SIZE_MINIMIZED && LOWORD(lp) && HIWORD(lp)) {
            if (g.rtv) {
                g.rtv->Release();
                g.rtv = NULL;
            }
            g.sc->ResizeBuffers(0, LOWORD(lp), HIWORD(lp), DXGI_FORMAT_UNKNOWN, 0);
            g.w = LOWORD(lp);
            g.h = HIWORD(lp);
            rtv_make();
        }
        return 0;
    case WM_DPICHANGED: {
        RECT *r = (RECT *)lp;
        g.dpi = HIWORD(wp) / 96.0f;
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DROPFILES: {
        HDROP d = (HDROP)wp;
        if (DragQueryFileW(d, 0, g.drop, SETUP_PATH_MAX))
            g.has_drop = 1;
        DragFinish(d);
        return 0;
    }
    case WM_CLOSE:
        g.closing = 1;                  /* the loop cancels a job under way, cleans up, then ends */
        return 0;
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* The controller: XInput user 0 and every other pad (pads.c) joined, as ImGui's gamepad navigation. Returns the
 * buttons newly pressed (OVL_PAD_*), after those held when the screen opened are let go. */
static unsigned pad_poll(void)
{
    ImGuiIO &io = ImGui::GetIO();
    XINPUT_STATE xs;
    pads_src_t o;
    unsigned b = 0, nav, press, pk = 0, packets = 0;
    float x = 0, y = 0;
    const float dz = 0.35f;

    memset(&o, 0, sizeof(o));
    if (XInputGetState(0, &xs) == ERROR_SUCCESS) {
        WORD w = xs.Gamepad.wButtons;
        b |= (w & XINPUT_GAMEPAD_DPAD_UP ? OVL_PAD_UP : 0) | (w & XINPUT_GAMEPAD_DPAD_DOWN ? OVL_PAD_DOWN : 0) |
             (w & XINPUT_GAMEPAD_DPAD_LEFT ? OVL_PAD_LEFT : 0) | (w & XINPUT_GAMEPAD_DPAD_RIGHT ? OVL_PAD_RIGHT : 0) |
             (w & XINPUT_GAMEPAD_A ? OVL_PAD_A : 0) | (w & XINPUT_GAMEPAD_B ? OVL_PAD_B : 0);
        x = xs.Gamepad.sThumbLX / 32767.0f;
        y = xs.Gamepad.sThumbLY / 32767.0f;
        pk = 1;
    }
    if (pads_read(&o, &packets) > 0) {
        b |= (o.v[XIN_UP] > 128 ? OVL_PAD_UP : 0) | (o.v[XIN_DOWN] > 128 ? OVL_PAD_DOWN : 0) |
             (o.v[XIN_LEFT] > 128 ? OVL_PAD_LEFT : 0) | (o.v[XIN_RIGHT] > 128 ? OVL_PAD_RIGHT : 0) |
             (o.v[XIN_A] > 128 ? OVL_PAD_A : 0) | (o.v[XIN_B] > 128 ? OVL_PAD_B : 0);
        if (fabsf(o.stick[0][0] / 32767.0f) > fabsf(x))
            x = o.stick[0][0] / 32767.0f;
        if (fabsf(o.stick[0][1] / 32767.0f) > fabsf(y))
            y = o.stick[0][1] / 32767.0f;
        pk = 1;
    }
    if (pk)
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    else
        io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    if (g.pad_fresh) {
        g.pad_mask = b;
        g.pad_fresh = 0;
    }
    g.pad_mask &= b;
    nav = b & ~g.pad_mask;
    press = nav & ~g.pad_last;
    g.pad_last = nav;
    io.AddKeyEvent(ImGuiKey_GamepadDpadUp, (nav & OVL_PAD_UP) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, (nav & OVL_PAD_DOWN) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, (nav & OVL_PAD_LEFT) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, (nav & OVL_PAD_RIGHT) != 0);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, x < -dz, x < -dz ? -x : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, x > dz, x > dz ? x : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, y > dz, y > dz ? y : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, y < -dz, y < -dz ? -y : 0.0f);
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, (nav & OVL_PAD_A) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadFaceRight, false);   /* B is the screen's Back, not ImGui's */
    if (press)
        fprintf(stderr, "[SETUP-UI] pad: %s%s%s%s%s%s\n", press & OVL_PAD_UP ? "Up " : "", press & OVL_PAD_DOWN ?
                "Down " : "", press & OVL_PAD_LEFT ? "Left " : "", press & OVL_PAD_RIGHT ? "Right " : "",
                press & OVL_PAD_A ? "A " : "", press & OVL_PAD_B ? "B" : "");
    return press;
}

/* ── Choices ───────────────────────────────────────────────────────────── */

static void screen(int s, const char *why)
{
    if (g.screen != s || why) {
        static const char *const names[] = { "home", "work", "confirm" };
        fprintf(stderr, "[SETUP-UI] screen: %s%s%s%s\n", names[s], why ? " (" : "", why ? why : "", why ? ")" : "");
    }
    g.screen = s;
    g.focus = 1;
}

static void choose(const wchar_t *path, const char *how)
{
    char p8[SETUP_PATH_MAX * 3];
    u8(path, p8, sizeof(p8));
    fprintf(stderr, "[SETUP-UI] %s: %s\n", how, p8);
    g.installing = 0;
    if (twoc_setup_identify(path) == 0)
        screen(SCR_WORK, "reading it");
}

static void choose_utf8(const char *path8, const char *how)
{
    wchar_t w[SETUP_PATH_MAX];
    if (MultiByteToWideChar(CP_UTF8, 0, path8, -1, w, SETUP_PATH_MAX))
        choose(w, how);
}

/* The Windows file dialog: an image (.iso) or a folder. */
static void dialog(int folder)
{
    IFileOpenDialog *d = NULL;
    IShellItem *it = NULL;
    PWSTR p = NULL;
    DWORD opt = 0;
    static const COMDLG_FILTERSPEC spec[] = { { L"Xbox disc images (*.iso)", L"*.iso;*.xiso" },
                                              { L"All files (*.*)", L"*.*" } };

    fprintf(stderr, "[SETUP-UI] button: %s\n", folder ? "Choose folder with the extracted game" :
            "Choose disc image (.iso)");
    if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d))))
        return;
    d->GetOptions(&opt);
    d->SetOptions(opt | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
    if (folder)
        d->SetTitle(L"Choose the folder with the extracted game (default.xbe and Crashdat)");
    else {
        d->SetTitle(L"Choose the disc image of Crash Bandicoot: The Wrath of Cortex (Xbox, USA)");
        d->SetFileTypes(2, spec);
    }
    if (SUCCEEDED(d->Show(g.hwnd)) && SUCCEEDED(d->GetResult(&it)) &&
        SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p)))
        choose(p, folder ? "folder chosen" : "image chosen");
    else
        fprintf(stderr, "[SETUP-UI] the dialog was closed without a choice\n");
    CoTaskMemFree(p);
    if (it)
        it->Release();
    d->Release();
    g.pad_fresh = 1;
}

/* ── Drawing ───────────────────────────────────────────────────────────── */

static const ImVec4 k_amber(1.0f, 0.78f, 0.35f, 1.0f);
static const ImVec4 k_green(0.55f, 0.9f, 0.55f, 1.0f);
static const ImVec4 k_red(1.0f, 0.5f, 0.45f, 1.0f);
static const ImVec4 k_dim(0.72f, 0.72f, 0.72f, 1.0f);

static bool big(const char *label, float w)
{
    bool r = ImGui::Button(label, ImVec2(w, ImGui::GetFrameHeight() * 1.5f));
    if (r)
        fprintf(stderr, "[SETUP-UI] button: %s\n", label);
    return r;
}

static void focus_here(void)
{
    if (g.focus) {
        ImGui::SetKeyboardFocusHere();
        ImGui::SetNavCursorVisible(true);   /* the first A or Enter presses it (a hidden cursor eats one) */
        g.focus = 0;
    }
}

static void fmt_time(double s, char *buf, size_t cap)
{
    if (s < 0)
        snprintf(buf, cap, "working out the time left");
    else if (s < 1.5)
        snprintf(buf, cap, "about a second left");
    else if (s < 90)
        snprintf(buf, cap, "about %.0f s left", s);
    else
        snprintf(buf, cap, "about %.0f min left", s / 60.0);
}

static void home(float cw, unsigned press)
{
    LONG nf = g_nfound < MAX_FOUND ? g_nfound : MAX_FOUND, i;
    const setup_recovery_t *rec = twoc_setup_recovery();

    if (g.mode == SETUP_UI_RECOVERY) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw);
        ImGui::TextColored(k_amber, "%s", rec->text);
        ImGui::PopTextWrapPos();
        if (rec->source[0]) {
            char l[SETUP_PATH_MAX * 3 + 40], s8[SETUP_PATH_MAX * 3];
            u8(rec->source, s8, sizeof(s8));
            snprintf(l, sizeof(l), "Retry: set up again from %s", s8);
            focus_here();
            if (big(l, cw))
                choose(rec->source, "retry after the interrupted setup");
        }
        ImGui::Spacing();
    } else if (g.mode == SETUP_UI_BROKEN) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw);
        ImGui::TextColored(k_amber, "The game files need attention: %s", g.why);
        ImGui::PopTextWrapPos();
    } else if (g.mode == SETUP_UI_REPLACE) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw);
        ImGui::TextColored(k_amber, "Choose another disc image or folder. Your current game files stay as they are "
                           "until the new ones have been copied and checked; your saves are kept.");
        ImGui::PopTextWrapPos();
    } else
        ImGui::TextColored(k_amber, "The game files aren't set up yet.");
    ImGui::Spacing();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw);
    ImGui::TextUnformatted("This game needs your own copy of the disc: an image of Crash Bandicoot: The Wrath of "
                           "Cortex for the original Xbox (USA), as an .iso file, or the files extracted from one. The "
                           "setup copies the game's files out of it into this game's folder and checks every one. "
                           "Nothing is downloaded and nothing is sent anywhere.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    focus_here();
    if (big("Choose disc image (.iso)...", cw))
        dialog(0);
    if (big("Choose folder with the extracted game...", cw))
        dialog(1);
    if (nf > 0) {
        ImGui::Spacing();
        ImGui::TextColored(k_dim, "Disc images found on this PC:");
        for (i = 0; i < nf; i++) {
            char l[500];
            snprintf(l, sizeof(l), "%s##found%ld", g_found[i].label, i);
            if (!g_found[i].ok)
                ImGui::PushStyleColor(ImGuiCol_Text, k_dim);
            if (ImGui::Button(l, ImVec2(cw, 0))) {
                fprintf(stderr, "[SETUP-UI] button: found image %ld\n", i);
                choose(g_found[i].path, "found image chosen");
            }
            if (!g_found[i].ok)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", g_found[i].path8);
        }
    } else if (!g_scan_done)
        ImGui::TextColored(k_dim, "Looking for disc images on this PC...");
    ImGui::Spacing();
    ImGui::TextColored(k_dim, "Or type or paste the path of the image or folder (or drag it onto this window):");
    ImGui::SetNextItemWidth(cw - ImGui::CalcTextSize("Use this path").x - ImGui::GetStyle().FramePadding.x * 2 -
                            ImGui::GetStyle().ItemSpacing.x);
    bool enter = ImGui::InputText("##typed", g.typed, sizeof(g.typed), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Use this path") || enter) && g.typed[0]) {
        fprintf(stderr, "[SETUP-UI] button: Use this path\n");
        choose_utf8(g.typed, "typed path");
    }
    ImGui::Spacing();
    if (g.files_ok && (g.mode == SETUP_UI_RECOVERY || g.mode == SETUP_UI_REPLACE)) {
        if (big(g.mode == SETUP_UI_REPLACE ? "Back to the game (keep the current files)" :
                "Start the game with the current files", cw))
            g.result = SETUP_UI_START_ANYWAY;
    } else if (g.mode == SETUP_UI_BROKEN && strncmp(g.why, "The game files aren't set up", 28) &&
               !strstr(g.why, "default.xbe (the game's program) is missing")) {
        if (ImGui::Button("Start anyway (it may not work)", ImVec2(cw, 0)))
            g.result = SETUP_UI_START_ANYWAY;
    }
    if (big("Quit", cw))
        g.result = SETUP_UI_QUIT;
    (void)press;
}

static void work(float cw, unsigned press, const setup_status_t *st)
{
    char a[64], b[64], t[64], l[600];

    if (st->state != g.last_state) {
        static const char *const n[] = { "idle", "identifying", "identified", "copying", "verifying", "swapping",
                                         "done", "failed", "cancelled" };
        fprintf(stderr, "[SETUP-UI] job: %s\n", n[st->state]);
        g.last_state = st->state;
        g.focus = 1;
        g.pad_fresh = 1;
        if (st->state == SETUP_DONE)
            g.done_at = GetTickCount64();
    }
    if (st->source_utf8[0]) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw);
        ImGui::TextColored(k_dim, "From: %s", st->source_utf8);
        if (st->layout[0])
            ImGui::TextColored(k_dim, "It is %s.", st->layout);
        if (st->found[0])
            ImGui::TextColored(st->found_ok ? k_green : k_red, "Found: %s%s", st->found,
                               st->found_ok ? ": the supported disc" : "");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    }
    switch (st->state) {
    case SETUP_IDENTIFYING:
        ImGui::TextUnformatted("Reading it...");
        break;
    case SETUP_IDENTIFIED:
        ImGui::TextUnformatted("Checking...");
        break;
    case SETUP_COPYING: case SETUP_VERIFYING: case SETUP_SWAPPING: {
        float frac = st->bytes_total ? (float)((double)st->bytes_done / (double)st->bytes_total) : 0.0f;
        ImGui::TextUnformatted(st->state == SETUP_COPYING ? (st->in_place ? "Checking your game_files folder (1 of 1)"
                               : "Copying the game files (step 1 of 2)") : st->state == SETUP_VERIFYING ?
                               (st->verify_only ? "Checking the game files" : st->in_place ?
                               "Checking your game_files folder" : "Checking the copy, file by file (step 2 of 2)") :
                               "Putting the files in place...");
        snprintf(a, sizeof(a), "%.0f", (double)st->bytes_done / 1048576.0);
        snprintf(b, sizeof(b), "%.0f", (double)st->bytes_total / 1048576.0);
        snprintf(l, sizeof(l), "%s of %s MB", a, b);
        ImGui::ProgressBar(st->state == SETUP_SWAPPING ? 1.0f : frac, ImVec2(cw, ImGui::GetFrameHeight() * 1.4f), l);
        if (st->state != SETUP_SWAPPING) {
            fmt_time(st->eta_s, t, sizeof(t));
            ImGui::Text("%u of %u files  -  %.0f MB/s  -  %s", st->files_done, st->files_total, st->mb_s, t);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw);
            ImGui::TextColored(k_dim, "%s", st->current);
            ImGui::PopTextWrapPos();
        }
        ImGui::Spacing();
        focus_here();
        if (big("Cancel", cw) || (press & OVL_PAD_B) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            twoc_setup_cancel();
        break;
    }
    case SETUP_DONE: {
        double left = 2.0 - (double)(GetTickCount64() - g.done_at) / 1000.0;
        ImGui::TextColored(k_green, "Game files ready.");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw);
        ImGui::TextUnformatted(st->note);
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        snprintf(l, sizeof(l), "Start the game (in %.0f s)", left > 0 ? ceil(left) : 0.0);
        focus_here();
        if (big(l, cw) || left <= 0)
            g.result = SETUP_UI_READY;
        break;
    }
    case SETUP_FAILED: case SETUP_CANCELLED:
        if (st->state == SETUP_FAILED) {
            ImGui::TextColored(k_red, "The game files couldn't be set up.");
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw);
            ImGui::TextUnformatted(st->why);
            if (st->hint[0]) {
                ImGui::Spacing();
                ImGui::TextColored(k_amber, "What to try: %s", st->hint);
            }
        } else {
            ImGui::TextColored(k_amber, "Cancelled.");
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw);
        }
        if (st->note[0]) {
            ImGui::Spacing();
            ImGui::TextColored(k_dim, "%s", st->note);
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        focus_here();
        if (big("Retry", cw)) {
            wchar_t s[SETUP_PATH_MAX];
            wcscpy_s(s, SETUP_PATH_MAX, st->source);
            choose(s, "retry");
        }
        if (big("Choose another...", cw) || (press & OVL_PAD_B) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            twoc_setup_reset();
            screen(SCR_HOME, NULL);
        }
        if (big("Quit", cw))
            g.result = SETUP_UI_QUIT;
        break;
    }
}

static void confirm(float cw, unsigned press, const setup_status_t *st)
{
    ImGui::TextColored(k_amber, "Replace the current game files?");
    ImGui::Spacing();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw);
    ImGui::Text("A game_files folder is already there (%s). The files from %s will be copied and checked first; only "
                "then are the current ones moved aside to game_files.old (deleted at the next start), and your saves "
                "and settings move into the new folder.", g.files_ok ? "it passes the check" : "it fails the check",
                st->source_utf8);
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    focus_here();                       /* the safe answer first, as the F1 menu's questions */
    if (big("Cancel##confirm", cw) || (press & OVL_PAD_B) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        twoc_setup_reset();
        screen(SCR_HOME, "replace declined");
        return;
    }
    if (big("Replace them", cw)) {
        fprintf(stderr, "[SETUP-UI] replace confirmed\n");
        if (twoc_setup_install(1) == 0) {
            g.installing = 1;
            screen(SCR_WORK, "replacing");
        }
    }
}

static void frame(void)
{
    ImGuiIO &io = ImGui::GetIO();
    setup_status_t st;
    unsigned press;
    float scale, cw, fit;
    char fb[96];

    io.DisplaySize = ImVec2((float)g.w, (float)g.h);
    scale = g.dpi > 0 ? g.dpi : 1.0f;
    fit = g.w / (980.0f * scale) < g.h / (720.0f * scale) ? g.w / (980.0f * scale) : g.h / (720.0f * scale);
    if (fit < 1.0f)
        scale *= fit < 0.55f ? 0.55f : fit;
    {
        ImGuiStyle &s = ImGui::GetStyle();
        s = g.base;
        s.ScaleAllSizes(scale);
        s.FontScaleDpi = scale;
    }
    ImGui_ImplDX11_NewFrame();
    press = pad_poll();
    ImGui::NewFrame();
    twoc_setup_status(&st);

    /* The flow: a choice read, then copied (after the question when game_files is there). */
    if (st.state == SETUP_IDENTIFIED && st.found_ok && g.screen == SCR_WORK && !g.installing) {
        if (st.replace)
            screen(SCR_CONFIRM, "game_files is there");
        else if (twoc_setup_install(0) == 0)
            g.installing = 1;
        else
            screen(SCR_HOME, "install couldn't start");
    }
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##setup", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    cw = io.DisplaySize.x - 2.0f * ImGui::GetStyle().WindowPadding.x;
    if (cw > 860.0f * scale) {
        cw = 860.0f * scale;
        ImGui::SetCursorPosX((io.DisplaySize.x - cw) * 0.5f);
    }
    {
        float x0 = ImGui::GetCursorPosX();
        ImGui::BeginGroup();
        ImGui::SetWindowFontScale(1.45f);
        ImGui::TextUnformatted("Crash Bandicoot: The Wrath of Cortex");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextColored(k_dim, "Crash TWOC, an unofficial PC port: first-run setup");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::SetCursorPosX(x0);
        if (g.screen == SCR_HOME)
            home(cw, press);
        else if (g.screen == SCR_CONFIRM)
            confirm(cw, press, &st);
        else
            work(cw, press, &st);
        ImGui::Spacing();
        ImGui::Separator();
        if (GetTickCount64() - g.free_at > 2000) {
            ULARGE_INTEGER fr;
            g.free_at = GetTickCount64();
            if (GetDiskFreeSpaceExW(twoc_data_dir(), &fr, NULL, NULL))
                snprintf(g.free_text, sizeof(g.free_text), "%.1f GB free there; the game files need about %.1f GB.",
                         (double)fr.QuadPart / 1073741824.0, (double)(twoc_disc_bytes() + (64ull << 20)) / 1073741824.0);
        }
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw);
        ImGui::TextColored(k_dim, "The game files go in %s\\game_files. %s%s", twoc_data_dir_utf8(), g.free_text,
                           twoc_data_dir_fallback() ? " (The game's own folder can't be written to, so its files, "
                           "settings and saves are kept here instead.)" : "");
        snprintf(fb, sizeof(fb), "%s", g.closing ? "Stopping: cleaning up what the setup made..." : "");
        if (fb[0])
            ImGui::TextColored(k_amber, "%s", fb);
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
    }
    ImGui::End();
    ImGui::Render();
    {
        static const float bg[4] = { 0.06f, 0.06f, 0.08f, 1.0f };
        if (g.rtv) {
            g.ctx->OMSetRenderTargets(1, &g.rtv, NULL);
            g.ctx->ClearRenderTargetView(g.rtv, bg);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
        g.sc->Present(1, 0);
    }
}

/* ── The run ───────────────────────────────────────────────────────────── */

static void dev_drop(void)
{
    const wchar_t *p = _wgetenv(L"RECOMP_DEV_SETUP_DROP");
    wchar_t w[SETUP_PATH_MAX];
    char p8[SETUP_PATH_MAX * 3];
    size_t n;
    HGLOBAL hg;
    if (!p || !*p || g.dev_drop_done || GetTickCount64() - g.t_open < 1000)
        return;
    g.dev_drop_done = 1;
    wcsncpy_s(w, SETUP_PATH_MAX, p, _TRUNCATE);
    u8(w, p8, sizeof(p8));
    n = wcslen(w);
    hg = GlobalAlloc(GHND, sizeof(DROPFILES) + (n + 2) * sizeof(wchar_t));
    if (!hg)
        return;
    {
        DROPFILES *df = (DROPFILES *)GlobalLock(hg);
        df->pFiles = sizeof(DROPFILES);
        df->fWide = TRUE;
        memcpy((char *)df + sizeof(DROPFILES), w, n * sizeof(wchar_t));
        GlobalUnlock(hg);
    }
    fprintf(stderr, "[SETUP-UI] test drop (RECOMP_DEV_SETUP_DROP): %s\n", p8);
    PostMessageW(g.hwnd, WM_DROPFILES, (WPARAM)hg, 0);
}

extern "C" int twoc_setup_ui_run(int mode, const char *why, int files_ok)
{
    static const char *const fonts[] = { "C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\arial.ttf" };
    static const char *const more[] = { "C:\\Windows\\Fonts\\msyh.ttc", "C:\\Windows\\Fonts\\msgothic.ttc",
                                        "C:\\Windows\\Fonts\\seguisym.ttf" };
    typedef DPI_AWARENESS_CONTEXT (WINAPI *set_ctx_t)(DPI_AWARENESS_CONTEXT);
    set_ctx_t set_ctx = (set_ctx_t)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext");
    DPI_AWARENESS_CONTEXT old_ctx = NULL;
    WNDCLASSEXW wc;
    HMONITOR mon;
    MONITORINFO mi;
    RECT r;
    int cx, cy, w, h, co = 0, i;
    unsigned dpi = 96;
    const char *sz = getenv("RECOMP_DEV_SETUP_SIZE");
    HANDLE scan;

    memset(&g, 0, sizeof(g));
    g.mode = mode;
    g.files_ok = files_ok;
    g.result = -1;
    g.pad_fresh = 1;
    snprintf(g.why, sizeof(g.why), "%s", why ? why : "");
    if (set_ctx)
        old_ctx = set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    co = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.lpszClassName = L"CrashTWOCSetup";
    RegisterClassExW(&wc);

    mon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    {
        typedef HRESULT (WINAPI *gdfm_t)(HMONITOR, int, UINT *, UINT *);
        HMODULE shc = LoadLibraryW(L"shcore.dll");
        gdfm_t gdfm = shc ? (gdfm_t)GetProcAddress(shc, "GetDpiForMonitor") : NULL;
        UINT dx = 96, dy = 96;
        if (gdfm && SUCCEEDED(gdfm(mon, 0, &dx, &dy)))
            dpi = dx;
    }
    g.dpi = dpi / 96.0f;
    w = (int)(1280 * g.dpi);
    h = (int)(800 * g.dpi);
    if (w > (mi.rcWork.right - mi.rcWork.left) * 9 / 10)
        w = (mi.rcWork.right - mi.rcWork.left) * 9 / 10;
    if (h > (mi.rcWork.bottom - mi.rcWork.top) * 9 / 10)
        h = (mi.rcWork.bottom - mi.rcWork.top) * 9 / 10;
    if (sz)
        sscanf(sz, "%dx%d", &w, &h);
    r.left = 0;
    r.top = 0;
    r.right = w;
    r.bottom = h;
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    cx = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - (r.right - r.left)) / 2;
    cy = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - (r.bottom - r.top)) / 2;
    if (cy < mi.rcWork.top)
        cy = mi.rcWork.top;
    g.hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, WIN_TITLE, WS_OVERLAPPEDWINDOW, cx, cy,
                             r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    if (!g.hwnd || d3d_make()) {
        fprintf(stderr, "[SETUP-UI] the setup window couldn't be made (%s)\n", g.hwnd ? "no D3D11 device" : "no window");
        d3d_free();
        if (g.hwnd)
            DestroyWindow(g.hwnd);
        if (co)
            CoUninitialize();
        if (set_ctx && old_ctx)
            set_ctx(old_ctx);
        return SETUP_UI_NO_WINDOW;
    }
    GetClientRect(g.hwnd, &r);
    g.w = r.right;
    g.h = r.bottom;
    DragAcceptFiles(g.hwnd, TRUE);
    /* Drops from Explorer reach the window even when the game runs elevated (it shouldn't, but). */
    {
        typedef BOOL (WINAPI *cwmf_t)(HWND, UINT, DWORD, void *);
        cwmf_t cwmf = (cwmf_t)GetProcAddress(GetModuleHandleW(L"user32.dll"), "ChangeWindowMessageFilterEx");
        if (cwmf) {
            cwmf(g.hwnd, WM_DROPFILES, 1 /* MSGFLT_ALLOW */, NULL);
            cwmf(g.hwnd, 0x0049 /* WM_COPYGLOBALDATA */, 1, NULL);
        }
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    {
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = NULL;
        io.LogFilename = NULL;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        for (i = 0; i < (int)(sizeof(fonts) / sizeof(fonts[0])); i++)
            if (GetFileAttributesA(fonts[i]) != INVALID_FILE_ATTRIBUTES && io.Fonts->AddFontFromFileTTF(fonts[i], 18.0f))
                break;
        if (i == (int)(sizeof(fonts) / sizeof(fonts[0])))
            io.Fonts->AddFontDefault();
        else {
            ImFontConfig mc;           /* letters segoeui lacks in a path (CJK, symbols), loaded as needed */
            mc.MergeMode = true;
            for (i = 0; i < (int)(sizeof(more) / sizeof(more[0])); i++)
                if (GetFileAttributesA(more[i]) != INVALID_FILE_ATTRIBUTES)
                    io.Fonts->AddFontFromFileTTF(more[i], 18.0f, &mc);
        }
        ImGui::StyleColorsDark();
        ImGuiStyle &st = ImGui::GetStyle();
        st.WindowRounding = 0.0f;
        st.FrameRounding = 4.0f;
        st.WindowPadding = ImVec2(28, 22);
        st.ItemSpacing = ImVec2(10, 8);
        st.FramePadding = ImVec2(10, 6);
        g.base = st;
    }
    if (!ImGui_ImplDX11_Init(g.dev, g.ctx)) {
        ImGui::DestroyContext();
        d3d_free();
        DestroyWindow(g.hwnd);
        if (co)
            CoUninitialize();
        if (set_ctx && old_ctx)
            set_ctx(old_ctx);
        return SETUP_UI_NO_WINDOW;
    }
    pads_init(pads_mode_parse(getenv("RECOMP_CONTROLLER_INPUT")));
    scan = (HANDLE)_beginthreadex(NULL, 0, scan_thread, NULL, 0, NULL);
    ShowWindow(g.hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(g.hwnd);
    g.t_open = GetTickCount64();
    fprintf(stderr, "[SETUP-UI] setup screen open (%s): %u x %u pixels, DPI %u%%%s%s\n",
            mode == SETUP_UI_FIRST ? "no game files" : mode == SETUP_UI_BROKEN ? "the check failed" :
            mode == SETUP_UI_RECOVERY ? "after a recovery" : "asked for from the F1 menu", g.w, g.h,
            (unsigned)(g.dpi * 100 + 0.5f), files_ok ? "; the current game files pass the check" : "",
            twoc_data_dir_fallback() ? "; data folder in %LOCALAPPDATA%" : "");
    screen(SCR_HOME, NULL);

    while (g.result < 0) {
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        dev_drop();
        if (g.has_drop) {
            g.has_drop = 0;
            if (!twoc_setup_busy() && g.screen != SCR_CONFIRM)
                choose(g.drop, "dropped");
        }
        if (g.closing) {                /* the window's close: a job under way stops and cleans up first */
            if (twoc_setup_busy())
                twoc_setup_cancel();
            else {
                g.result = SETUP_UI_QUIT;
                fprintf(stderr, "[SETUP-UI] the window was closed\n");
                break;
            }
        }
        if (IsIconic(g.hwnd)) {
            Sleep(50);
            continue;
        }
        frame();
    }
    twoc_setup_wait();
    {
        setup_status_t st;
        twoc_setup_status(&st);
        if (g.result == SETUP_UI_READY && st.state == SETUP_DONE)
            fprintf(stderr, "[SETUP-UI] closed: game files ready (%s)\n", st.note);
        else
            fprintf(stderr, "[SETUP-UI] closed: %s\n", g.result == SETUP_UI_QUIT ? "Quit" : g.result ==
                    SETUP_UI_START_ANYWAY ? "start with the current files" : "ready");
    }
    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();
    d3d_free();
    DestroyWindow(g.hwnd);
    UnregisterClassW(L"CrashTWOCSetup", GetModuleHandleW(NULL));
    if (scan) {
        WaitForSingleObject(scan, 3000);
        CloseHandle(scan);
    }
    if (co)
        CoUninitialize();
    if (set_ctx && old_ctx)
        set_ctx(old_ctx);
    return g.result;
}
