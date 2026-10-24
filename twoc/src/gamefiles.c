/**
 * The game files at the start (step 83, notes 18.86; step 107, notes 18.110).
 *
 * The exe runs the code of one default.xbe (the USA Xbox disc's) and reads
 * the game's data from Crashdat beside it, both in game_files in the data
 * folder (setup.h). A missing or different file used to fail without a word
 * (a missing default.xbe: a box naming another folder; a missing Crashdat
 * file: whatever the game does when a load fails). This checks them before
 * anything runs: default.xbe by size and SHA-256, every one of the disc's
 * files by its presence and size (step 83 counted the files in each folder;
 * step 107 has the disc's manifest, twoc/data/disc_usa.tsv), and says what is
 * wrong. Since step 107 the setup screen (setup_ui.cpp) shows it and offers
 * to set the files up; step 83's message boxes remain for when it can't.
 *
 * The expected values are the disc's: its 2,267 files are default.xbe and
 * Crashdat's 2,266. No game data, only sizes and hashes.
 */

#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gamefiles.h"
#include "setup.h"

#define GF_DIR          L"game_files"

/* Every file under `dir` (relative path `rel`, '/' between names), marked in `seen` when it is a disc file;
 * sizes compared. Extra files counted. */
typedef struct {
    unsigned char *seen;
    unsigned files, extra, wrong;
    char extra_names[256], wrong_names[300];
} walk_t;

static void walk(const wchar_t *dir, const char *rel, walk_t *w)
{
    wchar_t pat[MAX_PATH * 2 + 4], sub[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    _snwprintf_s(pat, sizeof(pat) / sizeof(pat[0]), _TRUNCATE, L"%s\\*", dir);
    h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        char n8[600], r[1200];
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
            continue;
        if (!WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, n8, sizeof(n8), NULL, NULL))
            continue;
        snprintf(r, sizeof(r), "%s/%s", rel, n8);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            _snwprintf_s(sub, sizeof(sub) / sizeof(sub[0]), _TRUNCATE, L"%s\\%s", dir, fd.cFileName);
            walk(sub, r, w);
        } else {
            int i = twoc_disc_find(r);
            w->files++;
            if (i < 0) {
                w->extra++;
                if (strlen(w->extra_names) + strlen(r) + 4 < sizeof(w->extra_names))
                    snprintf(w->extra_names + strlen(w->extra_names), sizeof(w->extra_names) -
                             strlen(w->extra_names), "%s%s", w->extra_names[0] ? ", " : "", r);
            } else {
                const char *p, *sha;
                uint32_t size;
                twoc_disc_entry((unsigned)i, &p, &size, &sha);
                w->seen[i] = 1;
                if ((((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow) != size) {
                    w->wrong++;
                    if (strlen(w->wrong_names) + strlen(r) + 4 < sizeof(w->wrong_names))
                        snprintf(w->wrong_names + strlen(w->wrong_names), sizeof(w->wrong_names) -
                                 strlen(w->wrong_names), "%s%s", w->wrong_names[0] ? ", " : "", r);
                }
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* SHA-256 of a file as lowercase hex; 0 if it couldn't be read. */
static int sha256_file(const wchar_t *path, char hex[65], unsigned long long *size)
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hh = NULL;
    unsigned char *buf, d[32];
    HANDLE f;
    DWORD got;
    int ok = 0, i;

    *size = 0;
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    buf = (unsigned char *)malloc(65536);
    if (buf && BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 &&
        BCryptCreateHash(alg, &hh, NULL, 0, NULL, 0, 0) == 0) {
        ok = 1;
        while (ReadFile(f, buf, 65536, &got, NULL) && got) {
            *size += got;
            if (BCryptHashData(hh, buf, got, 0) != 0) {
                ok = 0;
                break;
            }
        }
        if (ok && BCryptFinishHash(hh, d, sizeof(d), 0) == 0)
            for (i = 0; i < 32; i++)
                snprintf(hex + 2 * i, 3, "%02x", d[i]);
        else
            ok = 0;
    }
    if (hh)
        BCryptDestroyHash(hh);
    if (alg)
        BCryptCloseAlgorithmProvider(alg, 0);
    free(buf);
    CloseHandle(f);
    return ok;
}

static void thousands(unsigned long long v, char *buf, size_t cap)
{
    if (v >= 1000000)
        snprintf(buf, cap, "%llu,%03llu,%03llu", v / 1000000, v / 1000 % 1000, v % 1000);
    else if (v >= 1000)
        snprintf(buf, cap, "%llu,%03llu", v / 1000, v % 1000);
    else
        snprintf(buf, cap, "%llu", v);
}

/* Step 93: the check's outcome, for the F1 menu's About tab. */
static char g_result[700] = "not checked this run";
static char g_why[1400];                /* the last check's words, for the boxes */
static char g_gd[MAX_PATH * 6];         /* game_files' full path, UTF-8 */

const char *twoc_game_files_result(void)
{
    return g_result;
}

void twoc_game_files_note(const char *what)
{
    if (strlen(g_result) + strlen(what) + 4 < sizeof(g_result))
        snprintf(g_result + strlen(g_result), sizeof(g_result) - strlen(g_result), "; %s", what);
}

static int bad(int code, const char *log_line, const char *why)
{
    snprintf(g_result, sizeof(g_result), "%s", log_line);
    snprintf(g_why, sizeof(g_why), "%s", why);
    fprintf(stderr, "[FILES] %s\n", log_line);
    fflush(stderr);
    return code;
}

int twoc_game_files_check_quiet(char *why, size_t cap)
{
    wchar_t gd[MAX_PATH * 2], xbe[MAX_PATH * 2], cd[MAX_PATH * 2];
    char hex[65] = "", line[1200], a[32], b[32], tot[32];
    unsigned long long size = 0;
    unsigned i, n = twoc_disc_files(), missing = 0, xbe_i;
    char miss_names[300] = "";
    LARGE_INTEGER f, t0, t1;
    DWORD at;
    walk_t w;
    int code;
    const char *p, *sha;
    uint32_t xbe_size;

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    if (!GetFullPathNameW(GF_DIR, MAX_PATH * 2, gd, NULL))
        wcscpy_s(gd, MAX_PATH * 2, GF_DIR);
    WideCharToMultiByte(CP_UTF8, 0, gd, -1, g_gd, sizeof(g_gd), NULL, NULL);
    _snwprintf_s(xbe, MAX_PATH * 2, _TRUNCATE, L"%s\\default.xbe", gd);
    _snwprintf_s(cd, MAX_PATH * 2, _TRUNCATE, L"%s\\Crashdat", gd);
    xbe_i = (unsigned)twoc_disc_find("default.xbe");
    twoc_disc_entry(xbe_i, &p, &xbe_size, &sha);

    /* default.xbe */
    at = GetFileAttributesW(xbe);
    if (at == INVALID_FILE_ATTRIBUTES || (at & FILE_ATTRIBUTE_DIRECTORY)) {
        at = GetFileAttributesW(gd);
        snprintf(line, sizeof(line), "%s\\default.xbe is missing (%s); the game can't start", g_gd,
                 at == INVALID_FILE_ATTRIBUTES ? "no game_files folder here" : "game_files has no default.xbe");
        code = bad(at == INVALID_FILE_ATTRIBUTES ? GF_NONE : GF_MISSING, line, at == INVALID_FILE_ATTRIBUTES ?
                   "The game files aren't set up yet: there is no game_files folder." :
                   "game_files is there, but default.xbe (the game's program) is missing from it.");
        snprintf(why, cap, "%s", g_why);
        return code;
    }
    if (!sha256_file(xbe, hex, &size) || size != xbe_size || strcmp(hex, sha) != 0) {
        char title[128] = "", region[160] = "", desc[400] = "";
        unsigned tid = 0, reg = 0, ver = 0;
        FILE *fx = _wfopen(xbe, L"rb");
        if (fx) {                       /* its certificate, to say what it is */
            unsigned char *buf = (unsigned char *)malloc(1 << 20);
            size_t got = buf ? fread(buf, 1, 1 << 20, fx) : 0;
            if (buf && twoc_xbe_describe(buf, got, title, sizeof(title), &tid, &reg, &ver) == 0) {
                twoc_region_name(reg, region, sizeof(region));
                snprintf(desc, sizeof(desc), " It is \"%s\" (title id %08X, region %s, version %u).", title, tid,
                         region, ver);
            }
            free(buf);
            fclose(fx);
        }
        thousands(size, a, sizeof(a));
        thousands(xbe_size, b, sizeof(b));
        snprintf(line, sizeof(line), "%s\\default.xbe is not the USA disc's: %s bytes, SHA-256 %s (expected %s bytes, %s)",
                 g_gd, a, hex[0] ? hex : "unreadable", b, sha);
        snprintf(why, cap, "game_files holds another default.xbe than the USA disc's (%s bytes%s).%s This port runs "
                 "only the USA (NTSC-U) release of Crash Bandicoot: The Wrath of Cortex.", a,
                 size == xbe_size ? ", the right size, but different contents" : "", desc);
        return bad(GF_XBE, line, why);
    }

    /* Crashdat: every disc file there with its size */
    at = GetFileAttributesW(cd);
    if (at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY)) {
        snprintf(line, sizeof(line), "%s\\Crashdat is missing; the game can't load its data", g_gd);
        code = bad(GF_MISSING, line, "game_files has the right default.xbe, but its Crashdat folder (the game's "
                   "data) is missing.");
        snprintf(why, cap, "%s", g_why);
        return code;
    }
    memset(&w, 0, sizeof(w));
    w.seen = (unsigned char *)calloc(n, 1);
    if (!w.seen) {
        snprintf(why, cap, "Out of memory.");
        return bad(GF_FILES, "out of memory", why);
    }
    w.seen[xbe_i] = 1;
    walk(cd, "Crashdat", &w);
    for (i = 0; i < n; i++)
        if (!w.seen[i]) {
            missing++;
            twoc_disc_entry(i, &p, &xbe_size, &sha);
            if (strlen(miss_names) + strlen(p) + 4 < sizeof(miss_names))
                snprintf(miss_names + strlen(miss_names), sizeof(miss_names) - strlen(miss_names), "%s%s",
                         miss_names[0] ? ", " : "", p);
        }
    free(w.seen);
    QueryPerformanceCounter(&t1);
    thousands(w.files - w.extra + 1, tot, sizeof(tot));
    if (!missing && !w.wrong) {
        snprintf(g_result, sizeof(g_result), "as on the disc: default.xbe (SHA-256 %.16s...), Crashdat %u files; %s in "
                 "all", hex, w.files - w.extra, tot);
        if (w.extra) {
            fprintf(stderr, "[FILES] Crashdat has %u files not on the disc (%s); the disc's are all there\n", w.extra,
                    w.extra_names);
            twoc_game_files_note("extra files in Crashdat");
        }
        fprintf(stderr, "[FILES] game files as on the disc: default.xbe (SHA-256 %.16s...), Crashdat %u files; %s "
                "in all (%.1f ms)\n", hex, w.files - w.extra, tot,
                (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart);
        why[0] = 0;
        return GF_OK;
    }
    thousands(n, b, sizeof(b));
    snprintf(line, sizeof(line), "Crashdat's files differ from the disc's: %u of the disc's %u missing%s%s%s, %u with "
             "another size%s%s%s; default.xbe is the disc's", missing, n, missing ? " (" : "", miss_names,
             missing ? ")" : "", w.wrong, w.wrong ? " (" : "", w.wrong_names, w.wrong ? ")" : "");
    if (missing)
        snprintf(why, cap, "game_files is incomplete: %u of the disc's %s files are missing (%s%s).%s The game would "
                 "stop or crash when it loads a missing file.", missing, b, miss_names, missing > 3 ? ", ..." : "",
                 w.wrong ? " Others have the wrong size." : "");
    else
        snprintf(why, cap, "%u of the game's files have the wrong size (%s%s): a damaged or modified copy.", w.wrong,
                 w.wrong_names, w.wrong > 3 ? ", ..." : "");
    return bad(GF_FILES, line, why);
}

/* Step 83's boxes (when the setup screen can't be shown). */
int twoc_game_files_check(void)
{
    char why[1400], msg[3000];
    int code = twoc_game_files_check_quiet(why, sizeof(why)), r;

    if (code == GF_OK)
        return 1;
    snprintf(msg, sizeof(msg), "%s\n\nTo fix it, extract the disc image of Crash Bandicoot: The Wrath of Cortex (Xbox, "
             "USA) into\n%s\nso that default.xbe and the Crashdat folder are directly in it.%s", why, g_gd,
             code == GF_NONE || code == GF_MISSING ? "" : "\n\nStart anyway?");
    r = MessageBoxA(NULL, msg, "Crash TWOC - game files", MB_SETFOREGROUND | (code == GF_NONE || code == GF_MISSING ?
                    MB_ICONERROR | MB_OK : MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2));
    if (code == GF_NONE || code == GF_MISSING)
        return 0;
    fprintf(stderr, "[FILES] %s\n", r == IDYES ? "started anyway (the player chose Yes)" : "not started (No)");
    if (r == IDYES)
        twoc_game_files_note("started anyway");
    return r == IDYES;
}
