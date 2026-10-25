/**
 * The first-run setup's core (step 107, notes 18.110). See setup.h.
 *
 * Test aids (inert unless set):
 *   RECOMP_DEV_SETUP_HOLD=PHASE@N:MS   wait MS ms at that point and log it, so a test can kill the process
 *       there: copy@N (after N files copied), verify@N (after N checked), carry@0 (before the saves are
 *       moved), renames@0 (between the two renames of the swap)
 *   RECOMP_DEV_SETUP_FAIL=diskfull@N   the Nth file's write fails as a full disk would
 *   RECOMP_DEV_SETUP_FREE_MB=N         the data folder's disk reported with N MB free
 *   RECOMP_DEV_SETUP_MBPS=N            the copy held to N MB/s (a slow disk)
 */
#include <windows.h>
#include <bcrypt.h>
#include <process.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "setup.h"
#include "xdvdfs.h"
#include "disc_manifest.h"
#include "names.h"

#define DM_N ((unsigned)(sizeof(k_dm_files) / sizeof(k_dm_files[0])))
#define CHUNK (1u << 20)
#define MAX_DATA_DIR 196u               /* characters: 259 less the deepest file below it ("\game_files\" + 44) and a margin */
#define FREE_MARGIN (64ull << 20)       /* asked for beyond the disc's bytes */

#define N_GAME   L"game_files"
#define N_NEW    L"game_files.new"
#define N_OLD    L"game_files.old"
#define N_MARK   L"game_files.setup-in-progress.txt"
#define N_REQ    L"game_files.setup-request.txt"

/* ── Small things ──────────────────────────────────────────────────────── */

static void u8(const wchar_t *w, char *out, size_t cap)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL))
        out[0] = 0;
}

static void w8(const char *s, wchar_t *out, size_t cap)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, out, (int)cap))
        out[0] = 0;
}

/* A path for the screen and the log: the \\?\ prefix taken off. */
static const wchar_t *plain(const wchar_t *p)
{
    if (wcsncmp(p, L"\\\\?\\UNC\\", 8) == 0)
        return p + 6;                   /* "\\server..." (the caller sees one backslash less; fine for words) */
    if (wcsncmp(p, L"\\\\?\\", 4) == 0)
        return p + 4;
    return p;
}

/* An absolute path in \\?\ form (long paths). */
static void longp(const wchar_t *abs, wchar_t *out, size_t cap)
{
    if (wcsncmp(abs, L"\\\\?\\", 4) == 0)
        _snwprintf_s(out, cap, _TRUNCATE, L"%s", abs);
    else if (abs[0] == L'\\' && abs[1] == L'\\')
        _snwprintf_s(out, cap, _TRUNCATE, L"\\\\?\\UNC\\%s", abs + 2);
    else
        _snwprintf_s(out, cap, _TRUNCATE, L"\\\\?\\%s", abs);
}

/* dir \ rel (rel in UTF-8 with '/' between names). */
static void joinu(const wchar_t *dir, const char *rel, wchar_t *out, size_t cap)
{
    wchar_t r[SETUP_PATH_MAX];
    size_t i;
    w8(rel, r, SETUP_PATH_MAX);
    for (i = 0; r[i]; i++)
        if (r[i] == L'/')
            r[i] = L'\\';
    _snwprintf_s(out, cap, _TRUNCATE, L"%s\\%s", dir, r);
}

static int exists(const wchar_t *p)
{
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

static int is_dir(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
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

static void now_text(char *buf, size_t cap)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    snprintf(buf, cap, "%04u-%02u-%02u %02u:%02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}

static int dev_hold(const char *phase, unsigned n)
{
    const char *v = getenv("RECOMP_DEV_SETUP_HOLD");
    char p[32];
    unsigned at, ms;
    if (!v || sscanf(v, "%31[a-z]@%u:%u", p, &at, &ms) != 3 || strcmp(p, phase) || at != n)
        return 0;
    fprintf(stderr, "[SETUP] test hold (RECOMP_DEV_SETUP_HOLD): %s@%u, %u ms\n", phase, n, ms);
    fflush(stderr);
    Sleep(ms);
    return 1;
}

/* Windows errors in the screen's words. */
static const char *err_words(DWORD e)
{
    switch (e) {
    case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: return "the disk is full";
    case ERROR_ACCESS_DENIED: return "access is denied (the folder may be read-only or protected)";
    case ERROR_WRITE_PROTECT: return "the disk is write-protected";
    case ERROR_SHARING_VIOLATION: case ERROR_LOCK_VIOLATION: return "another program has the file open";
    case ERROR_PATH_NOT_FOUND: return "a folder on the way isn't there";
    case ERROR_FILE_NOT_FOUND: return "the file isn't there";
    case ERROR_FILENAME_EXCED_RANGE: return "the path is too long";
    case ERROR_CRC: return "the disk reports damaged data";
    case ERROR_NOT_READY: case ERROR_DEV_NOT_EXIST: return "the disk was removed or isn't ready";
    case ERROR_ALREADY_EXISTS: case ERROR_FILE_EXISTS: return "something with that name is already there";
    case ERROR_DIR_NOT_EMPTY: return "the folder isn't empty";
    default: return "Windows couldn't do it";
    }
}

static int busy_error(DWORD e)
{
    return e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION || e == ERROR_ACCESS_DENIED ||
           e == ERROR_USER_MAPPED_FILE || e == ERROR_DELETE_PENDING;
}

/* Retried while an antivirus or the indexer holds the file: 10 times over about 5 s. */
#define RETRIES 10
static void retry_wait(int k, const char *what, const wchar_t *p, DWORD e)
{
    char a[SETUP_PATH_MAX * 3];
    if (k == 0) {
        u8(plain(p), a, sizeof(a));
        fprintf(stderr, "[SETUP] %s %s: in use (Windows error %lu); trying again for about 5 s\n", what, a,
                (unsigned long)e);
    }
    Sleep(100 + 100 * k);
}

static HANDLE open_retry(const wchar_t *p, DWORD access, DWORD share, DWORD disp, DWORD flags, DWORD *err)
{
    int k;
    for (k = 0;; k++) {
        HANDLE h = CreateFileW(p, access, share, NULL, disp, flags, NULL);
        DWORD e;
        if (h != INVALID_HANDLE_VALUE)
            return h;
        e = GetLastError();
        if (!busy_error(e) || k == RETRIES) {
            *err = e;
            return INVALID_HANDLE_VALUE;
        }
        retry_wait(k, "opening", p, e);
    }
}

static BOOL move_retry(const wchar_t *from, const wchar_t *to, DWORD *err)
{
    int k;
    for (k = 0;; k++) {
        DWORD e;
        if (MoveFileExW(from, to, 0))
            return TRUE;
        e = GetLastError();
        if (!busy_error(e) || k == RETRIES) {
            *err = e;
            return FALSE;
        }
        retry_wait(k, "renaming", from, e);
    }
}

static BOOL delete_retry(const wchar_t *p)
{
    int k;
    for (k = 0;; k++) {
        DWORD e;
        if (DeleteFileW(p))
            return TRUE;
        e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
            return TRUE;
        if (e == ERROR_ACCESS_DENIED && k == 0) {   /* a read-only disc file (copied off a disc as it was) */
            DWORD a = GetFileAttributesW(p);
            if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY)) {
                SetFileAttributesW(p, a & ~FILE_ATTRIBUTE_READONLY);
                continue;
            }
        }
        if (!busy_error(e) || k == RETRIES)
            return FALSE;
        retry_wait(k, "deleting", p, e);
    }
}

/* ── The data folder ───────────────────────────────────────────────────── */

static wchar_t g_dir[SETUP_PATH_MAX];   /* plain absolute */
static wchar_t g_ldir[SETUP_PATH_MAX];  /* \\?\ form */
static char g_dir8[SETUP_PATH_MAX * 3];
static int g_fallback, g_self;
static char g_dir_note[SETUP_PATH_MAX * 3 + 256];     /* step 109: the migration's line, logged once the log is open */

static int writable(const wchar_t *dir)
{
    wchar_t p[SETUP_PATH_MAX + 64];
    HANDLE h;
    _snwprintf_s(p, sizeof(p) / sizeof(p[0]), _TRUNCATE, L"%s\\write-test.%lu.tmp", dir,
                 (unsigned long)GetCurrentProcessId());
    h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    CloseHandle(h);
    return 1;
}

static int started_by_itself(void)
{
    wchar_t *e = GetEnvironmentStringsW(), *p;
    int self = 1;
    for (p = e; p && *p; p += wcslen(p) + 1)        /* test aids (RECOMP_DEV_*) don't make a launcher */
        if ((_wcsnicmp(p, L"RECOMP_", 7) == 0 && _wcsnicmp(p, L"RECOMP_DEV_", 11) != 0) ||
            _wcsnicmp(p, L"TWOC_LAUNCHER=", 14) == 0) {
            self = 0;
            break;
        }
    if (e)
        FreeEnvironmentStringsW(e);
    return self;
}

/* ── Step 109: the data folder's old name ──────────────────────────────────
 * %LOCALAPPDATA%\CrashTWOC (the name before Project Wrath) into a new %LOCALAPPDATA%\ProjectWrath: when the new
 * one holds no game_files and no ini yet and the old one is there, the old one's contents are copied into
 * ProjectWrath.migrating (its game_files with the saves and backups, its crash_twoc.ini, anything else; not its
 * logs, the mute marker or a setup's leftovers), then moved into ProjectWrath. The old folder is only read: never
 * changed or deleted. A copy that stopped part-way (a power cut) leaves ProjectWrath.migrating, which the next
 * start deletes (it is this code's own) and makes again. Links are never followed. */
static int mig_skip(const wchar_t *name, int dir, int top)
{
    size_t n = wcslen(name);
    if (!wcscmp(name, L".") || !wcscmp(name, L".."))
        return 1;
    if (top && dir && (!_wcsicmp(name, L"logs") || !_wcsicmp(name, N_NEW) || !_wcsicmp(name, N_OLD)))
        return 1;
    if (top && !dir && (!_wcsicmp(name, N_MARK) || !_wcsicmp(name, N_REQ) || !_wcsicmp(name, L"crash_twoc.muted")))
        return 1;
    return top && !dir && n > 4 && !_wcsicmp(name + n - 4, L".log");
}

/* Copies `from` into `to` (made here), recursively; counts files and bytes. 0, or a Windows error. */
static DWORD mig_copy(const wchar_t *from, const wchar_t *to, int top, unsigned *files, unsigned long long *bytes)
{
    wchar_t pat[SETUP_PATH_MAX + 4], a[SETUP_PATH_MAX], b2[SETUP_PATH_MAX];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    DWORD e = 0;

    if (!CreateDirectoryW(to, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return GetLastError();
    _snwprintf_s(pat, sizeof(pat) / sizeof(pat[0]), _TRUNCATE, L"%s\\*", from);
    h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : GetLastError();
    do {
        int dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (mig_skip(fd.cFileName, dir, top) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        _snwprintf_s(a, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", from, fd.cFileName);
        _snwprintf_s(b2, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", to, fd.cFileName);
        if (dir)
            e = mig_copy(a, b2, 0, files, bytes);
        else if (!CopyFileExW(a, b2, NULL, NULL, NULL, COPY_FILE_FAIL_IF_EXISTS))
            e = GetLastError();
        else {
            (*files)++;
            *bytes += ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        }
    } while (!e && FindNextFileW(h, &fd));
    FindClose(h);
    return e;
}

/* Deletes `dir` (this code's own ProjectWrath.migrating) and everything in it; a link only as the link. */
static void mig_delete(const wchar_t *dir)
{
    wchar_t pat[SETUP_PATH_MAX + 4], a[SETUP_PATH_MAX];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    _snwprintf_s(pat, sizeof(pat) / sizeof(pat[0]), _TRUNCATE, L"%s\\*", dir);
    h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
                continue;
            _snwprintf_s(a, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", dir, fd.cFileName);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                mig_delete(a);
            else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                RemoveDirectoryW(a);
            else {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                    SetFileAttributesW(a, fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
                DeleteFileW(a);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

/* `base` is %LOCALAPPDATA% (or a test's stand-in), `dir` the new data folder in it. */
static void migrate_data_dir(const wchar_t *base, const wchar_t *dir)
{
    wchar_t old[SETUP_PATH_MAX], tmp[SETUP_PATH_MAX], p[SETUP_PATH_MAX], q[SETUP_PATH_MAX];
    wchar_t pat[SETUP_PATH_MAX + 4];
    char o8[SETUP_PATH_MAX * 3];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    unsigned files = 0, moved = 0, kept = 0;
    unsigned long long bytes = 0;
    ULONGLONG t0 = GetTickCount64();
    DWORD e;

    if (twoc_names_dev())               /* the developer's crash_twoc.exe uses the old folder itself */
        return;
    _snwprintf_s(old, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", base, TWOC_OLD_DATA_FOLDER);
    _snwprintf_s(tmp, SETUP_PATH_MAX, _TRUNCATE, L"%s.migrating", dir);
    if (!is_dir(old) || is_link(old))
        return;
    _snwprintf_s(p, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", dir, N_GAME);
    if (is_dir(p))
        return;                         /* the new folder is in use already */
    {
        char ini8[MAX_PATH * 3];
        wchar_t iw[MAX_PATH];
        snprintf(ini8, sizeof(ini8), "%s", twoc_ini_name());
        MultiByteToWideChar(CP_UTF8, 0, ini8, -1, iw, MAX_PATH);
        _snwprintf_s(p, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", dir, iw);
        _snwprintf_s(q, SETUP_PATH_MAX, _TRUNCATE, L"%s\\" TWOC_OLD_INI_NAME_W, dir);
        if (exists(p) || exists(q))
            return;
    }
    u8(old, o8, sizeof(o8));
    if (is_dir(tmp))                    /* a copy that stopped part-way: this code's own, made again */
        mig_delete(tmp);
    e = mig_copy(old, tmp, 1, &files, &bytes);
    if (e) {
        mig_delete(tmp);
        snprintf(g_dir_note, sizeof(g_dir_note), "[DATA] the old data folder %s couldn't be copied into the new one "
                 "(Windows error %lu): the new one starts empty, the old one is as it was (tried again at the next "
                 "start)", o8, (unsigned long)e);
        return;
    }
    SHCreateDirectoryExW(NULL, dir, NULL);
    _snwprintf_s(pat, sizeof(pat) / sizeof(pat[0]), _TRUNCATE, L"%s\\*", tmp);
    h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
                continue;
            _snwprintf_s(p, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", tmp, fd.cFileName);
            _snwprintf_s(q, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", dir, fd.cFileName);
            if (MoveFileExW(p, q, 0))
                moved++;
            else
                kept++;                 /* already in the new folder (its own logs): the new one's kept */
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    mig_delete(tmp);
    snprintf(g_dir_note, sizeof(g_dir_note), "[DATA] the old data folder %s (the name before Project Wrath) copied "
             "into the new one: %u files, %.1f MB, %u items moved in%s, %llu ms; the old folder is as it was (delete it "
             "yourself once the game runs from the new one)", o8, files, (double)bytes / 1048576.0, moved,
             kept ? ", some already there kept" : "", GetTickCount64() - t0);
}

const char *twoc_data_dir_note(void)
{
    return g_dir_note;
}

int twoc_data_dir_init(void)
{
    wchar_t exe[SETUP_PATH_MAX], *slash;
    DWORD n;

    g_self = started_by_itself();
    if (g_self && (n = GetModuleFileNameW(NULL, exe, SETUP_PATH_MAX)) > 0 && n < SETUP_PATH_MAX &&
        (slash = wcsrchr(exe, L'\\')) != NULL) {
        *slash = 0;
        if (writable(exe))
            _snwprintf_s(g_dir, SETUP_PATH_MAX, _TRUNCATE, L"%s", exe);
        else {
            wchar_t la[MAX_PATH];
            const wchar_t *dev = _wgetenv(L"RECOMP_DEV_LOCALAPPDATA");  /* a test's stand-in, in its scratch */
            if (dev && *dev) {
                _snwprintf_s(g_dir, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", dev, twoc_data_folder_w());
                g_fallback = 1;
                migrate_data_dir(dev, g_dir);      /* step 109: from the old name, CrashTWOC */
            } else if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, la))) {
                _snwprintf_s(g_dir, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", la, twoc_data_folder_w());
                g_fallback = 1;
                migrate_data_dir(la, g_dir);
            } else
                _snwprintf_s(g_dir, SETUP_PATH_MAX, _TRUNCATE, L"%s", exe);
            if (g_fallback)
                SHCreateDirectoryExW(NULL, g_dir, NULL);    /* with every folder on the way */
        }
        if (!SetCurrentDirectoryW(g_dir) && g_fallback) {   /* not even there: the exe's folder, read-only */
            _snwprintf_s(g_dir, SETUP_PATH_MAX, _TRUNCATE, L"%s", exe);
            g_fallback = 0;
            SetCurrentDirectoryW(g_dir);
        }
    }
    if (!g_dir[0] && !GetCurrentDirectoryW(SETUP_PATH_MAX, g_dir))
        wcscpy_s(g_dir, SETUP_PATH_MAX, L".");
    {   /* backslashes only, no "." or "..": the \\?\ form below takes the path as it is */
        wchar_t full[SETUP_PATH_MAX];
        if (GetFullPathNameW(g_dir, SETUP_PATH_MAX, full, NULL))
            wcscpy_s(g_dir, SETUP_PATH_MAX, full);
    }
    n = (DWORD)wcslen(g_dir);
    if (n > 3 && g_dir[n - 1] == L'\\')
        g_dir[n - 1] = 0;
    longp(g_dir, g_ldir, SETUP_PATH_MAX);
    if (wcslen(g_dir) == 3 && g_dir[2] == L'\\')   /* "D:\": the \\?\ form keeps its backslash; joins add one */
        g_ldir[wcslen(g_ldir) - 1] = 0;
    u8(g_dir, g_dir8, sizeof(g_dir8));
    return g_self;
}

const wchar_t *twoc_data_dir(void)
{
    return g_dir;
}

int twoc_data_dir_fallback(void)
{
    return g_fallback;
}

const char *twoc_data_dir_utf8(void)
{
    return g_dir8;
}

int twoc_settings_file(const char *name, char *out, size_t cap)
{
    wchar_t w[SETUP_PATH_MAX], nw[MAX_PATH], *slash;
    DWORD n;

    if (!MultiByteToWideChar(CP_UTF8, 0, name, -1, nw, MAX_PATH))     /* step 109: UTF-8 (the exe's own name) */
        wcscpy_s(nw, MAX_PATH, L"settings.ini");
    if (g_fallback)
        _snwprintf_s(w, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", g_dir, nw);
    else if ((n = GetModuleFileNameW(NULL, w, SETUP_PATH_MAX)) > 0 && n < SETUP_PATH_MAX &&
             (slash = wcsrchr(w, L'\\')) != NULL)
        _snwprintf_s(slash + 1, SETUP_PATH_MAX - (slash + 1 - w), _TRUNCATE, L"%s", nw);
    else
        _snwprintf_s(w, SETUP_PATH_MAX, _TRUNCATE, L"%s", nw);
    /* the narrow functions' code page: UTF-8 with the exe's manifest (Windows 10 1903 on) */
    return WideCharToMultiByte(CP_ACP, 0, w, -1, out, (int)cap, NULL, NULL) ? 0 : -1;
}

static void dpath(const wchar_t *name, wchar_t *out, size_t cap)
{
    _snwprintf_s(out, cap, _TRUNCATE, L"%s\\%s", g_ldir, name);
}

int twoc_setup_game_files_exist(void)
{
    wchar_t g[SETUP_PATH_MAX];
    dpath(N_GAME, g, SETUP_PATH_MAX);
    return is_dir(g);
}

/* ── The manifest ──────────────────────────────────────────────────────── */

static unsigned *g_sorted;
static INIT_ONCE g_dm_once = INIT_ONCE_STATIC_INIT;

static int cmp_dm(const void *a, const void *b)
{
    return _stricmp(k_dm_files[*(const unsigned *)a].path, k_dm_files[*(const unsigned *)b].path);
}

static BOOL CALLBACK dm_init(PINIT_ONCE o, PVOID p, PVOID *c)
{
    unsigned i;
    (void)o; (void)p; (void)c;
    g_sorted = (unsigned *)malloc(DM_N * sizeof(unsigned));
    if (!g_sorted)
        return FALSE;
    for (i = 0; i < DM_N; i++)
        g_sorted[i] = i;
    qsort(g_sorted, DM_N, sizeof(unsigned), cmp_dm);
    return TRUE;
}

unsigned twoc_disc_files(void)
{
    return DM_N;
}

uint64_t twoc_disc_bytes(void)
{
    return DM_BYTES;
}

const char *twoc_disc_name(void)
{
    return "Crash Bandicoot: The Wrath of Cortex (Xbox, USA)";
}

int twoc_disc_find(const char *path)
{
    char p[1100];
    size_t i, lo = 0, hi = DM_N;

    if (!InitOnceExecuteOnce(&g_dm_once, dm_init, NULL, NULL) || !g_sorted)
        return -1;
    while (*path == '/' || *path == '\\')
        path++;
    for (i = 0; path[i] && i + 1 < sizeof(p); i++)
        p[i] = path[i] == '\\' ? '/' : path[i];
    p[i] = 0;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = _stricmp(k_dm_files[g_sorted[mid]].path, p);
        if (c == 0)
            return (int)g_sorted[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return -1;
}

void twoc_disc_entry(unsigned i, const char **path, uint32_t *size, const char **sha256)
{
    *path = k_dm_files[i].path;
    *size = k_dm_files[i].size;
    *sha256 = k_dm_files[i].sha256;
}

/* A top-level name the disc has (default.xbe, Crashdat): what a swap leaves behind and moves nothing of. */
static int disc_top(const wchar_t *name)
{
    unsigned i;
    char n8[600];
    size_t l;
    u8(name, n8, sizeof(n8));
    l = strlen(n8);
    for (i = 0; i < DM_N; i++) {
        const char *p = k_dm_files[i].path;
        if (_strnicmp(p, n8, l) == 0 && (p[l] == 0 || p[l] == '/'))
            return 1;
    }
    return 0;
}

void twoc_region_name(unsigned region, char *buf, size_t cap)
{
    buf[0] = 0;
    if (region & 1)
        snprintf(buf + strlen(buf), cap - strlen(buf), "%sNorth America (NTSC-U)", buf[0] ? ", " : "");
    if (region & 2)
        snprintf(buf + strlen(buf), cap - strlen(buf), "%sJapan (NTSC-J)", buf[0] ? ", " : "");
    if (region & 4)
        snprintf(buf + strlen(buf), cap - strlen(buf), "%sEurope and the rest of the world (PAL)", buf[0] ? ", " : "");
    if (region & 0x80000000u)
        snprintf(buf + strlen(buf), cap - strlen(buf), "%sdevelopment kits", buf[0] ? ", " : "");
    if (!buf[0])
        snprintf(buf, cap, "none given (0x%08X)", region);
}

int twoc_xbe_describe(const unsigned char *b, size_t n, char *title, size_t tcap, unsigned *title_id,
                      unsigned *region, unsigned *version)
{
    uint32_t base, cert;
    size_t c, i;
    wchar_t w[41];

    if (n < 0x180 || memcmp(b, "XBEH", 4) != 0)
        return -1;
    memcpy(&base, b + 0x104, 4);
    memcpy(&cert, b + 0x118, 4);
    if (cert < base || (size_t)(cert - base) + 0xB0 > n)
        return -1;
    c = cert - base;
    memcpy(title_id, b + c + 8, 4);
    for (i = 0; i < 40; i++) {
        uint16_t ch;
        memcpy(&ch, b + c + 0x0C + 2 * i, 2);
        w[i] = (wchar_t)ch;
        if (!ch)
            break;
    }
    w[i < 40 ? i : 40] = 0;
    u8(w, title, tcap);
    memcpy(region, b + c + 0xA0, 4);
    memcpy(version, b + c + 0xAC, 4);
    return 0;
}

/* ── SHA-256 ───────────────────────────────────────────────────────────── */

typedef struct {
    BCRYPT_ALG_HANDLE alg;
    BCRYPT_HASH_HANDLE h;
} sha_t;

static int sha_open(sha_t *s)
{
    memset(s, 0, sizeof(*s));
    if (BCryptOpenAlgorithmProvider(&s->alg, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_HASH_REUSABLE_FLAG) != 0)
        return -1;
    if (BCryptCreateHash(s->alg, &s->h, NULL, 0, NULL, 0, BCRYPT_HASH_REUSABLE_FLAG) != 0) {
        BCryptCloseAlgorithmProvider(s->alg, 0);
        return -1;
    }
    return 0;
}

static void sha_close(sha_t *s)
{
    if (s->h)
        BCryptDestroyHash(s->h);
    if (s->alg)
        BCryptCloseAlgorithmProvider(s->alg, 0);
}

static void sha_hex(sha_t *s, char hex[65])
{
    unsigned char d[32];
    int i;
    if (BCryptFinishHash(s->h, d, 32, 0) != 0) {
        hex[0] = 0;
        return;
    }
    for (i = 0; i < 32; i++)
        snprintf(hex + 2 * i, 3, "%02x", d[i]);
}

/* ── The job's state ───────────────────────────────────────────────────── */

static CRITICAL_SECTION g_cs;
static INIT_ONCE g_cs_once = INIT_ONCE_STATIC_INIT;
static setup_status_t g_st;
static volatile LONG g_cancel;
static HANDLE g_thread;
static LARGE_INTEGER g_qf, g_t0;
/* The source identified (owned by the worker; kept between identify and install). */
static struct {
    int kind;
    xdvd_t *img;
    wchar_t root[SETUP_PATH_MAX];       /* a folder: where default.xbe is (\\?\ form) */
    int in_place;
} g_src;
/* Speed: bytes and times of the last samples. */
static uint64_t g_sp_b[16];
static double g_sp_t[16];
static int g_sp_n;

static BOOL CALLBACK cs_init(PINIT_ONCE o, PVOID p, PVOID *c)
{
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&g_cs);
    QueryPerformanceFrequency(&g_qf);
    return TRUE;
}

static void lock(void)
{
    InitOnceExecuteOnce(&g_cs_once, cs_init, NULL, NULL);
    EnterCriticalSection(&g_cs);
}

static void unlock(void)
{
    LeaveCriticalSection(&g_cs);
}

static double secs_since(LARGE_INTEGER t)
{
    LARGE_INTEGER n;
    QueryPerformanceCounter(&n);
    return (double)(n.QuadPart - t.QuadPart) / (double)g_qf.QuadPart;
}

static void set_state(int s)
{
    lock();
    g_st.state = s;
    unlock();
}

static void phase_begin(int s, uint64_t bytes, unsigned files)
{
    lock();
    g_st.state = s;
    g_st.bytes_done = 0;
    g_st.bytes_total = bytes;
    g_st.files_done = 0;
    g_st.files_total = files;
    g_st.mb_s = 0;
    g_st.eta_s = -1;
    g_st.current[0] = 0;
    QueryPerformanceCounter(&g_t0);
    g_sp_n = 0;
    unlock();
}

static void progress(uint64_t add, const char *file, int file_done)
{
    double t;
    lock();
    g_st.bytes_done += add;
    if (file_done)
        g_st.files_done++;
    if (file)
        snprintf(g_st.current, sizeof(g_st.current), "%s", file);
    t = secs_since(g_t0);
    g_st.secs = t;
    if (g_sp_n == 0 || t - g_sp_t[(g_sp_n - 1) % 16] >= 0.25) {   /* a sample every 0.25 s, the last 4 s */
        g_sp_b[g_sp_n % 16] = g_st.bytes_done;
        g_sp_t[g_sp_n % 16] = t;
        g_sp_n++;
        if (g_sp_n >= 2) {
            int a = g_sp_n > 16 ? g_sp_n - 16 : 0, z = g_sp_n - 1;
            double dt = g_sp_t[z % 16] - g_sp_t[a % 16];
            if (dt > 0.1)
                g_st.mb_s = (double)(g_sp_b[z % 16] - g_sp_b[a % 16]) / 1048576.0 / dt;
        }
        if (g_st.mb_s > 0.01)
            g_st.eta_s = (double)(g_st.bytes_total - g_st.bytes_done) / 1048576.0 / g_st.mb_s;
    }
    unlock();
}

/* A failure: `why` what happened, `hint` what to try (both plain words). */
static int fail(const char *why, const char *hint)
{
    lock();
    snprintf(g_st.why, sizeof(g_st.why), "%s", why);
    snprintf(g_st.hint, sizeof(g_st.hint), "%s", hint ? hint : "");
    g_st.state = SETUP_FAILED;
    unlock();
    fprintf(stderr, "[SETUP] failed: %s%s%s\n", why, hint && *hint ? " | " : "", hint ? hint : "");
    fflush(stderr);
    return -1;
}

static int cancelled(void)
{
    return g_cancel != 0;
}

/* ── The journal ───────────────────────────────────────────────────────── */

static char g_j_started[32];

static int journal(const char *phase)
{
    wchar_t m[SETUP_PATH_MAX];
    char t[SETUP_PATH_MAX * 3 + 600];
    HANDLE h;
    DWORD put, e = 0;

    dpath(N_MARK, m, SETUP_PATH_MAX);
    if (!g_j_started[0])
        now_text(g_j_started, sizeof(g_j_started));
    snprintf(t, sizeof(t), "%s: a setup of the game files is under way, or was interrupted.\r\n"
             "The game deals with this file at its next start; there is no need to delete it by hand.\r\n"
             "source=%s\r\nkind=%s\r\nstarted=%s\r\nreplacing=%d\r\nphase=%s\r\n", twoc_app_name(), g_st.source_utf8,
             g_src.kind == SETUP_SRC_IMAGE ? "image" : "folder", g_j_started, g_st.replace, phase);
    h = open_retry(m, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, &e);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    if (!WriteFile(h, t, (DWORD)strlen(t), &put, NULL) || put != strlen(t)) {
        CloseHandle(h);
        return -1;
    }
    FlushFileBuffers(h);
    CloseHandle(h);
    fprintf(stderr, "[SETUP] journal: phase %s\n", phase);
    return 0;
}

static void journal_remove(void)
{
    wchar_t m[SETUP_PATH_MAX];
    dpath(N_MARK, m, SETUP_PATH_MAX);
    delete_retry(m);
    g_j_started[0] = 0;
}

/* ── Folders: the disc's files deleted, user items carried ─────────────── */

static int cmp_depth(const void *a, const void *b)
{
    const char *x = *(const char *const *)a, *y = *(const char *const *)b;
    int dx = 0, dy = 0;
    for (; *x; x++)
        dx += *x == '/';
    for (; *y; y++)
        dy += *y == '/';
    return dy - dx;
}

/* Entries left in `dir` (files and folders, all levels), up to `max` names in `names`. */
static unsigned count_left(const wchar_t *dir, char *names, size_t cap)
{
    wchar_t pat[SETUP_PATH_MAX + 4], sub[SETUP_PATH_MAX];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    unsigned n = 0;

    _snwprintf_s(pat, sizeof(pat) / sizeof(pat[0]), _TRUNCATE, L"%s\\*", dir);
    h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        char n8[600];
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
            continue;
        n++;
        u8(fd.cFileName, n8, sizeof(n8));
        if (names && strlen(names) + strlen(n8) + 4 < cap)
            snprintf(names + strlen(names), cap - strlen(names), "%s%s", names[0] ? ", " : "", n8);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            _snwprintf_s(sub, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", dir, fd.cFileName);
            n += count_left(sub, NULL, 0);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

/* Deletes the disc's files in `dir` and the folders they leave empty, then `dir` if empty. Nothing else is
 * touched. Returns the entries left (0: `dir` is gone). */
/* A linked folder (a junction or a symbolic link): never followed when deleting, so nothing outside is touched. */
static int is_link(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
}

/* `rel` (a disc path) lies below a linked folder inside `dir`. */
static int below_link(const wchar_t *dir, const char *rel)
{
    char d[1100];
    size_t j;
    for (j = 0; rel[j]; j++)
        if (rel[j] == '/') {
            wchar_t p[SETUP_PATH_MAX];
            memcpy(d, rel, j);
            d[j] = 0;
            joinu(dir, d, p, SETUP_PATH_MAX);
            if (is_link(p))
                return 1;
        }
    return 0;
}

/* Folders below `dir` left with nothing in them, deepest first (never through a link). Empty folders hold no
 * data, so this never loses any. */
static void remove_empty_dirs(const wchar_t *dir)
{
    wchar_t pat[SETUP_PATH_MAX + 4], sub[SETUP_PATH_MAX];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    _snwprintf_s(pat, sizeof(pat) / sizeof(pat[0]), _TRUNCATE, L"%s\\*", dir);
    h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchLimitToDirectories, NULL, 0);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
            continue;
        _snwprintf_s(sub, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", dir, fd.cFileName);
        remove_empty_dirs(sub);
        RemoveDirectoryW(sub);          /* only if empty */
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static unsigned delete_disc_files(const wchar_t *dir, char *left_names, size_t cap)
{
    wchar_t p[SETUP_PATH_MAX];
    const char **dirs;
    size_t nd = 0, i, j;
    unsigned left, linked = 0;

    if (left_names)
        left_names[0] = 0;
    if (!is_dir(dir))
        return 0;
    if (is_link(dir)) {                 /* the folder itself a link: nothing inside is ours to delete */
        fprintf(stderr, "[SETUP] a linked folder, left as it is: nothing deleted through it\n");
        return count_left(dir, left_names, cap) + 1;
    }
    dirs = (const char **)malloc(DM_N * 16 * sizeof(char *));
    for (i = 0; i < DM_N; i++) {
        if (below_link(dir, k_dm_files[i].path)) {
            linked++;
            continue;
        }
        joinu(dir, k_dm_files[i].path, p, SETUP_PATH_MAX);
        delete_retry(p);
    }
    if (linked)
        fprintf(stderr, "[SETUP] %u disc files below a linked folder (a junction or a link): left as they are\n",
                linked);
    /* Every folder on the way to a disc file and the disc's empty ones, deepest first (a folder can be listed
     * more than once). */
    if (dirs) {
        for (i = 0; i < DM_N; i++) {
            const char *s = k_dm_files[i].path;
            for (j = 0; s[j]; j++)
                if (s[j] == '/' && nd < DM_N * 16) {
                    char *d = (char *)malloc(j + 1);
                    if (!d)
                        continue;
                    memcpy(d, s, j);
                    d[j] = 0;
                    dirs[nd++] = d;
                }
        }
        for (i = 0; k_dm_empty_dirs[i] && nd < DM_N * 16; i++) {
            char *d = _strdup(k_dm_empty_dirs[i]);
            if (d)
                dirs[nd++] = d;
        }
        qsort((void *)dirs, nd, sizeof(char *), cmp_depth);
        for (i = 0; i < nd; i++) {
            if (dirs[i] && (i == 0 || !dirs[i - 1] || strcmp(dirs[i], dirs[i - 1]))) {
                joinu(dir, dirs[i], p, SETUP_PATH_MAX);
                if (!is_link(p))
                    RemoveDirectoryW(p);    /* only if empty */
            }
        }
        for (i = 0; i < nd; i++)
            free((void *)dirs[i]);
        free((void *)dirs);
    }
    remove_empty_dirs(dir);
    RemoveDirectoryW(dir);
    if (!is_dir(dir))
        return 0;
    left = count_left(dir, left_names, cap);
    if (!left) {                        /* empty but still there (an indexer had it open): once more */
        Sleep(300);
        RemoveDirectoryW(dir);
        if (!is_dir(dir))
            return 0;
    }
    return left;
}

/* Moves the top-level items of `from` that aren't the disc's (saves, their backups, TDATA, ...) into `to`.
 * Returns the number moved, or -1 (`err`) when one couldn't be; `clash` counts those left because `to` has one
 * of that name. */
static int carry(const wchar_t *from, const wchar_t *to, int *clash, char *err, size_t cap)
{
    wchar_t pat[SETUP_PATH_MAX + 4], a[SETUP_PATH_MAX], b[SETUP_PATH_MAX];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int n = 0;

    if (clash)
        *clash = 0;
    _snwprintf_s(pat, sizeof(pat) / sizeof(pat[0]), _TRUNCATE, L"%s\\*", from);
    h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        DWORD e = 0;
        char n8[600];
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..") || disc_top(fd.cFileName))
            continue;
        _snwprintf_s(a, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", from, fd.cFileName);
        _snwprintf_s(b, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", to, fd.cFileName);
        u8(fd.cFileName, n8, sizeof(n8));
        if (exists(b)) {
            if (clash)
                (*clash)++;
            fprintf(stderr, "[SETUP] carry: %s is in both folders: left where it is\n", n8);
            continue;
        }
        if (!move_retry(a, b, &e)) {
            snprintf(err, cap, "%s couldn't be moved: %s (Windows error %lu)", n8, err_words(e), (unsigned long)e);
            FindClose(h);
            return -1;
        }
        fprintf(stderr, "[SETUP] carry: %s moved\n", n8);
        n++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

/* ── Recovery ──────────────────────────────────────────────────────────── */

static setup_recovery_t g_rec;

const setup_recovery_t *twoc_setup_recovery(void)
{
    return &g_rec;
}

static void journal_read(char *source, size_t scap, char *phase, size_t pcap, char *started, size_t tcap)
{
    wchar_t m[SETUP_PATH_MAX];
    char line[SETUP_PATH_MAX * 3 + 32];
    FILE *f;
    source[0] = phase[0] = started[0] = 0;
    dpath(N_MARK, m, SETUP_PATH_MAX);
    if ((f = _wfopen(m, L"rb")) == NULL)
        return;
    while (fgets(line, sizeof(line), f)) {
        char *e = line + strlen(line);
        while (e > line && (e[-1] == '\n' || e[-1] == '\r'))
            *--e = 0;
        if (!strncmp(line, "source=", 7))
            snprintf(source, scap, "%s", line + 7);
        else if (!strncmp(line, "phase=", 6))
            snprintf(phase, pcap, "%s", line + 6);
        else if (!strncmp(line, "started=", 8))
            snprintf(started, tcap, "%s", line + 8);
    }
    fclose(f);
}

static const char *phase_words(const char *p)
{
    if (!strcmp(p, "copying")) return "while copying the game's files";
    if (!strcmp(p, "verifying")) return "while checking the copied files";
    if (!strcmp(p, "carrying")) return "while moving your saves and settings into the new folder";
    if (!strcmp(p, "renaming")) return "while putting the new files in place (moving the old ones aside)";
    if (!strcmp(p, "placing")) return "between moving the old files aside and putting the new ones in place";
    if (!strcmp(p, "finished")) return "at its very end";
    return "at a point it didn't record";
}

void twoc_setup_recover(void)
{
    wchar_t g[SETUP_PATH_MAX], n[SETUP_PATH_MAX], o[SETUP_PATH_MAX], m[SETUP_PATH_MAX];
    char source[SETUP_PATH_MAX * 3], phase[32], started[32], acts[700] = "", left[300];
    int hm, hn, ho, hg;
    DWORD e = 0;

    dpath(N_GAME, g, SETUP_PATH_MAX);
    dpath(N_NEW, n, SETUP_PATH_MAX);
    dpath(N_OLD, o, SETUP_PATH_MAX);
    dpath(N_MARK, m, SETUP_PATH_MAX);
    hm = exists(m);
    hn = is_dir(n);
    ho = is_dir(o);
    hg = is_dir(g);
    if (!hm && !hn && !(ho && !hg))
        return;                         /* the usual start: nothing to do, nothing logged */
    journal_read(source, sizeof(source), phase, sizeof(phase), started, sizeof(started));
    fprintf(stderr, "[SETUP] recovery at start: journal %s%s%s, game_files %s, game_files.new %s, game_files.old %s\n",
            hm ? "there (phase " : "not there", hm ? phase : "", hm ? ")" : "", hg ? "there" : "missing",
            hn ? "there" : "not there", ho ? "there" : "not there");
    if (hm && hg && !hn && (!strcmp(phase, "placing") || !strcmp(phase, "finished"))) {
        /* The new files were in place: only the journal's removal was missed. Nothing to undo. */
        journal_remove();
        fprintf(stderr, "[SETUP] recovery: the last setup had put the new files in place; its journal removed\n");
        return;
    }
    g_rec.happened = 1;
    w8(source, g_rec.source, SETUP_PATH_MAX);
    if (!hg && ho) {                    /* stopped between the two renames (or game_files went missing) */
        if (move_retry(o, g, &e)) {
            g_rec.old_put_back = 1;
            hg = 1;
            snprintf(acts + strlen(acts), sizeof(acts) - strlen(acts), " Your earlier game files were put back "
                     "(game_files.old is game_files again).");
            fprintf(stderr, "[SETUP] recovery: game_files.old put back as game_files\n");
        } else {
            snprintf(acts + strlen(acts), sizeof(acts) - strlen(acts), " game_files.old couldn't be put back as "
                     "game_files (%s); it is still there.", err_words(e));
            fprintf(stderr, "[SETUP] recovery: game_files.old couldn't be put back (error %lu)\n", (unsigned long)e);
        }
    }
    if (hn) {
        unsigned rest;
        if (hg) {
            char err[400];
            int clash = 0, moved = carry(n, g, &clash, err, sizeof(err));
            if (moved > 0)
                snprintf(acts + strlen(acts), sizeof(acts) - strlen(acts), " Your saves and other files were moved "
                         "back into game_files.");
            if (moved < 0)
                fprintf(stderr, "[SETUP] recovery: moving back: %s\n", err);
        }
        rest = delete_disc_files(n, left, sizeof(left));
        if (rest) {
            wchar_t keep[SETUP_PATH_MAX];
            char kt[32], k8[SETUP_PATH_MAX * 3];
            SYSTEMTIME t;
            GetLocalTime(&t);
            snprintf(kt, sizeof(kt), "%04u-%02u-%02u_%02u-%02u-%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                     t.wSecond);
            _snwprintf_s(keep, SETUP_PATH_MAX, _TRUNCATE, L"%s\\game_files.leftover_%S", g_ldir, kt);
            MoveFileExW(n, keep, 0);
            u8(plain(keep), k8, sizeof(k8));
            snprintf(acts + strlen(acts), sizeof(acts) - strlen(acts), " Some files that aren't the disc's were "
                     "in the half-made copy (%s); they are kept in %s.", left, k8);
            fprintf(stderr, "[SETUP] recovery: game_files.new kept as %s: %u entries not from the disc (%s)\n", k8,
                    rest, left);
        } else
            fprintf(stderr, "[SETUP] recovery: the half-made game_files.new deleted\n");
    }
    if (hm)
        journal_remove();
    snprintf(g_rec.text, sizeof(g_rec.text), "The last setup didn't finish: it stopped %s%s%s%s. %s was cleaned "
             "up.%s Nothing else was changed.", hm ? phase_words(phase) : "before it could record anything",
             started[0] ? " (it started " : "", started, started[0] ? ")" : "",
             hn ? "The half-made copy (game_files.new)" : "What it left", acts);
    fprintf(stderr, "[SETUP] recovery: %s\n", g_rec.text);
    fflush(stderr);
}

static volatile LONG g_old_once;

static unsigned __stdcall old_cleanup_thread(void *arg)
{
    wchar_t o[SETUP_PATH_MAX];
    char left[300];
    unsigned rest;
    (void)arg;
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
    dpath(N_OLD, o, SETUP_PATH_MAX);
    rest = delete_disc_files(o, left, sizeof(left));
    if (rest)
        fprintf(stderr, "[SETUP] game_files.old kept: it holds %u files or folders that aren't the disc's (%s); "
                "its disc files were deleted\n", rest, left);
    else
        fprintf(stderr, "[SETUP] game_files.old deleted (the files the last setup replaced; this start's check "
                "passed)\n");
    return 0;
}

void twoc_setup_old_cleanup(void)
{
    wchar_t o[SETUP_PATH_MAX], m[SETUP_PATH_MAX];
    HANDLE t;
    dpath(N_OLD, o, SETUP_PATH_MAX);
    dpath(N_MARK, m, SETUP_PATH_MAX);
    if (!is_dir(o) || exists(m) || InterlockedExchange(&g_old_once, 1))
        return;
    t = (HANDLE)_beginthreadex(NULL, 0, old_cleanup_thread, NULL, 0, NULL);
    if (t)
        CloseHandle(t);
}

int twoc_setup_request_write(void)
{
    wchar_t r[SETUP_PATH_MAX];
    HANDLE h;
    DWORD put;
    char t[200];
    snprintf(t, sizeof(t), "%s: open the setup screen at the next start (F1 menu, Choose another disc image...). "
             "Deleted at that start.\r\n", twoc_app_name());
    dpath(N_REQ, r, SETUP_PATH_MAX);
    h = CreateFileW(r, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    WriteFile(h, t, (DWORD)strlen(t), &put, NULL);
    CloseHandle(h);
    fprintf(stderr, "[SETUP] the setup screen asked for at the next start (game_files.setup-request.txt)\n");
    return 0;
}

int twoc_setup_request_take(void)
{
    wchar_t r[SETUP_PATH_MAX];
    dpath(N_REQ, r, SETUP_PATH_MAX);
    if (!exists(r))
        return 0;
    delete_retry(r);
    fprintf(stderr, "[SETUP] the setup screen was asked for (the F1 menu's Choose another disc image...)\n");
    return 1;
}

/* ── The source: identify ──────────────────────────────────────────────── */

static void src_close(void)
{
    if (g_src.img)
        xdvd_close(g_src.img);
    memset(&g_src, 0, sizeof(g_src));
}

/* The source's file `path` as a wide \\?\ path (a folder source). */
static void src_file(const char *path, wchar_t *out, size_t cap)
{
    joinu(g_src.root, path, out, cap);
}

/* Reads a whole file of the source (at most `cap` bytes). Returns its size, or -1 with `err`. */
static long long src_read_all(const char *path, unsigned char *buf, size_t cap, char *err, size_t ecap)
{
    if (g_src.kind == SETUP_SRC_IMAGE) {
        const xdvd_entry_t *e = xdvd_find(g_src.img, path);
        if (!e || e->dir)
            return -2;
        if (e->size > cap) {
            snprintf(err, ecap, "%s is %u bytes: far larger than the game's", path, e->size);
            return -1;
        }
        if (xdvd_read(g_src.img, e, 0, buf, e->size, err, ecap))
            return -1;
        return e->size;
    } else {
        wchar_t p[SETUP_PATH_MAX];
        HANDLE h;
        LARGE_INTEGER sz;
        DWORD got = 0, e = 0;
        src_file(path, p, SETUP_PATH_MAX);
        h = open_retry(p, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, &e);
        if (h == INVALID_HANDLE_VALUE)
            return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? -2 :
                   (snprintf(err, ecap, "%s couldn't be read: %s", path, err_words(e)), -1);
        if (!GetFileSizeEx(h, &sz) || (uint64_t)sz.QuadPart > cap) {
            CloseHandle(h);
            snprintf(err, ecap, "%s is far larger than the game's", path);
            return -1;
        }
        if (!ReadFile(h, buf, (DWORD)sz.QuadPart, &got, NULL) || got != (DWORD)sz.QuadPart) {
            CloseHandle(h);
            snprintf(err, ecap, "%s couldn't be read to its end", path);
            return -1;
        }
        CloseHandle(h);
        return sz.QuadPart;
    }
}

/* A folder source's files (relative, '/'), counted against the manifest: extra = not on the disc. */
static unsigned folder_extra(const wchar_t *dir, const char *rel, char *names, size_t cap)
{
    wchar_t pat[SETUP_PATH_MAX + 4], sub[SETUP_PATH_MAX];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    unsigned n = 0;

    _snwprintf_s(pat, sizeof(pat) / sizeof(pat[0]), _TRUNCATE, L"%s\\*", dir);
    h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        char n8[600], r[1200];
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
            continue;
        u8(fd.cFileName, n8, sizeof(n8));
        snprintf(r, sizeof(r), "%s%s%s", rel, rel[0] ? "/" : "", n8);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            /* the setup's own and the game's own (saves, title data, backups) aren't the disc's: not counted */
            if (!rel[0] && (!_stricmp(n8, "UDATA") || !_stricmp(n8, "TDATA") || !_stricmp(n8, "UDATA_backups")))
                continue;
            _snwprintf_s(sub, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", dir, fd.cFileName);
            n += folder_extra(sub, r, names, cap);
        } else if (twoc_disc_find(r) < 0) {
            n++;
            if (names && strlen(names) + strlen(r) + 4 < cap)
                snprintf(names + strlen(names), cap - strlen(names), "%s%s", names[0] ? ", " : "", r);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

static int same_path(const wchar_t *a, const wchar_t *b)
{
    wchar_t fa[SETUP_PATH_MAX], fb[SETUP_PATH_MAX];
    if (!GetFullPathNameW(a, SETUP_PATH_MAX, fa, NULL) || !GetFullPathNameW(b, SETUP_PATH_MAX, fb, NULL))
        return 0;
    return CompareStringOrdinal(fa, -1, fb, -1, TRUE) == CSTR_EQUAL;
}

static unsigned __stdcall identify_thread(void *arg)
{
    wchar_t p[SETUP_PATH_MAX], probe[SETUP_PATH_MAX], g[SETUP_PATH_MAX], nw[SETUP_PATH_MAX];
    char err[1000], title[128], region_words[160], hex[65], line[1400], extra_names[400] = "";
    unsigned char *xbe = NULL;
    long long xn;
    unsigned tid = 0, region = 0, version = 0, missing = 0, wrong = 0, extra = 0, i;
    char miss_names[400] = "", wrong_names[400] = "";
    DWORD at;
    sha_t sha;
    (void)arg;

    longp(g_st.source, p, SETUP_PATH_MAX);
    at = GetFileAttributesW(p);
    if (at == INVALID_FILE_ATTRIBUTES) {
        DWORD e = GetLastError();
        snprintf(line, sizeof(line), "Nothing is there: %s (%s).", g_st.source_utf8, err_words(e));
        fail(line, "Check the path: it must name the disc image file (.iso) or the folder with the extracted game.");
        return 0;
    }
    if (at & FILE_ATTRIBUTE_DIRECTORY) {
        WIN32_FIND_DATAW fd;
        HANDLE h;
        wchar_t pat[SETUP_PATH_MAX + 4], one[SETUP_PATH_MAX] = L"";
        int found = 0;
        g_src.kind = SETUP_SRC_FOLDER;
        _snwprintf_s(probe, SETUP_PATH_MAX, _TRUNCATE, L"%s\\default.xbe", p);
        if (exists(probe)) {
            wcscpy_s(g_src.root, SETUP_PATH_MAX, p);
            snprintf(g_st.layout, sizeof(g_st.layout), "an extracted folder (default.xbe in it)");
        } else {                        /* one folder down: "Crash (USA)\default.xbe" */
            _snwprintf_s(pat, sizeof(pat) / sizeof(pat[0]), _TRUNCATE, L"%s\\*", p);
            h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchLimitToDirectories, NULL, 0);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !wcscmp(fd.cFileName, L".") ||
                        !wcscmp(fd.cFileName, L".."))
                        continue;
                    _snwprintf_s(probe, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s\\default.xbe", p, fd.cFileName);
                    if (exists(probe)) {
                        found++;
                        _snwprintf_s(one, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", p, fd.cFileName);
                    }
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
            if (found == 1) {
                char sub8[600];
                wcscpy_s(g_src.root, SETUP_PATH_MAX, one);
                u8(wcsrchr(one, L'\\') + 1, sub8, sizeof(sub8));
                snprintf(g_st.layout, sizeof(g_st.layout), "an extracted folder (default.xbe one folder down, in %s)",
                         sub8);
            } else {
                snprintf(line, sizeof(line), found ? "This folder holds several games (%d folders inside it have a "
                         "default.xbe)." : "This folder doesn't hold an extracted Xbox game: there is no default.xbe "
                         "in it, or in the folders right inside it.", found);
                fail(line, found ? "Choose the one folder that holds Crash Bandicoot: The Wrath of Cortex." :
                     "Choose the folder that holds default.xbe and the Crashdat folder (what extracting the disc "
                     "image gives), or choose the disc image (.iso) itself.");
                return 0;
            }
        }
        dpath(N_GAME, g, SETUP_PATH_MAX);
        dpath(N_NEW, nw, SETUP_PATH_MAX);
        if (same_path(g_src.root, nw)) {
            fail("That is the setup's own half-made copy (game_files.new).", "Choose the disc image or another folder.");
            return 0;
        }
        g_src.in_place = same_path(g_src.root, g);
    } else {
        g_src.kind = SETUP_SRC_IMAGE;
        if (xdvd_open(p, &g_src.img, err, sizeof(err))) {
            char why[1200];
            snprintf(why, sizeof(why), "This file can't be used: %s.", err);
            fail(why, strstr(err, "truncated") || strstr(err, "cut short") ?
                 "The image is incomplete: a download or copy that didn't finish, or a disk that ran out of space. "
                 "Make or copy the image again, then choose it." :
                 strstr(err, "opened") ? "Close any program that has it open, check you can read it, and Retry." :
                 "Choose the Xbox disc image (.iso) of Crash Bandicoot: The Wrath of Cortex, or the folder it was "
                 "extracted to.");
            return 0;
        }
        snprintf(g_st.layout, sizeof(g_st.layout), "a disc image: %s", xdvd_layout(g_src.img));
    }
    fprintf(stderr, "[SETUP] source %s: %s%s\n", g_st.source_utf8, g_st.layout, g_src.in_place ?
            " (game_files itself: checked in place)" : "");
    lock();
    g_st.kind = g_src.kind;
    g_st.in_place = g_src.in_place;
    unlock();

    /* default.xbe first: its SHA-256, then its certificate to say what it is. */
    xbe = (unsigned char *)malloc(64u << 20);
    if (!xbe) {
        fail("Out of memory.", "Close other programs and Retry.");
        return 0;
    }
    err[0] = 0;
    xn = src_read_all("default.xbe", xbe, 64u << 20, err, sizeof(err));
    if (xn < 0) {
        free(xbe);
        if (xn == -2)
            fail(g_src.kind == SETUP_SRC_IMAGE ? "This disc image has no default.xbe: it isn't an Xbox game disc, or "
                 "the image is damaged." : "default.xbe couldn't be found.",
                 "Choose the disc image of Crash Bandicoot: The Wrath of Cortex (Xbox, USA).");
        else {
            snprintf(line, sizeof(line), "default.xbe couldn't be read: %s.", err);
            fail(line, "If the image or folder is on a removable disk, check it is plugged in, then Retry.");
        }
        return 0;
    }
    if (sha_open(&sha) == 0) {
        BCryptHashData(sha.h, xbe, (ULONG)xn, 0);
        sha_hex(&sha, hex);
        sha_close(&sha);
    } else
        hex[0] = 0;
    title[0] = 0;
    if (twoc_xbe_describe(xbe, (size_t)xn, title, sizeof(title), &tid, &region, &version)) {
        free(xbe);
        fail("default.xbe isn't an Xbox program (its header is wrong): the image or folder is damaged, or it isn't "
             "an Xbox game.", "Choose the disc image of Crash Bandicoot: The Wrath of Cortex (Xbox, USA).");
        return 0;
    }
    free(xbe);
    twoc_region_name(region, region_words, sizeof(region_words));
    snprintf(g_st.found, sizeof(g_st.found), "%s (title id %08X, region %s, version %u)", title, tid, region_words,
             version);
    fprintf(stderr, "[SETUP] default.xbe: %lld bytes, SHA-256 %s; certificate: \"%s\", title id %08X, region %08X, "
            "version %08X\n", xn, hex, title, tid, region, version);
    if ((unsigned long long)xn != DM_XBE_SIZE || strcmp(hex, DM_XBE_SHA256) != 0) {
        char why[1400];
        if (tid != DM_TITLE_ID)
            snprintf(why, sizeof(why), "This is a different game: \"%s\" (title id %08X, region %s). This port runs "
                     "only %s.", title, tid, region_words, twoc_disc_name());
        else if (!(region & DM_REGION))
            snprintf(why, sizeof(why), "This is Crash Bandicoot: The Wrath of Cortex for another region: %s (title "
                     "id %08X, version %u). Only the USA release (North America, NTSC-U) is supported: the port is "
                     "built from that release's program.", region_words, tid, version);
        else if (version != DM_VERSION)
            snprintf(why, sizeof(why), "This is another version of Crash Bandicoot: The Wrath of Cortex (version %u, "
                     "region %s). Only version %u of the USA release is supported: the port is built from that "
                     "program.", version, region_words, DM_VERSION);
        else
            snprintf(why, sizeof(why), "This default.xbe says it is the USA release, but it differs from the disc's "
                     "(%lld bytes, SHA-256 %.16s...; the disc's: %llu bytes, %.16s...): it has been modified, patched "
                     "or damaged.", xn, hex, (unsigned long long)DM_XBE_SIZE, DM_XBE_SHA256);
        lock();
        g_st.found_ok = 0;
        unlock();
        fail(why, tid != DM_TITLE_ID ? "Choose the disc image of Crash Bandicoot: The Wrath of Cortex (Xbox, USA)." :
             "Use an unmodified image of the USA disc (one made from the disc, not patched).");
        return 0;
    }

    /* Then the file list and sizes. */
    for (i = 0; i < DM_N; i++) {
        const char *path = k_dm_files[i].path;
        uint64_t have = 0;
        int there;
        if (g_src.kind == SETUP_SRC_IMAGE) {
            const xdvd_entry_t *e = xdvd_find(g_src.img, path);
            there = e && !e->dir;
            have = there ? e->size : 0;
        } else {
            WIN32_FILE_ATTRIBUTE_DATA fa;
            wchar_t fp[SETUP_PATH_MAX];
            src_file(path, fp, SETUP_PATH_MAX);
            there = GetFileAttributesExW(fp, GetFileExInfoStandard, &fa) && !(fa.dwFileAttributes &
                    FILE_ATTRIBUTE_DIRECTORY);
            have = there ? ((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow : 0;
        }
        if (!there) {
            missing++;
            if (strlen(miss_names) + strlen(path) + 8 < sizeof(miss_names))
                snprintf(miss_names + strlen(miss_names), sizeof(miss_names) - strlen(miss_names), "%s%s",
                         miss_names[0] ? ", " : "", path);
        } else if (have != k_dm_files[i].size) {
            wrong++;
            if (strlen(wrong_names) + strlen(path) + 8 < sizeof(wrong_names))
                snprintf(wrong_names + strlen(wrong_names), sizeof(wrong_names) - strlen(wrong_names), "%s%s",
                         wrong_names[0] ? ", " : "", path);
        }
    }
    if (g_src.kind == SETUP_SRC_IMAGE) {
        size_t n, j;
        const xdvd_entry_t *e = xdvd_entries(g_src.img, &n);
        for (j = 0; j < n; j++)
            if (!e[j].dir && twoc_disc_find(e[j].path) < 0) {
                extra++;
                if (strlen(extra_names) + strlen(e[j].path) + 8 < sizeof(extra_names))
                    snprintf(extra_names + strlen(extra_names), sizeof(extra_names) - strlen(extra_names), "%s%s",
                             extra_names[0] ? ", " : "", e[j].path);
            }
    } else
        extra = folder_extra(g_src.root, "", extra_names, sizeof(extra_names));
    if (missing || wrong) {
        char why[1400], a[32], b[32];
        thousands(DM_N, b, sizeof(b));
        thousands(missing, a, sizeof(a));
        if (missing)
            snprintf(why, sizeof(why), "default.xbe is the right one, but %s of the disc's %s files are missing from "
                     "this %s (%s%s).%s", a, b, g_src.kind == SETUP_SRC_IMAGE ? "image" : "folder", miss_names,
                     missing > 3 ? ", ..." : "", wrong ? " Some others have the wrong size." : "");
        else
            snprintf(why, sizeof(why), "default.xbe is the right one, but %u files have the wrong size (%s%s): a "
                     "damaged or modified copy.", wrong, wrong_names, wrong > 3 ? ", ..." : "");
        fail(why, g_src.kind == SETUP_SRC_IMAGE ? "Make the image again from the disc (or copy it again), then choose "
             "it." : "Extract the disc image again into an empty folder, or choose the disc image itself.");
        return 0;
    }
    lock();
    g_st.found_ok = 1;
    g_st.state = SETUP_IDENTIFIED;
    unlock();
    fprintf(stderr, "[SETUP] the supported disc: default.xbe and all %u files with the disc's sizes%s%u not on the "
            "disc%s%s%s\n", DM_N, extra ? "; " : " (", extra, extra ? " (" : ")", extra ? extra_names : "",
            extra ? ", not copied)" : "");
    if (extra) {
        lock();
        snprintf(g_st.note, sizeof(g_st.note), "%u files there aren't on the disc and won't be copied (%s%s).", extra,
                 extra_names, extra > 3 ? ", ..." : "");
        unlock();
    }
    fflush(stderr);
    return 0;
}

/* ── Copy, verify, swap ────────────────────────────────────────────────── */

/* A file's data and metadata to the disk. FlushFileBuffers also makes the disk empty its own cache each time,
 * which costs ~15 ms a file here (2,267 files: the copy ran at 23 MB/s); NtFlushBuffersFileEx with
 * FLUSH_FLAGS_NO_SYNC (Windows 10 1709 on) writes the same data without that, and sync_disk's one
 * FlushFileBuffers before the swap empties the disk's cache once for all of them. */
typedef LONG (NTAPI *nt_flush_ex_t)(HANDLE, ULONG, PVOID, ULONG, PVOID);
static nt_flush_ex_t g_flush_ex;
static int g_flush_init;

static BOOL flush_file(HANDLE h)
{
    if (!g_flush_init) {
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        g_flush_ex = nt ? (nt_flush_ex_t)GetProcAddress(nt, "NtFlushBuffersFileEx") : NULL;
        g_flush_init = 1;
    }
    if (g_flush_ex) {
        ULONG_PTR iosb[2] = { 0, 0 };
        if (g_flush_ex(h, 0x2 /* FLUSH_FLAGS_NO_SYNC */, NULL, 0, iosb) >= 0)
            return TRUE;
    }
    return FlushFileBuffers(h);
}

/* The disk's own cache emptied, once (a FlushFileBuffers on one of the files written). */
static int sync_disk(const wchar_t *dir)
{
    wchar_t p[SETUP_PATH_MAX];
    DWORD e = 0;
    HANDLE h;
    BOOL ok;
    joinu(dir, "default.xbe", p, SETUP_PATH_MAX);
    h = open_retry(p, GENERIC_WRITE, FILE_SHARE_READ, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, &e);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    ok = FlushFileBuffers(h);
    CloseHandle(h);
    return ok ? 0 : -1;
}

static int make_dirs_for(const wchar_t *base, const char *rel)
{
    char d[1100];
    size_t j;
    for (j = 0; rel[j]; j++)
        if (rel[j] == '/') {
            wchar_t p[SETUP_PATH_MAX];
            memcpy(d, rel, j);
            d[j] = 0;
            joinu(base, d, p, SETUP_PATH_MAX);
            if (!CreateDirectoryW(p, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
                return -1;
        }
    return 0;
}

static unsigned char *g_buf;

static int cmp_offset(const void *a, const void *b)
{
    const xdvd_entry_t *x = xdvd_find(g_src.img, k_dm_files[*(const unsigned *)a].path);
    const xdvd_entry_t *y = xdvd_find(g_src.img, k_dm_files[*(const unsigned *)b].path);
    return x->offset < y->offset ? -1 : x->offset > y->offset;
}

/* Every disc file from the source into `dest`, hashed on the way. */
static int copy_all(const wchar_t *dest)
{
    unsigned *order, i, k, fail_at = 0, mbps = 0;
    sha_t sha;
    const char *f = getenv("RECOMP_DEV_SETUP_FAIL"), *t = getenv("RECOMP_DEV_SETUP_MBPS");
    char line[1400];
    LARGE_INTEGER t0;

    if (f && sscanf(f, "diskfull@%u", &fail_at) != 1)
        fail_at = 0;
    if (t)
        mbps = (unsigned)atoi(t);
    order = (unsigned *)malloc(DM_N * sizeof(unsigned));
    if (!order || sha_open(&sha)) {
        free(order);
        return fail("Out of memory, or Windows' SHA-256 isn't available.", "Close other programs and Retry.");
    }
    for (i = 0; i < DM_N; i++)
        order[i] = i;
    if (g_src.kind == SETUP_SRC_IMAGE)
        qsort(order, DM_N, sizeof(unsigned), cmp_offset);   /* the image read front to back */
    phase_begin(SETUP_COPYING, DM_BYTES, DM_N);
    QueryPerformanceCounter(&t0);
    for (k = 0; k < DM_N; k++) {
        const dm_file_t *m = &k_dm_files[order[k]];
        const xdvd_entry_t *e = g_src.kind == SETUP_SRC_IMAGE ? xdvd_find(g_src.img, m->path) : NULL;
        wchar_t out[SETUP_PATH_MAX], in[SETUP_PATH_MAX];
        HANDLE ho, hi = INVALID_HANDLE_VALUE;
        uint64_t at = 0;
        DWORD err = 0;
        char hex[65], o8[SETUP_PATH_MAX * 3];

        if (cancelled())
            goto stop;
        joinu(dest, m->path, out, SETUP_PATH_MAX);
        if (make_dirs_for(dest, m->path)) {
            err = GetLastError();
            u8(plain(out), o8, sizeof(o8));
            snprintf(line, sizeof(line), "A folder couldn't be made for %s: %s (Windows error %lu).", m->path,
                     err_words(err), (unsigned long)err);
            goto failed;
        }
        if (g_src.kind == SETUP_SRC_FOLDER) {
            src_file(m->path, in, SETUP_PATH_MAX);
            hi = open_retry(in, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, &err);
            if (hi == INVALID_HANDLE_VALUE) {
                snprintf(line, sizeof(line), "%s couldn't be read from the folder: %s (Windows error %lu).", m->path,
                         err_words(err), (unsigned long)err);
                goto failed;
            }
        }
        ho = open_retry(out, GENERIC_WRITE, 0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, &err);
        if (ho == INVALID_HANDLE_VALUE) {
            if (hi != INVALID_HANDLE_VALUE)
                CloseHandle(hi);
            snprintf(line, sizeof(line), "%s couldn't be created in game_files.new: %s (Windows error %lu).", m->path,
                     err_words(err), (unsigned long)err);
            goto failed;
        }
        progress(0, m->path, 0);
        while (at < m->size) {
            uint32_t n = (uint32_t)(m->size - at < CHUNK ? m->size - at : CHUNK);
            DWORD put = 0, got = 0;
            char rerr[600];
            if (cancelled()) {
                CloseHandle(ho);
                if (hi != INVALID_HANDLE_VALUE)
                    CloseHandle(hi);
                goto stop;
            }
            if (g_src.kind == SETUP_SRC_IMAGE) {
                if (xdvd_read(g_src.img, e, at, g_buf, n, rerr, sizeof(rerr))) {
                    CloseHandle(ho);
                    snprintf(line, sizeof(line), "The disc image couldn't be read (at %s): %s.", m->path, rerr);
                    goto failed;
                }
            } else if (!ReadFile(hi, g_buf, n, &got, NULL) || got != n) {
                err = GetLastError();
                CloseHandle(ho);
                CloseHandle(hi);
                snprintf(line, sizeof(line), "%s couldn't be read from the folder to its end: %s.", m->path,
                         got != n && !err ? "it is shorter than when it was checked" : err_words(err));
                goto failed;
            }
            BCryptHashData(sha.h, g_buf, n, 0);
            if (fail_at && k + 1 == fail_at) {
                SetLastError(ERROR_DISK_FULL);
                fprintf(stderr, "[SETUP] test fault (RECOMP_DEV_SETUP_FAIL): file %u's write fails, disk full\n", k + 1);
                fail_at = 0;
                put = 0;
            } else if (!WriteFile(ho, g_buf, n, &put, NULL))
                put = 0;
            if (put != n) {
                char a[32];
                err = GetLastError();
                CloseHandle(ho);
                if (hi != INVALID_HANDLE_VALUE)
                    CloseHandle(hi);
                thousands((DM_BYTES - g_st.bytes_done) / 1048576 + 64, a, sizeof(a));
                if (err == ERROR_DISK_FULL || err == ERROR_HANDLE_DISK_FULL)
                    snprintf(line, sizeof(line), "The disk filled up while copying (at %s): about %s MB more is "
                             "needed.", m->path, a);
                else
                    snprintf(line, sizeof(line), "%s couldn't be written: %s (Windows error %lu).", m->path,
                             err_words(err), (unsigned long)err);
                goto failed;
            }
            at += n;
            progress(n, NULL, 0);
            if (mbps) {                 /* the test's slow disk */
                double want = (double)g_st.bytes_done / 1048576.0 / mbps, have = secs_since(t0);
                if (want > have)
                    Sleep((DWORD)((want - have) * 1000.0));
            }
        }
        if (hi != INVALID_HANDLE_VALUE)
            CloseHandle(hi);
        if (!flush_file(ho)) {
            err = GetLastError();
            CloseHandle(ho);
            snprintf(line, sizeof(line), "%s couldn't be written to the disk: %s (Windows error %lu).", m->path,
                     err_words(err), (unsigned long)err);
            goto failed;
        }
        CloseHandle(ho);
        sha_hex(&sha, hex);
        if (strcmp(hex, m->sha256)) {
            snprintf(line, sizeof(line), "%s on the %s isn't the disc's: its contents differ (SHA-256 %.16s..., the "
                     "disc's %.16s...). The %s is damaged.", m->path, g_src.kind == SETUP_SRC_IMAGE ? "image" :
                     "folder", hex, m->sha256, g_src.kind == SETUP_SRC_IMAGE ? "image" : "copy");
            sha_close(&sha);
            free(order);
            return fail(line, g_src.kind == SETUP_SRC_IMAGE ? "Make the image again from the disc (or download or "
                        "copy it again), then choose it." : "Extract the disc image again, or choose the image itself.");
        }
        progress(0, NULL, 1);
        dev_hold("copy", k + 1);
    }
    sha_close(&sha);
    free(order);
    for (k = 0; k_dm_empty_dirs[k]; k++) {     /* the disc's folders with no file in them, made too */
        char rel[1100];
        snprintf(rel, sizeof(rel), "%s/", k_dm_empty_dirs[k]);
        make_dirs_for(dest, rel);
    }
    fprintf(stderr, "[SETUP] copied %u files, %llu bytes in %.1f s (%.0f MB/s)\n", DM_N, (unsigned long long)DM_BYTES,
            secs_since(t0), (double)DM_BYTES / 1048576.0 / (secs_since(t0) > 0 ? secs_since(t0) : 1));
    return 0;
failed:
    sha_close(&sha);
    free(order);
    return fail(line, strstr(line, "disk filled") ? "Free some disk space (the Recycle Bin, Downloads), or move the "
                "game's folder to a disk with more room, then Retry." :
                strstr(line, "access is denied") || strstr(line, "write-protected") ? "This folder can't be written "
                "to. Move the game's folder somewhere you can write, such as Documents, then start it again." :
                strstr(line, "another program") ? "Close the program using it (a file manager, an antivirus scan), "
                "then Retry." : "Retry; if it fails again, choose another copy of the game.");
stop:
    sha_close(&sha);
    free(order);
    set_state(SETUP_CANCELLED);
    return 1;
}

/* Every disc file in `dir` against the manifest: size and SHA-256 from the disk. `bad` names the first few. */
static int verify_dir(const wchar_t *dir, unsigned *nbad, char *bad, size_t cap)
{
    unsigned i;
    sha_t sha;
    *nbad = 0;
    bad[0] = 0;
    if (sha_open(&sha))
        return fail("Windows' SHA-256 isn't available.", "");
    phase_begin(SETUP_VERIFYING, DM_BYTES, DM_N);
    for (i = 0; i < DM_N; i++) {
        const dm_file_t *m = &k_dm_files[i];
        wchar_t p[SETUP_PATH_MAX];
        HANDLE h;
        DWORD err = 0, got;
        uint64_t total = 0;
        char hex[65];
        int ok = 1;

        if (cancelled()) {
            sha_close(&sha);
            set_state(SETUP_CANCELLED);
            return 1;
        }
        joinu(dir, m->path, p, SETUP_PATH_MAX);
        progress(0, m->path, 0);
        h = open_retry(p, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, &err);
        if (h == INVALID_HANDLE_VALUE)
            ok = 0;
        else {
            while (ReadFile(h, g_buf, CHUNK, &got, NULL) && got) {
                BCryptHashData(sha.h, g_buf, got, 0);
                total += got;
                progress(got, NULL, 0);
                if (cancelled())
                    break;
            }
            CloseHandle(h);
            sha_hex(&sha, hex);
            ok = total == m->size && !strcmp(hex, m->sha256);
        }
        if (!ok) {
            (*nbad)++;
            if (strlen(bad) + strlen(m->path) + 24 < cap)
                snprintf(bad + strlen(bad), cap - strlen(bad), "%s%s (%s)", bad[0] ? ", " : "", m->path,
                         h == INVALID_HANDLE_VALUE ? (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND ?
                         "missing" : "unreadable") : total != m->size ? "wrong size" : "different contents");
        }
        progress(0, NULL, 1);
        dev_hold("verify", i + 1);
    }
    sha_close(&sha);
    return 0;
}

static unsigned __stdcall install_thread(void *arg)
{
    int replace = (int)(intptr_t)arg, r;
    wchar_t g[SETUP_PATH_MAX], n[SETUP_PATH_MAX], o[SETUP_PATH_MAX];
    ULARGE_INTEGER fr;
    uint64_t freeb;
    const char *fm = getenv("RECOMP_DEV_SETUP_FREE_MB");
    char line[1400], a[32], b[32], bad[600], left[300];
    unsigned nbad = 0;
    DWORD e = 0;
    LARGE_INTEGER t0;
    int made_new = 0, hg;

    QueryPerformanceCounter(&t0);
    dpath(N_GAME, g, SETUP_PATH_MAX);
    dpath(N_NEW, n, SETUP_PATH_MAX);
    dpath(N_OLD, o, SETUP_PATH_MAX);
    hg = is_dir(g);
    if (g_src.in_place) {               /* game_files itself: checked, nothing copied */
        if (verify_dir(g, &nbad, bad, sizeof(bad)) != 0)
            goto out;
        if (nbad) {
            snprintf(line, sizeof(line), "Your game_files folder is the game, but %u of its files aren't the disc's: "
                     "%s.", nbad, bad);
            fail(line, "Choose the disc image instead: the setup makes a fresh copy from it.");
            goto out;
        }
        lock();
        snprintf(g_st.note, sizeof(g_st.note), "game_files already holds the disc's %u files (every size and SHA-256 "
                 "checked); nothing was copied.", DM_N);
        g_st.state = SETUP_DONE;
        unlock();
        fprintf(stderr, "[SETUP] game_files checked in place: all %u files are the disc's\n", DM_N);
        goto out;
    }
    if (hg && !replace) {
        fail("game_files is already there, and replacing it wasn't confirmed.", "");
        goto out;
    }
    /* The game's own file code (the Xbox kernel's paths, its saves) works with paths up to 260 characters: the
     * deepest disc file and save are ~56 characters below the data folder. */
    if (wcslen(g_dir) > MAX_DATA_DIR) {
        snprintf(line, sizeof(line), "The game's folder is too deep: its path is %u characters long (%s), and the game "
                 "can't open its files below one longer than %u.", (unsigned)wcslen(g_dir), g_dir8, MAX_DATA_DIR);
        char hint[200];
        snprintf(hint, sizeof(hint), "Move the game's folder to a shorter path, such as C:\\Games\\%s, then start "
                 "it again.", twoc_app_name());
        fail(line, hint);
        goto out;
    }
    /* Room on the disk: the disc's bytes and a little more (the files a replace takes out stay until the next
     * start, so nothing is freed meanwhile). */
    if (!GetDiskFreeSpaceExW(g_ldir, &fr, NULL, NULL))
        fr.QuadPart = ~0ull;
    freeb = fm ? (uint64_t)atoll(fm) << 20 : fr.QuadPart;
    lock();
    g_st.free_bytes = freeb;
    g_st.need_bytes = DM_BYTES + FREE_MARGIN;
    g_st.replace = replace && hg;
    unlock();
    if (freeb < DM_BYTES + FREE_MARGIN) {
        snprintf(a, sizeof(a), "%.2f", (double)freeb / 1073741824.0);
        snprintf(b, sizeof(b), "%.2f", (double)(DM_BYTES + FREE_MARGIN) / 1073741824.0);
        snprintf(line, sizeof(line), "Not enough free disk space for the game files in %s: %s GB free, about %s GB "
                 "needed.", g_dir8, a, b);
        fail(line, "Free some space (empty the Recycle Bin, clear Downloads), or move the game's folder to a disk with "
             "more room, then Retry.");
        goto out;
    }
    /* Leftovers of an earlier replace: the old files' disc files go now; anything else in there is kept aside. */
    if (is_dir(o) && delete_disc_files(o, left, sizeof(left))) {
        wchar_t keep[SETUP_PATH_MAX];
        SYSTEMTIME t;
        GetLocalTime(&t);
        _snwprintf_s(keep, SETUP_PATH_MAX, _TRUNCATE, L"%s\\game_files.old_%04u-%02u-%02u_%02u-%02u-%02u", g_ldir,
                     t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        if (!move_retry(o, keep, &e)) {
            fail("game_files.old (from an earlier setup) couldn't be moved aside.", "Close any program using it, then "
                 "Retry.");
            goto out;
        }
        fprintf(stderr, "[SETUP] game_files.old had files not from the disc (%s): kept aside\n", left);
    }
    if (is_dir(n) && delete_disc_files(n, left, sizeof(left))) {
        fail("A game_files.new folder is in the way and holds files that aren't the disc's.", "Move or delete "
             "game_files.new yourself, then Retry.");
        goto out;
    }
    /* The journal first, then the folder. */
    if (journal("copying")) {
        snprintf(line, sizeof(line), "The game's folder can't be written to: %s.", g_dir8);
        fail(line, "Move the game's folder somewhere you can write, such as Documents, then start it again.");
        goto out;
    }
    if (!CreateDirectoryW(n, NULL)) {
        e = GetLastError();
        snprintf(line, sizeof(line), "game_files.new couldn't be made in %s: %s (Windows error %lu).", g_dir8,
                 err_words(e), (unsigned long)e);
        fail(line, "Move the game's folder somewhere you can write, such as Documents, then start it again.");
        goto cleanup;
    }
    made_new = 1;
    r = copy_all(n);
    if (r)
        goto cleanup;
    if (journal("verifying"))
        goto journal_lost;
    if (verify_dir(n, &nbad, bad, sizeof(bad)))
        goto cleanup;
    if (nbad) {
        snprintf(line, sizeof(line), "%u copied files didn't read back as written: %s. The disk may be failing.", nbad,
                 bad);
        fail(line, "Retry; if it fails again, try another disk.");
        goto cleanup;
    }
    if (sync_disk(n)) {
        e = GetLastError();
        snprintf(line, sizeof(line), "The copied files couldn't be written to the disk for good: %s (Windows error "
                 "%lu).", err_words(e), (unsigned long)e);
        fail(line, "Retry; if it fails again, try another disk.");
        goto cleanup;
    }
    fprintf(stderr, "[SETUP] game_files.new checked from the disk: all %u files, every size and SHA-256; the disk's "
            "cache flushed (%s)\n", DM_N, g_flush_ex ? "each file flushed without a sync, then one sync" :
            "FlushFileBuffers on each file");
    phase_begin(SETUP_SWAPPING, 0, 0);
    if (hg) {
        int moved, clash = 0;
        char err[400];
        if (journal("carrying"))
            goto journal_lost;
        dev_hold("carry", 0);
        moved = carry(g, n, &clash, err, sizeof(err));
        if (moved < 0) {
            int back = carry(n, g, NULL, err, sizeof(err));
            (void)back;
            snprintf(line, sizeof(line), "Your saves couldn't be moved into the new folder: %s. They were put back.",
                     err);
            fail(line, "Close any program using the game's folder (a file manager, a backup tool), then Retry.");
            goto cleanup;
        }
        if (journal("renaming"))
            goto journal_lost;
        if (!move_retry(g, o, &e)) {
            carry(n, g, NULL, err, sizeof(err));
            snprintf(line, sizeof(line), "The old game_files couldn't be moved aside: %s (Windows error %lu). Nothing "
                     "was changed: your saves are back in it.", err_words(e), (unsigned long)e);
            fail(line, "Close any program that has a file in game_files open (a file manager, an antivirus scan), "
                 "then Retry.");
            goto cleanup;
        }
        if (journal("placing"))
            fprintf(stderr, "[SETUP] the journal couldn't be updated (placing): going on\n");
        dev_hold("renames", 0);
        if (!move_retry(n, g, &e)) {
            DWORD e2 = 0;
            char err2[400];
            if (move_retry(o, g, &e2))     /* the rollback: the old folder back, the saves back into it */
                carry(n, g, NULL, err2, sizeof(err2));
            snprintf(line, sizeof(line), "The new game files couldn't be put in place: %s (Windows error %lu). %s",
                     err_words(e), (unsigned long)e, e2 ? "game_files.old couldn't be put back either: start the game "
                     "again and it will do it." : "Your old game files and saves are back as they were.");
            fail(line, "Close any program using the game's folder, then Retry.");
            goto cleanup;
        }
        fprintf(stderr, "[SETUP] swapped: game_files -> game_files.old, game_files.new -> game_files (%d items of "
                "yours moved across%s)\n", moved, clash ? ", some left in game_files.old" : "");
    } else {
        if (journal("placing"))
            goto journal_lost;
        if (!move_retry(n, g, &e)) {
            snprintf(line, sizeof(line), "The new game files couldn't be put in place: %s (Windows error %lu).",
                     err_words(e), (unsigned long)e);
            fail(line, "Close any program using the game's folder, then Retry.");
            goto cleanup;
        }
        fprintf(stderr, "[SETUP] game_files.new -> game_files\n");
    }
    journal("finished");
    journal_remove();
    lock();
    snprintf(g_st.note + strlen(g_st.note), sizeof(g_st.note) - strlen(g_st.note), "%s%u files, %.0f MB, copied and "
             "every size and SHA-256 checked in %.0f s.%s", g_st.note[0] ? " " : "", DM_N,
             (double)DM_BYTES / 1048576.0, secs_since(t0), hg ? " The files they replaced are kept as game_files.old "
             "until the next start; your saves were moved across." : "");
    g_st.state = SETUP_DONE;
    unlock();
    fprintf(stderr, "[SETUP] done in %.1f s\n", secs_since(t0));
    goto out;
journal_lost:
    fail("The setup's journal file couldn't be written (the folder became read-only, or the disk is full).",
         "Free some disk space, then Retry.");
cleanup:
    {   /* only what this run made: game_files.new's disc files and the folders they leave empty, the journal */
        unsigned rest = made_new || is_dir(n) ? delete_disc_files(n, left, sizeof(left)) : 0;
        int was = g_st.state;
        journal_remove();
        lock();
        snprintf(g_st.note, sizeof(g_st.note), "%s%s", made_new ? "The half-made copy (game_files.new) was deleted. " :
                 "", hg ? "Your game_files and saves weren't changed." : "Nothing else was changed.");
        if (rest)
            snprintf(g_st.note + strlen(g_st.note), sizeof(g_st.note) - strlen(g_st.note), " game_files.new was "
                     "kept: it holds %u things that aren't the disc's (%s).", rest, left);
        g_st.state = was == SETUP_CANCELLED ? SETUP_CANCELLED : SETUP_FAILED;
        unlock();
        fprintf(stderr, "[SETUP] %s; cleaned up: %s\n", was == SETUP_CANCELLED ? "cancelled" : "stopped", g_st.note);
    }
out:
    fflush(stderr);
    return 0;
}

static unsigned __stdcall verify_thread(void *arg)
{
    wchar_t g[SETUP_PATH_MAX];
    char bad[700], line[1400];
    unsigned nbad = 0;
    LARGE_INTEGER t0;
    (void)arg;
    QueryPerformanceCounter(&t0);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    dpath(N_GAME, g, SETUP_PATH_MAX);
    if (verify_dir(g, &nbad, bad, sizeof(bad)) == 0) {
        if (nbad) {
            snprintf(line, sizeof(line), "%u of the %u game files aren't the disc's: %s.", nbad, DM_N, bad);
            fail(line, "Choose another disc image... sets them up again from your disc image (your saves are kept).");
        } else {
            lock();
            snprintf(g_st.note, sizeof(g_st.note), "All %u game files are the disc's (every size and SHA-256), checked "
                     "in %.1f s.", DM_N, secs_since(t0));
            g_st.state = SETUP_DONE;
            unlock();
        }
        fprintf(stderr, "[SETUP] Verify game files: %u of %u differ%s%s (%.1f s)\n", nbad, DM_N, nbad ? ": " : "",
                nbad ? bad : "", secs_since(t0));
    }
    return 0;
}

static int start(unsigned (__stdcall *fn)(void *), void *arg)
{
    if (twoc_setup_busy())
        return -1;
    if (g_thread) {
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    if (!g_buf && (g_buf = (unsigned char *)malloc(CHUNK)) == NULL)
        return -1;
    g_cancel = 0;
    g_thread = (HANDLE)_beginthreadex(NULL, 0, fn, arg, 0, NULL);
    return g_thread ? 0 : -1;
}

int twoc_setup_identify(const wchar_t *path)
{
    wchar_t clean[SETUP_PATH_MAX];
    size_t l;

    if (twoc_setup_busy())
        return -1;
    /* A typed or pasted path: quotes and spaces around it off ("Copy as path" adds quotes). */
    while (*path == L' ' || *path == L'\t' || *path == L'"')
        path++;
    wcsncpy_s(clean, SETUP_PATH_MAX, path, _TRUNCATE);
    l = wcslen(clean);
    while (l && (clean[l - 1] == L' ' || clean[l - 1] == L'\t' || clean[l - 1] == L'"' || clean[l - 1] == L'\r' ||
                 clean[l - 1] == L'\n'))
        clean[--l] = 0;
    while (l > 3 && (clean[l - 1] == L'\\' || clean[l - 1] == L'/'))
        clean[--l] = 0;
    src_close();
    lock();
    memset(&g_st, 0, sizeof(g_st));
    if (!GetFullPathNameW(clean, SETUP_PATH_MAX, g_st.source, NULL))
        wcscpy_s(g_st.source, SETUP_PATH_MAX, clean);
    u8(g_st.source, g_st.source_utf8, sizeof(g_st.source_utf8));
    g_st.state = SETUP_IDENTIFYING;
    g_st.eta_s = -1;
    g_st.replace = twoc_setup_game_files_exist();
    unlock();
    fprintf(stderr, "[SETUP] chosen: %s\n", g_st.source_utf8);
    return start(identify_thread, NULL);
}

int twoc_setup_install(int replace)
{
    if (g_st.state != SETUP_IDENTIFIED || !g_st.found_ok)
        return -1;
    if (g_thread) {
        WaitForSingleObject(g_thread, INFINITE);
    }
    fprintf(stderr, "[SETUP] install from %s%s\n", g_st.source_utf8, replace ? " (replacing game_files: confirmed)" :
            "");
    return start(install_thread, (void *)(intptr_t)replace);
}

int twoc_setup_verify(void)
{
    if (twoc_setup_busy())
        return -1;
    lock();
    memset(&g_st, 0, sizeof(g_st));
    g_st.verify_only = 1;
    g_st.state = SETUP_VERIFYING;
    g_st.eta_s = -1;
    unlock();
    return start(verify_thread, NULL);
}

void twoc_setup_cancel(void)
{
    if (twoc_setup_busy() && !InterlockedExchange(&g_cancel, 1))
        fprintf(stderr, "[SETUP] cancel asked for\n");  /* once (a closing window asks every frame) */
}

void twoc_setup_status(setup_status_t *out)
{
    lock();
    *out = g_st;
    if (g_st.state >= SETUP_COPYING && g_st.state <= SETUP_SWAPPING)
        out->secs = secs_since(g_t0);
    unlock();
}

int twoc_setup_busy(void)
{
    return g_thread && WaitForSingleObject(g_thread, 0) == WAIT_TIMEOUT;
}

void twoc_setup_wait(void)
{
    if (g_thread)
        WaitForSingleObject(g_thread, INFINITE);
}

void twoc_setup_reset(void)
{
    if (twoc_setup_busy())
        return;
    src_close();
    lock();
    memset(&g_st, 0, sizeof(g_st));
    unlock();
}
