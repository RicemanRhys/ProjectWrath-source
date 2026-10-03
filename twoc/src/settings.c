/**
 * Player settings: crash_twoc.ini next to the exe (step 54, notes 18.57).
 * See settings.h for the keys and how they meet the command line.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"

static const struct {
    const char *key, *var;
} g_keys[] = {
    { "fullscreen", "RECOMP_FULLSCREEN" },
    { "widescreen", "RECOMP_WIDESCREEN" },
    { "render_size", "RECOMP_RENDER_SIZE" },
    { "pacing", "RECOMP_PACING" },
    { "anisotropy", "RECOMP_ANISOTROPY" },
    { "volume", "RECOMP_VOLUME" },
};
#define N_KEYS (sizeof(g_keys) / sizeof(g_keys[0]))

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

int twoc_pacing_parse(const char *v)
{
    int i;

    for (i = 0; v && i < TWOC_PACING_COUNT; i++)
        if (_stricmp(v, g_pacing_names[i]) == 0)
            return i;
    return TWOC_PACING_AUTO;
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

void twoc_settings_load(void)
{
    FILE *f = fopen(twoc_settings_path(), "r");
    char line[256];
    int lineno = 0;

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
        if (i == N_KEYS) {
            fprintf(stderr, "[CFG] line %d: unknown key '%s': ignored\n", lineno, k);
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

void twoc_settings_current(twoc_settings_t *s)
{
    const char *v;
    int a;

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
    v = getenv("RECOMP_VOLUME");
    s->volume = v && *v ? atoi(v) : -1;
    if (s->volume > 100)
        s->volume = 100;
    if (v && *v && s->volume < 0)
        s->volume = 0;
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
    else if (strcmp(k, "volume") == 0 && s->volume >= 0)
        snprintf(buf, cap, "%d", s->volume);
}

int twoc_settings_save(const twoc_settings_t *s)
{
    static const char *const notes[N_KEYS] = { "", "0, 1 or W:H", "game (480 lines), WxH or native",
                                               "auto, vsync, limiter or limiter+vsync",
                                               "1 (the game's filtering), 2, 4, 8 or 16",
                                               "0-100, the game's level in Windows' volume mixer" };
    FILE *f = fopen(twoc_settings_path(), "w");
    size_t i;

    if (!f) {
        fprintf(stderr, "[CFG] %s not written\n", twoc_settings_path());
        return -1;
    }
    fprintf(f, "# Crash TWOC settings, written by the settings overlay (Esc in the game window).\n"
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
        fprintf(f, notes[i][0] ? "%s = %s           # %s\n" : "%s = %s\n", g_keys[i].key, v, notes[i]);
    }
    fclose(f);
    fprintf(stderr, "[CFG] settings written to %s\n", twoc_settings_path());
    return 0;
}
