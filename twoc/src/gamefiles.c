/**
 * The game files at the start (step 83, notes 18.86).
 *
 * The exe runs the code of one default.xbe (the USA Xbox disc's) and reads
 * the game's data from Crashdat beside it, both in game_files in the folder
 * it was started from. A missing or different file used to fail without a
 * word (a missing default.xbe: a box naming another folder; a missing
 * Crashdat file: whatever the game does when a load fails). This checks
 * them before anything runs: default.xbe by size and SHA-256, Crashdat by
 * its files' count in each of its folders, and says what is wrong and how
 * to fix it, in the log and a message box.
 *
 * The expected values are the disc's (read from the user's image with
 * step 83's xisocount.py): its 2,267 files are default.xbe and Crashdat's
 * 2,266. No game data, only sizes, counts and a hash.
 */

#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <string.h>

#include "gamefiles.h"

#define GF_DIR          "game_files"

static const struct {
    const char *xbe_sha256;
    unsigned xbe_size;
    unsigned disc_files;                /* default.xbe and Crashdat's */
    struct { const char *dir; unsigned files; } crashdat[8];    /* "" = Crashdat's own files */
} k_disc = {
    "e8d7cbf225d899eb88227c11d1f40434c34e27c1b23ed946fb2c3471168d2f4d",
    1777664,
    2267,
    { { "", 7 }, { "ats", 51 }, { "chars", 959 }, { "Gfx", 48 }, { "levels", 481 }, { "sfx", 683 },
      { "stuff", 37 }, { NULL, 0 } },
};

/* Files under `dir`, all levels. */
static unsigned count_files(const char *dir)
{
    char pat[MAX_PATH + 4], sub[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    unsigned n = 0;

    snprintf(pat, sizeof(pat), "%s\\*", dir);
    h = FindFirstFileExA(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, "..")) {
                snprintf(sub, sizeof(sub), "%s\\%s", dir, fd.cFileName);
                n += count_files(sub);
            }
        } else
            n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

/* Crashdat's own files (not in a folder). */
static unsigned count_top(const char *dir)
{
    char pat[MAX_PATH + 4];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    unsigned n = 0;

    snprintf(pat, sizeof(pat), "%s\\*", dir);
    h = FindFirstFileExA(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            n++;
    while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

/* Crashdat's folders that aren't the disc's, and their files. */
static unsigned count_unknown(const char *dir, char *names, size_t cap)
{
    char pat[MAX_PATH + 4], sub[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    unsigned n = 0;
    int i;

    names[0] = 0;
    snprintf(pat, sizeof(pat), "%s\\*", dir);
    h = FindFirstFileExA(pat, FindExInfoBasic, &fd, FindExSearchLimitToDirectories, NULL, 0);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !strcmp(fd.cFileName, ".") ||
            !strcmp(fd.cFileName, ".."))
            continue;
        for (i = 1; k_disc.crashdat[i].dir; i++)
            if (_stricmp(fd.cFileName, k_disc.crashdat[i].dir) == 0)
                break;
        if (k_disc.crashdat[i].dir)
            continue;
        snprintf(sub, sizeof(sub), "%s\\%s", dir, fd.cFileName);
        n += count_files(sub);
        if (strlen(names) + strlen(fd.cFileName) + 3 < cap)
            snprintf(names + strlen(names), cap - strlen(names), "%s%s", names[0] ? ", " : "", fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

/* SHA-256 of a file as lowercase hex; 0 if it couldn't be read. */
static int sha256_file(const char *path, char hex[65], unsigned long long *size)
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hh = NULL;
    unsigned char buf[65536], d[32];
    HANDLE f;
    DWORD got;
    int ok = 0, i;

    *size = 0;
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 &&
        BCryptCreateHash(alg, &hh, NULL, 0, NULL, 0, 0) == 0) {
        ok = 1;
        while (ReadFile(f, buf, sizeof(buf), &got, NULL) && got) {
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
static char g_result[600] = "not checked this run";

const char *twoc_game_files_result(void)
{
    return g_result;
}

/* The log line, then the box: MB_YESNO asks "start anyway?"; returns 1 to start. */
static int tell(const char *log_line, const char *msg, UINT kind)
{
    int r;
    snprintf(g_result, sizeof(g_result), "%s", log_line);
    fprintf(stderr, "[FILES] %s\n", log_line);
    fflush(stderr);
    r = MessageBoxA(NULL, msg, "Crash TWOC - game files", kind | MB_SETFOREGROUND);
    if (kind & MB_YESNO)
        fprintf(stderr, "[FILES] %s\n", r == IDYES ? "started anyway (the player chose Yes)" : "not started (No)");
    if ((kind & MB_YESNO) && r == IDYES && strlen(g_result) + 40 < sizeof(g_result))
        snprintf(g_result + strlen(g_result), sizeof(g_result) - strlen(g_result), "; started anyway");
    return (kind & MB_YESNO) && r == IDYES;
}

int twoc_game_files_check(void)
{
    char gd[MAX_PATH], xbe[MAX_PATH], cd[MAX_PATH], hex[65] = "", fix[MAX_PATH + 400], msg[4096], line[1024],
         a[32], b[32], extra_names[256];
    unsigned long long size = 0;
    unsigned have[8], total = 0, expect = 0, extra;
    LARGE_INTEGER f, t0, t1;
    DWORD at;
    int i, xbe_ok, short_by = 0;

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    if (!GetFullPathNameA(GF_DIR, sizeof(gd), gd, NULL))
        snprintf(gd, sizeof(gd), "%s", GF_DIR);
    snprintf(xbe, sizeof(xbe), "%s\\default.xbe", gd);
    snprintf(cd, sizeof(cd), "%s\\Crashdat", gd);
    snprintf(fix, sizeof(fix),
             "To fix it, extract the disc image of Crash Bandicoot: The Wrath of Cortex (Xbox, USA) into\n%s\n"
             "so that default.xbe and the Crashdat folder are directly in it (extract-xiso or xdvdfs; the "
             "README says how).", gd);

    /* default.xbe */
    at = GetFileAttributesA(xbe);
    if (at == INVALID_FILE_ATTRIBUTES || (at & FILE_ATTRIBUTE_DIRECTORY)) {
        at = GetFileAttributesA(gd);
        snprintf(line, sizeof(line), "%s is missing (%s); the game can't start", xbe,
                 at == INVALID_FILE_ATTRIBUTES ? "no game_files folder here" : "game_files has no default.xbe");
        snprintf(msg, sizeof(msg), "The game files aren't there: %s\n\n%s is missing.\n\n%s\n\nThe game looks for "
                 "game_files in the folder it was started from.",
                 at == INVALID_FILE_ATTRIBUTES ? "there is no game_files folder." : "game_files has no default.xbe.",
                 xbe, fix);
        tell(line, msg, MB_ICONERROR | MB_OK);
        return 0;
    }
    xbe_ok = sha256_file(xbe, hex, &size) && size == k_disc.xbe_size && strcmp(hex, k_disc.xbe_sha256) == 0;
    if (!xbe_ok) {
        thousands(size, a, sizeof(a));
        thousands(k_disc.xbe_size, b, sizeof(b));
        snprintf(line, sizeof(line), "%s is not the USA disc's: %s bytes, SHA-256 %s (expected %s bytes, %s)", xbe,
                 a, hex[0] ? hex : "unreadable", b, k_disc.xbe_sha256);
        snprintf(msg, sizeof(msg), "default.xbe isn't the one this game was built from.\n\n%s\n%s bytes%s (the USA "
                 "disc's: %s bytes, and its SHA-256 differs).\n\nThis recompilation runs the code of the USA (NTSC-U) "
                 "Xbox release only; another release or a modified file won't run.\n\n%s\n\nStart anyway?",
                 xbe, a, size == k_disc.xbe_size ? ", the right size" : "", b, fix);
        if (!tell(line, msg, MB_ICONERROR | MB_YESNO | MB_DEFBUTTON2))
            return 0;
    }

    /* Crashdat */
    at = GetFileAttributesA(cd);
    if (at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY)) {
        snprintf(line, sizeof(line), "%s is missing; the game can't load its data", cd);
        snprintf(msg, sizeof(msg), "The game's data isn't there: %s\nis missing (default.xbe is there).\n\n%s", cd,
                 fix);
        tell(line, msg, MB_ICONERROR | MB_OK);
        return 0;
    }
    line[0] = 0;
    for (i = 0; k_disc.crashdat[i].dir; i++) {
        char sub[MAX_PATH];
        if (i == 0)
            have[i] = count_top(cd);
        else {
            snprintf(sub, sizeof(sub), "%s\\%s", cd, k_disc.crashdat[i].dir);
            have[i] = count_files(sub);
        }
        total += have[i];
        expect += k_disc.crashdat[i].files;
        if (have[i] < k_disc.crashdat[i].files)
            short_by += k_disc.crashdat[i].files - have[i];
        if (have[i] != k_disc.crashdat[i].files && strlen(line) + 80 < sizeof(line))
            snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%s%s %u of %u", line[0] ? "; " : "",
                     i ? "Crashdat\\" : "Crashdat", k_disc.crashdat[i].dir, have[i], k_disc.crashdat[i].files);
    }
    extra = count_unknown(cd, extra_names, sizeof(extra_names));
    total += extra;
    QueryPerformanceCounter(&t1);
    if (!line[0] && !extra) {
        thousands(total + 1, a, sizeof(a));
        snprintf(g_result, sizeof(g_result), "as on the disc: default.xbe (SHA-256 %.16s...), Crashdat %u files; %s in "
                 "all", hex, total, a);
        fprintf(stderr, "[FILES] game files as on the disc: default.xbe (SHA-256 %.16s...), Crashdat %u files; %s "
                "in all (%.1f ms)\n", hex, total, a, (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart);
        return 1;
    }
    if (extra && strlen(line) + strlen(extra_names) + 60 < sizeof(line))
        snprintf(line + strlen(line), sizeof(line) - strlen(line), "%sfolders not on the disc: %s (%u files)",
                 line[0] ? "; " : "", extra_names, extra);
    {
        char log_line[1200], tot[32], exp_[32];
        thousands(total + 1, tot, sizeof(tot));
        thousands(expect + 1, exp_, sizeof(exp_));
        snprintf(log_line, sizeof(log_line), "Crashdat's files differ from the disc's: %s files with default.xbe, "
                 "the disc has %s (%s)%s", tot, exp_, line,
                 xbe_ok ? "; default.xbe is the disc's, only the count differs" : "");
        if (short_by) {
            snprintf(msg, sizeof(msg), "Some of the game's data files are missing.\n\nThe disc has %s files "
                     "(default.xbe and Crashdat); game_files has %s. By folder: %s.\n\n%s"
                     "The game will stop or crash when it loads a missing file.\n\n%s\n\nStart anyway?",
                     exp_, tot, line, xbe_ok ? "default.xbe is the right one: only the count differs.\n\n" : "",
                     fix);
            return tell(log_line, msg, MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
        }
        snprintf(msg, sizeof(msg), "Crashdat has more files than the disc: %s with default.xbe, the disc has %s. By "
                 "folder: %s.\n\n%sExtra files are usually harmless (a copy, a backup, a mod), but the game was "
                 "tested with the disc's files only. If it misbehaves:\n\n%s\n\nStart anyway?",
                 tot, exp_, line, xbe_ok ? "default.xbe is the right one: only the count differs.\n\n" : "", fix);
        return tell(log_line, msg, MB_ICONINFORMATION | MB_YESNO | MB_DEFBUTTON1);
    }
}
