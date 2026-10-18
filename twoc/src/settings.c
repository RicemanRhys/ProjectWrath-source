/**
 * Player settings: crash_twoc.ini next to the exe (step 54, notes 18.57).
 * See settings.h for the keys and how they meet the command line.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "settings.h"
#include "pads.h"

typedef struct {
    const char *key, *var, *note;
} key_t_;

static const key_t_ g_fixed[] = {
    { "fullscreen", "RECOMP_FULLSCREEN", "" },
    { "widescreen", "RECOMP_WIDESCREEN", "the aspect ratio: 0 (4:3), 1 (16:9) or W:H (16:10, 21:9)" },
    { "render_size", "RECOMP_RENDER_SIZE", "game (480 lines), WxH or native" },
    { "pacing", "RECOMP_PACING", "auto, vsync, limiter or limiter+vsync" },
    { "anisotropy", "RECOMP_ANISOTROPY", "1 (the game's filtering), 2, 4, 8 or 16" },
    { "antialiasing", "RECOMP_ANTIALIASING",
      "off, fxaa, smaa, msaa2x, msaa4x, msaa8x, or msaa2x+smaa, msaa4x+smaa, msaa8x+smaa (MSAA, then SMAA)" },
    { "render_scale", "RECOMP_RENDER_SCALE",
      "25-200: percent of the render size the picture is drawn at (under 100 lighter, over 100 supersampled)" },
    { "ao", "RECOMP_AO",
      "off, ssao or gtao: ambient occlusion, corners and contact shadows darkened from the depth (GTAO: Intel's"
      " XeGTAO)" },
    { "ao_strength", "RECOMP_AO_STRENGTH", "0-100: how much of the ambient occlusion is applied" },
    { "ao_half", "RECOMP_AO_HALF", "1: ambient occlusion worked out at half the resolution (cheaper, a little softer)" },
    { "framerate", "RECOMP_FRAMERATE",
      "60 (the game's), display (interpolated frames at the display's rate) or a rate in Hz (90, 120, 144, 165,"
      " 180, 240: interpolated, shown evenly where the display can)" },
    { "adaptive_framerate", "RECOMP_FRAMERATE_ADAPTIVE",
      "1: fewer interpolated frames while game frames run late, 0: always the display's count" },
    { "render_thread", "RECOMP_RENDER_THREAD",
      "auto (on with 4 or more processor cores), on or off: the frames between game frames drawn on a thread of"
      " their own, with a frame rate above 60" },
    { "show_fps", "RECOMP_SHOW_FPS", "1: the game's updates and the frames shown a second in the picture's corner" },
    { "startup_message", "RECOMP_STARTUP_MESSAGE",
      "1: \"Press F1 to open the settings menu\" for the first seconds, 0: not" },
    { "hud", "RECOMP_HUD", "centre (the game's 4:3 places) or edges: the HUD counters at the screen's edges, wider than 4:3" },
    { "hud_margin", "RECOMP_HUD_MARGIN", "0-10: percent of the screen's width the HUD at the edges keeps in from them" },
    { "volume", "RECOMP_VOLUME", "0-100, the game's level in Windows' volume mixer" },
    { "music_volume", "RECOMP_MUSIC_VOLUME", "0-100: music, ambience and speech (the game's streams)" },
    { "effects_volume", "RECOMP_EFFECTS_VOLUME", "0-100: the sound effects" },
    { "speakers", "RECOMP_SPEAKERS", "stereo or 5.1 (from the next start)" },
    { "hrtf", "RECOMP_HRTF", "1: 3D sounds placed left-right (the Xbox's HRTF), 0: centred" },
    { "pause", "RECOMP_OVERLAY_PAUSE", "1: the game waits while the overlay is open, 0: it runs behind it" },
    { "pause_unfocused", "RECOMP_PAUSE_UNFOCUSED",
      "1: the game waits, muted, while its window is in the background or minimised" },
    { "fast_title", "RECOMP_DEV_FAST_TITLE",
      "1: the title animation runs fast, so the main menu opens sooner (from the next start)" },
    { "keyboard", "RECOMP_KEYBOARD", "1: the keyboard plays too (key_ bindings), 0: the controller only" },
    { "controller_input", "RECOMP_CONTROLLER_INPUT",
      "sdl: every pad (SDL3; Xbox pads through XInput), or xinput: XInput only, as before" },
};
#define N_FIXED (sizeof(g_fixed) / sizeof(g_fixed[0]))

/* Keys of settings since removed: an older file's line is skipped without a
 * word, and the next save leaves it out (step 91: step 69's draw distance;
 * step 93: step 91's presets). */
static const char *const g_retired[] = { "draw_distance", "preset" };

/* Step 57: then one key per binding (controls.h), at its default unwritten:
 * pad_<xbox input> for the controller map, key_<binding> for the keyboard;
 * step 70: extra_<pc input> for the extra buttons (paddles, touchpad, ...). */
#define K_PAD N_FIXED
#define K_KEY (K_PAD + XIN_COUNT)
#define K_EXTRA (K_KEY + KIN_COUNT)
#define N_KEYS (K_EXTRA + SRC_EXTRAS)
static key_t_ g_keys[N_KEYS];
static char g_key_names[N_KEYS - N_FIXED][2][40];

static void keys_init(void)
{
    size_t i, j;

    if (g_keys[0].key)
        return;
    for (i = 0; i < N_FIXED; i++)
        g_keys[i] = g_fixed[i];
    for (i = N_FIXED; i < N_KEYS; i++) {
        char *k = g_key_names[i - N_FIXED][0], *v = g_key_names[i - N_FIXED][1];
        if (i < K_KEY)
            snprintf(k, 40, "pad_%s", controls_xin_ini((int)(i - K_PAD)));
        else if (i < K_EXTRA)
            snprintf(k, 40, "key_%s", controls_kin_ini((int)(i - K_KEY)));
        else
            snprintf(k, 40, "extra_%s", controls_extra_ini((int)(i - K_EXTRA) + XIN_COUNT));
        snprintf(v, 40, "RECOMP_%s", k);
        for (j = 0; v[j]; j++)
            v[j] = (char)toupper((unsigned char)v[j]);
        g_keys[i].key = k;
        g_keys[i].var = v;
        g_keys[i].note = "";
    }
}

/* A binding's value, checked: 0 if it reads. */
static int binding_ok(size_t i, const char *v)
{
    twoc_controls_t c;

    if (i < K_PAD)
        return 0;
    controls_default_pad(&c);
    controls_default_keys(&c);
    return i < K_KEY     ? controls_parse_pad(v, (int)(i - K_PAD), &c)
           : i < K_EXTRA ? controls_parse_keys(v, (int)(i - K_KEY), &c)
                         : controls_parse_extra(v, (int)(i - K_EXTRA) + XIN_COUNT, &c);
}

/* Keys the command line overrode: the file's own value, which a save keeps
 * unless the player changed that setting in the overlay. g_start is the
 * settings in effect at the start, to tell. */
static char g_file_val[N_KEYS][64];
static int g_overridden[N_KEYS];
static twoc_settings_t g_start;

static const char *g_pacing_names[TWOC_PACING_COUNT] = { "auto", "vsync", "limiter", "limiter+vsync" };

const char *twoc_pacing_name(int pacing)
{
    return pacing >= 0 && pacing < TWOC_PACING_COUNT ? g_pacing_names[pacing] : "auto";
}

/* Step 77: framerate = 60 | display | N. 0 the game's 60 (also unset or
 * anything unknown), 1 the display's rate, N > 60 a rate in Hz. */
int twoc_framerate_parse(const char *v)
{
    int n;

    if (!v || !*v)
        return 0;
    if (_stricmp(v, "display") == 0)
        return 1;
    n = atoi(v);
    return n > 60 ? (n > 1000 ? 1000 : n) : 0;
}

void twoc_framerate_text(int fr, char *buf, size_t cap)
{
    if (fr >= 2)
        snprintf(buf, cap, "%d", fr);
    else
        snprintf(buf, cap, "%s", fr ? "display" : "60");
}

int twoc_pacing_parse(const char *v)
{
    int i;

    for (i = 0; v && i < TWOC_PACING_COUNT; i++)
        if (_stricmp(v, g_pacing_names[i]) == 0)
            return i;
    return TWOC_PACING_AUTO;
}

static const char *g_aa_names[TWOC_AA_COUNT] = { "off", "fxaa", "smaa", "msaa2x", "msaa4x", "msaa8x",
                                                 "msaa2x+smaa", "msaa4x+smaa", "msaa8x+smaa" };

const char *twoc_aa_name(int aa)
{
    return aa >= 0 && aa < TWOC_AA_COUNT ? g_aa_names[aa] : "off";
}

int twoc_aa_parse(const char *v)
{
    int i;

    for (i = 0; v && i < TWOC_AA_COUNT; i++)    /* msaa4 reads as msaa4x */
        if (_stricmp(v, g_aa_names[i]) == 0 ||
            (i >= TWOC_AA_MSAA2 && i <= TWOC_AA_MSAA8 && _strnicmp(v, g_aa_names[i], 5) == 0 && v[5] == 0))
            return i;
    return TWOC_AA_OFF;
}

/* Step 92: ambient occlusion. */
static const char *const g_ao_names[TWOC_AO_COUNT] = { "off", "ssao", "gtao" };

const char *twoc_ao_name(int ao)
{
    return ao >= 0 && ao < TWOC_AO_COUNT ? g_ao_names[ao] : "off";
}

int twoc_ao_parse(const char *v)
{
    int i;

    for (i = 0; v && i < TWOC_AO_COUNT; i++)
        if (_stricmp(v, g_ao_names[i]) == 0)
            return i;
    return TWOC_AO_OFF;
}

int twoc_ao_strength_parse(const char *v)
{
    int p;

    if (!v || sscanf(v, "%d", &p) != 1)
        return TWOC_AO_STRENGTH_DEFAULT;
    return p < 0 ? 0 : p > 100 ? 100 : p;
}

unsigned twoc_aa_samples(int aa)
{
    return aa == TWOC_AA_MSAA2 || aa == TWOC_AA_MSAA2_SMAA ? 2 : aa == TWOC_AA_MSAA4 || aa == TWOC_AA_MSAA4_SMAA ? 4
           : aa == TWOC_AA_MSAA8 || aa == TWOC_AA_MSAA8_SMAA ? 8 : 1;
}

int twoc_aa_post(int aa)
{
    return aa == TWOC_AA_FXAA ? TWOC_AA_FXAA
           : aa == TWOC_AA_SMAA || aa >= TWOC_AA_MSAA2_SMAA ? TWOC_AA_SMAA : TWOC_AA_OFF;
}

int twoc_render_scale_parse(const char *v)
{
    int p;

    if (!v || !*v)
        return 100;
    p = atoi(v);
    return p <= 0 ? 100 : p < TWOC_RENDER_SCALE_MIN ? TWOC_RENDER_SCALE_MIN
           : p > TWOC_RENDER_SCALE_MAX ? TWOC_RENDER_SCALE_MAX : p;
}

int twoc_render_size_parse(const char *v, unsigned *pw, unsigned *ph)
{
    unsigned w = 0, h = 0;

    *pw = *ph = 0;
    if (!v || !*v || strcmp(v, "0") == 0)
        return 0;
    if (_stricmp(v, "native") == 0) {
        DEVMODEA dm;
        memset(&dm, 0, sizeof(dm));
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm)) {
            w = dm.dmPelsWidth;
            h = dm.dmPelsHeight;
        }
    } else if (sscanf(v, "%ux%u", &w, &h) != 2 && sscanf(v, "%uX%u", &w, &h) != 2) {
        w = h = 0;
    }
    if (w < TWOC_RENDER_SIZE_MIN || h < TWOC_RENDER_SIZE_MIN || w > TWOC_RENDER_SIZE_MAX || h > TWOC_RENDER_SIZE_MAX)
        return -1;
    *pw = w;
    *ph = h;
    return 0;
}

void twoc_render_fit(unsigned rw, unsigned rh, unsigned gw, unsigned gh, double wide, unsigned *w, unsigned *h)
{
    double r = wide > 0 ? wide : (double)gw / (double)gh;

    *w = gw;
    *h = gh;
    if (!rw) {
        if (wide > 0)
            *w = 2 * (unsigned)(gh * r / 2.0 + 0.5);
        return;
    }
    if ((double)rw / rh > r) {
        *h = rh;
        *w = 2 * (unsigned)(rh * r / 2.0 + 0.5);
    } else {
        *w = rw & ~1u;
        *h = (unsigned)(rw / r + 0.5);
    }
}

void twoc_render_scaled(unsigned vw, unsigned vh, int pct, unsigned *w, unsigned *h)
{
    double k = pct / 100.0, f;

    if (pct == 100 || !vw || !vh) {
        *w = vw;
        *h = vh;
        return;
    }
    *w = 2 * (unsigned)(vw * k / 2.0 + 0.5);
    *h = (unsigned)(vh * k + 0.5);
    if (*w > TWOC_RENDER_SCALE_MAX_TEX || *h > TWOC_RENDER_SCALE_MAX_TEX) {
        f = (double)TWOC_RENDER_SCALE_MAX_TEX / *w < (double)TWOC_RENDER_SCALE_MAX_TEX / *h
                ? (double)TWOC_RENDER_SCALE_MAX_TEX / *w : (double)TWOC_RENDER_SCALE_MAX_TEX / *h;
        *w = 2 * (unsigned)(*w * f / 2.0);
        *h = (unsigned)(*h * f);
    }
    *w = *w < 16 ? 16 : *w;
    *h = *h < 16 ? 16 : *h;
}

/* Step 79: render_thread = off | on | auto (1 and 0 read too; sync, a check
 * in d3d_host.c, is on here). */
static const char *g_rt_names[TWOC_RT_COUNT] = { "off", "on", "auto" };

const char *twoc_rt_name(int rt)
{
    return rt >= 0 && rt < TWOC_RT_COUNT ? g_rt_names[rt] : "off";
}

int twoc_rt_parse(const char *v)
{
    if (!v)
        return TWOC_RT_OFF;
    if (_stricmp(v, "on") == 0 || strcmp(v, "1") == 0 || _stricmp(v, "sync") == 0)
        return TWOC_RT_ON;
    if (_stricmp(v, "auto") == 0)
        return TWOC_RT_AUTO;
    return TWOC_RT_OFF;
}

const char *twoc_settings_path(void)
{
    static char path[MAX_PATH + 32];
    char *slash;

    if (!path[0]) {
        DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            snprintf(path, sizeof(path), "crash_twoc.ini");
        else if ((slash = strrchr(path, '\\')) != NULL)
            snprintf(slash + 1, sizeof(path) - (size_t)(slash + 1 - path), "crash_twoc.ini");
    }
    return path;
}

static char *trim(char *s)
{
    char *e;

    while (*s == ' ' || *s == '\t')
        s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        *--e = 0;
    return s;
}

static int retired(const char *k)
{
    size_t i;

    for (i = 0; i < sizeof(g_retired) / sizeof(g_retired[0]); i++)
        if (_stricmp(k, g_retired[i]) == 0)
            return 1;
    return 0;
}

void twoc_settings_load(void)
{
    FILE *f = fopen(twoc_settings_path(), "r");
    char line[256];
    int lineno = 0;

    keys_init();
    {   /* step 75: step 74's test switch is the setting's old name */
        const char *old = getenv("RECOMP_D3D_BRIDGE_INTERP_ADAPT");
        if (old && *old && !getenv("RECOMP_FRAMERATE_ADAPTIVE"))
            _putenv_s("RECOMP_FRAMERATE_ADAPTIVE", old);
    }
    if (!f)
        return;                         /* no file: nothing set, the run as before */
    fprintf(stderr, "[CFG] settings from %s\n", twoc_settings_path());
    while (fgets(line, sizeof(line), f)) {
        char *k = trim(line), *v, *c;
        size_t i;
        lineno++;
        if (!*k || *k == '#' || *k == ';' || *k == '[')
            continue;
        if ((c = strchr(k, '#')) != NULL || (c = strchr(k, ';')) != NULL)
            *c = 0;
        if ((v = strchr(k, '=')) == NULL) {
            fprintf(stderr, "[CFG] line %d: no '=': ignored\n", lineno);
            continue;
        }
        *v++ = 0;
        k = trim(k);
        v = trim(v);
        for (i = 0; i < N_KEYS && _stricmp(k, g_keys[i].key) != 0; i++)
            ;
        if (i == N_KEYS && retired(k))
            continue;
        if (i == N_KEYS) {
            fprintf(stderr, "[CFG] line %d: unknown key '%s': ignored\n", lineno, k);
            continue;
        }
        if (binding_ok(i, v) != 0) {
            fprintf(stderr, "[CFG] line %d: %s = %s: not a binding this knows: ignored\n", lineno, k, v);
            continue;
        }
        if (strcmp(g_keys[i].key, "render_size") == 0 && _stricmp(v, "game") == 0)
            v = "0";                    /* RECOMP_RENDER_SIZE's own word for the guest's size */
        if (getenv(g_keys[i].var)) {
            fprintf(stderr, "[CFG] %s = %s: %s=%s from the command line wins\n", g_keys[i].key, v, g_keys[i].var,
                    getenv(g_keys[i].var));
            snprintf(g_file_val[i], sizeof(g_file_val[i]), "%s", strcmp(v, "0") == 0 &&
                     strcmp(g_keys[i].key, "render_size") == 0 ? "game" : v);
            g_overridden[i] = 1;
            continue;
        }
        _putenv_s(g_keys[i].var, v);
        fprintf(stderr, "[CFG] %s = %s\n", g_keys[i].key, v);
    }
    fclose(f);
    twoc_settings_current(&g_start);
}

static int env_on(const char *name)
{
    const char *v = getenv(name);
    return v && *v && strcmp(v, "0") != 0;
}

/* A 0-100 level from `name`, -1 when unset. */
static int env_percent(const char *name)
{
    const char *v = getenv(name);
    int p;

    if (!v || !*v)
        return -1;
    p = atoi(v);
    return p < 0 ? 0 : p > 100 ? 100 : p;
}

void twoc_settings_current(twoc_settings_t *s)
{
    const char *v;
    int a;
    size_t i;

    memset(s, 0, sizeof(*s));
    s->fullscreen = env_on("RECOMP_FULLSCREEN");
    s->wide_w = 16.0;
    s->wide_h = 9.0;
    if (env_on("RECOMP_WIDESCREEN")) {
        double w, h;
        v = getenv("RECOMP_WIDESCREEN");
        s->widescreen = 1;
        if (strchr(v, ':') && sscanf(v, "%lf:%lf", &w, &h) == 2 && w > 0 && h > 0) {
            s->wide_w = w;
            s->wide_h = h;
        }
    }
    v = getenv("RECOMP_RENDER_SIZE");
    if (v && *v && strcmp(v, "0") != 0)
        snprintf(s->render_size, sizeof(s->render_size), "%s", v);
    s->pacing = twoc_pacing_parse(getenv("RECOMP_PACING"));
    v = getenv("RECOMP_ANISOTROPY");
    a = v ? atoi(v) : 1;
    s->anisotropy = a >= 16 ? 16 : a >= 8 ? 8 : a >= 4 ? 4 : a >= 2 ? 2 : 1;
    s->antialiasing = twoc_aa_parse(getenv("RECOMP_ANTIALIASING"));
    s->framerate = twoc_framerate_parse(getenv("RECOMP_FRAMERATE"));
    v = getenv("RECOMP_FRAMERATE_ADAPTIVE");
    s->adaptive = !(v && strcmp(v, "0") == 0);
    s->render_thread = twoc_rt_parse(getenv("RECOMP_RENDER_THREAD"));
    s->show_fps = env_on("RECOMP_SHOW_FPS");
    v = getenv("RECOMP_STARTUP_MESSAGE");
    s->startup_message = !(v && strcmp(v, "0") == 0);
    v = getenv("RECOMP_HUD");
    s->hud = v && _stricmp(v, "edges") == 0;
    v = getenv("RECOMP_HUD_MARGIN");
    s->hud_margin = v ? atoi(v) : 0;
    s->hud_margin = s->hud_margin < 0 ? 0 : s->hud_margin > 10 ? 10 : s->hud_margin;
    v = getenv("RECOMP_VOLUME");
    s->volume = v && *v ? atoi(v) : -1;
    if (s->volume > 100)
        s->volume = 100;
    if (v && *v && s->volume < 0)
        s->volume = 0;
    s->music = env_percent("RECOMP_MUSIC_VOLUME");
    s->effects = env_percent("RECOMP_EFFECTS_VOLUME");
    v = getenv("RECOMP_SPEAKERS");
    s->speakers = v && *v ? strcmp(v, "5.1") == 0 : -1;
    v = getenv("RECOMP_HRTF");
    s->hrtf = !(v && strcmp(v, "0") == 0);
    v = getenv("RECOMP_OVERLAY_PAUSE");
    s->pause = !(v && strcmp(v, "0") == 0);
    v = getenv("RECOMP_KEYBOARD");
    s->keyboard = !(v && strcmp(v, "0") == 0);
    s->controller_input = pads_mode_parse(getenv("RECOMP_CONTROLLER_INPUT"));
    s->render_scale = twoc_render_scale_parse(getenv("RECOMP_RENDER_SCALE"));     /* step 90 */
    s->ao = twoc_ao_parse(getenv("RECOMP_AO"));                                   /* step 92 */
    s->ao_strength = twoc_ao_strength_parse(getenv("RECOMP_AO_STRENGTH"));
    s->ao_half = env_on("RECOMP_AO_HALF");
    s->pause_unfocused = env_on("RECOMP_PAUSE_UNFOCUSED");
    s->fast_title = env_on("RECOMP_DEV_FAST_TITLE");
    keys_init();
    controls_default_pad(&s->controls);
    controls_default_keys(&s->controls);
    for (i = K_PAD; i < N_KEYS; i++)
        if ((v = getenv(g_keys[i].var)) != NULL && *v) {
            if (i < K_KEY)
                controls_parse_pad(v, (int)(i - K_PAD), &s->controls);
            else if (i < K_EXTRA)
                controls_parse_keys(v, (int)(i - K_KEY), &s->controls);
            else
                controls_parse_extra(v, (int)(i - K_EXTRA) + XIN_COUNT, &s->controls);
        }
}

void twoc_settings_display_defaults(twoc_settings_t *s)
{
    s->render_size[0] = 0;              /* each as twoc_settings_current reads it unset */
    s->render_scale = 100;
    s->pacing = TWOC_PACING_AUTO;
    s->framerate = 0;
    s->adaptive = 1;
    s->render_thread = TWOC_RT_OFF;
    s->show_fps = 0;
    s->startup_message = 1;
    s->fast_title = 0;
    s->anisotropy = 1;
    s->antialiasing = TWOC_AA_OFF;
    s->ao = TWOC_AO_OFF;
    s->ao_strength = TWOC_AO_STRENGTH_DEFAULT;
    s->ao_half = 0;
    s->hud = 0;
    s->hud_margin = 0;
}

/* Key i's value in `s` as the file writes it ("" for a volume never set). */
static void key_text(const twoc_settings_t *s, size_t i, char *buf, size_t cap)
{
    const char *k = g_keys[i].key;

    buf[0] = 0;
    if (strcmp(k, "fullscreen") == 0)
        snprintf(buf, cap, "%d", s->fullscreen ? 1 : 0);
    else if (strcmp(k, "widescreen") == 0 && !s->widescreen)
        snprintf(buf, cap, "0");
    else if (strcmp(k, "widescreen") == 0 && s->wide_w == 16.0 && s->wide_h == 9.0)
        snprintf(buf, cap, "1");
    else if (strcmp(k, "widescreen") == 0)
        snprintf(buf, cap, "%g:%g", s->wide_w, s->wide_h);
    else if (strcmp(k, "render_size") == 0)
        snprintf(buf, cap, "%s", s->render_size[0] ? s->render_size : "game");
    else if (strcmp(k, "pacing") == 0)
        snprintf(buf, cap, "%s", twoc_pacing_name(s->pacing));
    else if (strcmp(k, "anisotropy") == 0)
        snprintf(buf, cap, "%d", s->anisotropy);
    else if (strcmp(k, "antialiasing") == 0)
        snprintf(buf, cap, "%s", twoc_aa_name(s->antialiasing));
    else if (strcmp(k, "framerate") == 0)
        twoc_framerate_text(s->framerate, buf, cap);
    else if (strcmp(k, "adaptive_framerate") == 0)
        snprintf(buf, cap, "%d", s->adaptive ? 1 : 0);
    else if (strcmp(k, "render_thread") == 0 && s->render_thread != TWOC_RT_OFF)
        snprintf(buf, cap, "%s", twoc_rt_name(s->render_thread));      /* off, the default for now: unwritten */
    else if (strcmp(k, "show_fps") == 0 && s->show_fps)
        snprintf(buf, cap, "1");        /* step 82: off, the default: unwritten */
    else if (strcmp(k, "startup_message") == 0 && !s->startup_message)
        snprintf(buf, cap, "0");        /* step 86: on, the default: unwritten */
    else if (strcmp(k, "hud") == 0)
        snprintf(buf, cap, "%s", s->hud ? "edges" : "centre");
    else if (strcmp(k, "hud_margin") == 0)
        snprintf(buf, cap, "%d", s->hud_margin);
    else if (strcmp(k, "volume") == 0 && s->volume >= 0)
        snprintf(buf, cap, "%d", s->volume);
    else if (strcmp(k, "music_volume") == 0 && s->music >= 0)
        snprintf(buf, cap, "%d", s->music);
    else if (strcmp(k, "effects_volume") == 0 && s->effects >= 0)
        snprintf(buf, cap, "%d", s->effects);
    else if (strcmp(k, "speakers") == 0 && s->speakers >= 0)
        snprintf(buf, cap, "%s", s->speakers ? "5.1" : "stereo");
    else if (strcmp(k, "hrtf") == 0)
        snprintf(buf, cap, "%d", s->hrtf ? 1 : 0);
    else if (strcmp(k, "pause") == 0)
        snprintf(buf, cap, "%d", s->pause ? 1 : 0);
    else if (strcmp(k, "keyboard") == 0)
        snprintf(buf, cap, "%d", s->keyboard ? 1 : 0);
    else if (strcmp(k, "render_scale") == 0 && s->render_scale != 100)
        snprintf(buf, cap, "%d", s->render_scale);     /* step 90: 100, 1 and 0, the defaults: unwritten */
    else if (strcmp(k, "ao") == 0 && s->ao != TWOC_AO_OFF)
        snprintf(buf, cap, "%s", twoc_ao_name(s->ao));     /* step 92: off, the default: unwritten */
    else if (strcmp(k, "ao_strength") == 0 && s->ao_strength != TWOC_AO_STRENGTH_DEFAULT)
        snprintf(buf, cap, "%d", s->ao_strength);
    else if (strcmp(k, "ao_half") == 0 && s->ao_half)
        snprintf(buf, cap, "1");        /* off, the default: unwritten */
    else if (strcmp(k, "pause_unfocused") == 0 && s->pause_unfocused)
        snprintf(buf, cap, "1");
    else if (strcmp(k, "fast_title") == 0 && s->fast_title)
        snprintf(buf, cap, "1");
    else if (strcmp(k, "controller_input") == 0 && s->controller_input != PADS_SDL)
        snprintf(buf, cap, "%s", pads_mode_name(s->controller_input));     /* sdl, the default: unwritten */
    else if (i >= K_PAD) {              /* a binding: written only when it isn't the default */
        twoc_controls_t d;
        char def[64];
        controls_default_pad(&d);
        controls_default_keys(&d);
        if (i < K_KEY) {
            controls_pad_text(&s->controls, (int)(i - K_PAD), buf, (unsigned)cap);
            controls_pad_text(&d, (int)(i - K_PAD), def, sizeof(def));
        } else if (i < K_EXTRA) {
            controls_keys_text(&s->controls, (int)(i - K_KEY), buf, (unsigned)cap);
            controls_keys_text(&d, (int)(i - K_KEY), def, sizeof(def));
        } else {
            controls_extra_text(&s->controls, (int)(i - K_EXTRA) + XIN_COUNT, buf, (unsigned)cap);
            controls_extra_text(&d, (int)(i - K_EXTRA) + XIN_COUNT, def, sizeof(def));
        }
        if (strcmp(buf, def) == 0)
            buf[0] = 0;
    }
}

int twoc_settings_save(const twoc_settings_t *s)
{
    FILE *f = fopen(twoc_settings_path(), "w");
    size_t i;
    int bindings = 0;

    keys_init();
    if (!f) {
        fprintf(stderr, "[CFG] %s not written\n", twoc_settings_path());
        return -1;
    }
    fprintf(f, "# Crash TWOC settings, written by the settings overlay (F1 in the game window).\n"
               "# run_twoc.ps1 switches and RECOMP_* variables win over these. Delete the file for the defaults.\n");
    for (i = 0; i < N_KEYS; i++) {
        char now[64], then[64];
        const char *v = now;
        key_text(s, i, now, sizeof(now));
        key_text(&g_start, i, then, sizeof(then));
        if (g_overridden[i] && strcmp(now, then) == 0)
            v = g_file_val[i];          /* this run's command line, not the player's choice */
        if (!*v)
            continue;
        if (i >= K_PAD && !bindings++)
            fprintf(f, "# Controls changed from the defaults (F1, Controls): pad_<Xbox input> = <PC controller input>,\n"
                       "# key_<Xbox input> = up to two keys, or none; extra_<PC controller input> = <Xbox button> or none.\n");
        fprintf(f, g_keys[i].note[0] ? "%s = %s           # %s\n" : "%s = %s\n", g_keys[i].key, v, g_keys[i].note);
    }
    fclose(f);
    fprintf(stderr, "[CFG] settings written to %s\n", twoc_settings_path());
    return 0;
}
