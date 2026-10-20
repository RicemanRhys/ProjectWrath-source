/**
 * Save backups (step 83, notes 18.86). See saves.h.
 *
 * Everything that touches the disk after the start runs on one worker
 * thread ("twoc save backups"): the copies, the deletions, the restore's
 * scheduling, Explorer. The game's thread only counts its own opens and
 * closes of save files (a few interlocked operations, and a table lock for
 * the handles it has open there) and wakes the worker.
 *
 * A copy and the game's open of a save file must not overlap: the game's
 * open could fail on the copy's handle (a share mode without read), or the
 * copy read a file the game is rewriting. So the worker announces a copy
 * (g_copying) before it checks that the game has no save file open or
 * opening (g_guest_busy), and the game announces an open before it checks
 * for a copy: one of the two always sees the other. The worker then backs
 * off and tries again a second later, and a copy the game opened a file
 * during (g_guest_gen moved) is thrown away. Only an open whose share mode
 * excludes reading waits for a copy under way (the read of two files of a
 * few KB, well under a millisecond, and only if the game opens a save file
 * in that instant).
 */

#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "saves.h"

#define SV_GAME_DIR     L"game_files"
#define SV_SAVE_SUB     L"UDATA\\56550003\\4D5C926D6357"
#define SV_BACKUP_SUB   L"UDATA_backups"
#define SV_PENDING      L"restore at next start.txt"
#define SV_INFO         L"backup.txt"
#define SV_PARTIAL      L"_in progress"
#define SV_SETTLE_MS    2000            /* after the game closes a save file it wrote */
#define SV_RETRY_MS     1000            /* the game had a save file open: again after this */
#define SV_KEEP_LAST    10
#define SV_KEEP_DAYS    14
#define SV_MAX_LIST     256
#define SV_MAX_FILE     (16u << 20)
#define SV_NFILES       2

static const wchar_t *const k_files[SV_NFILES] = { L"crash.sav", L"SaveMeta.xbx" };

typedef struct {
    unsigned char *d[SV_NFILES];
    DWORD n[SV_NFILES];
    int have[SV_NFILES];
} snap_t;

static volatile LONG g_init;
static CRITICAL_SECTION g_cs;           /* the list, the state and the requests below */
static int g_on;
static wchar_t g_save[MAX_PATH], g_bak[MAX_PATH];
static saves_entry_t g_list[SV_MAX_LIST];
static int g_n;
static char g_status[320], g_pending[32], g_dir_u8[512];
static int g_busy_ui;

enum { RQ_RESTORE = 1, RQ_CANCEL = 2, RQ_OPEN = 4, RQ_REFRESH = 8 };
static HANDLE g_wake;
static LONG g_rq;
static char g_rq_name[32];
static ULONGLONG g_due;                 /* GetTickCount64 for the next backup; 0: none */
static const char *g_due_why;

/* The game's save files open now (guest handles). */
#define SV_MAX_H 32
static CRITICAL_SECTION g_hcs;
static struct { uint32_t h; int write; } g_h[SV_MAX_H];
static int g_hn;
static volatile LONG g_tracked;
static volatile LONG g_guest_busy;      /* opens under way + save files open */
static volatile LONG g_guest_gen;       /* save-folder opens so far */
static volatile LONG g_copying;
static DWORD g_dev_copy_ms;             /* RECOMP_DEV_SAVE_COPY_DELAY_MS: test only, 0 */
static volatile LONG g_closing;         /* step 97: the game is quitting: the worker takes on nothing new */
static volatile LONG g_working;         /* ... the worker is doing something (a copy, a restore, a list) */

/* ── small helpers ─────────────────────────────────────────── */

static void u8(const wchar_t *w, char *out, size_t cap)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL))
        out[0] = 0;
}

/* Under g_cs. */
static void status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_status, sizeof(g_status), fmt, ap);
    va_end(ap);
}

static void say(const char *fmt, ...)
{
    va_list ap;
    EnterCriticalSection(&g_cs);
    va_start(ap, fmt);
    vsnprintf(g_status, sizeof(g_status), fmt, ap);
    va_end(ap);
    LeaveCriticalSection(&g_cs);
}

static const char *err_text(DWORD e, char *buf, size_t cap)
{
    size_t n;
    if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0, buf, (DWORD)cap,
                        NULL))
        snprintf(buf, cap, "error %lu", (unsigned long)e);
    n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == '.' || buf[n - 1] == ' '))
        buf[--n] = 0;
    return buf;
}

/* yyyy-mm-dd_hh-mm-ss, then nothing or _N: a backup's folder. */
static int backup_name(const wchar_t *n, SYSTEMTIME *t)
{
    static const char pat[] = "dddd-dd-dd_dd-dd-dd";
    int i;
    for (i = 0; pat[i]; i++)
        if (pat[i] == 'd' ? (n[i] < L'0' || n[i] > L'9') : n[i] != (wchar_t)pat[i])
            return 0;
    if (n[19]) {
        if (n[19] != L'_' || !n[20])
            return 0;
        for (i = 20; n[i]; i++)
            if (n[i] < L'0' || n[i] > L'9' || i > 24)
                return 0;
    }
    if (t) {
        memset(t, 0, sizeof(*t));
        t->wYear = (WORD)_wtoi(n);
        t->wMonth = (WORD)_wtoi(n + 5);
        t->wDay = (WORD)_wtoi(n + 8);
        t->wHour = (WORD)_wtoi(n + 11);
        t->wMinute = (WORD)_wtoi(n + 14);
        t->wSecond = (WORD)_wtoi(n + 17);
    }
    return 1;
}

static long day_number(WORD y, WORD m, WORD d)
{
    SYSTEMTIME t;
    FILETIME ft;
    memset(&t, 0, sizeof(t));
    t.wYear = y;
    t.wMonth = m;
    t.wDay = d;
    if (!SystemTimeToFileTime(&t, &ft))
        return 0;
    return (long)((((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 864000000000ULL);
}

/* 1: read; 0: no such file; -1: an error (GetLastError). */
static int read_file(const wchar_t *path, unsigned char **out, DWORD *n)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER sz;
    DWORD got = 0, e;
    unsigned char *b;

    *out = NULL;
    *n = 0;
    if (h == INVALID_HANDLE_VALUE) {
        e = GetLastError();
        return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? 0 : -1;
    }
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > SV_MAX_FILE) {
        e = sz.QuadPart > SV_MAX_FILE ? ERROR_FILE_TOO_LARGE : GetLastError();
        CloseHandle(h);
        SetLastError(e);
        return -1;
    }
    b = (unsigned char *)malloc(sz.QuadPart ? (size_t)sz.QuadPart : 1);
    if (!b || (sz.QuadPart && (!ReadFile(h, b, (DWORD)sz.QuadPart, &got, NULL) || got != (DWORD)sz.QuadPart))) {
        e = b ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY;
        free(b);
        CloseHandle(h);
        SetLastError(e ? e : ERROR_READ_FAULT);
        return -1;
    }
    CloseHandle(h);
    *out = b;
    *n = (DWORD)sz.QuadPart;
    return 1;
}

static int write_file(const wchar_t *path, const void *d, DWORD n, int flush)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD put = 0, e;
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    if ((n && (!WriteFile(h, d, n, &put, NULL) || put != n)) || (flush && !FlushFileBuffers(h))) {
        e = GetLastError();
        CloseHandle(h);
        DeleteFileW(path);
        SetLastError(e ? e : ERROR_WRITE_FAULT);
        return 0;
    }
    CloseHandle(h);
    return 1;
}

static void snap_free(snap_t *s)
{
    int i;
    for (i = 0; i < SV_NFILES; i++)
        free(s->d[i]);
    memset(s, 0, sizeof(*s));
}

/* 1: read (have[] says which), -1: an error. */
static int snap_read(const wchar_t *dir, snap_t *s)
{
    wchar_t p[MAX_PATH];
    int i, r;
    memset(s, 0, sizeof(*s));
    for (i = 0; i < SV_NFILES; i++) {
        swprintf(p, MAX_PATH, L"%s\\%s", dir, k_files[i]);
        r = read_file(p, &s->d[i], &s->n[i]);
        if (r < 0) {
            DWORD e = GetLastError();
            snap_free(s);
            SetLastError(e);
            return -1;
        }
        s->have[i] = r;
    }
    return 1;
}

static int snap_equal(const snap_t *a, const snap_t *b)
{
    int i;
    for (i = 0; i < SV_NFILES; i++)
        if (a->have[i] != b->have[i] || a->n[i] != b->n[i] || (a->n[i] && memcmp(a->d[i], b->d[i], a->n[i])))
            return 0;
    return 1;
}

/* The game's save as it is now. 1: taken; 0: no save (no crash.sav); -1:
 * an error; -2: the game has a save file open, or opened one meanwhile.
 * `guarded` once the game runs. */
static int snap_game(snap_t *s, int guarded)
{
    LONG gen = InterlockedCompareExchange(&g_guest_gen, 0, 0);
    int r;

    memset(s, 0, sizeof(*s));
    if (guarded) {
        InterlockedExchange(&g_copying, 1);
        if (InterlockedCompareExchange(&g_guest_busy, 0, 0)) {
            InterlockedExchange(&g_copying, 0);
            return -2;
        }
    }
    r = snap_read(g_save, s);
    if (guarded && g_dev_copy_ms)       /* test only: a copy that takes this long */
        Sleep(g_dev_copy_ms);
    if (guarded) {
        InterlockedExchange(&g_copying, 0);
        if (InterlockedCompareExchange(&g_guest_gen, 0, 0) != gen) {
            snap_free(s);
            return -2;
        }
    }
    if (r > 0 && !s->have[0]) {
        snap_free(s);
        return 0;
    }
    return r;
}

/* A backup's folder and the files a backup holds; anything else in it stays
 * (and keeps the folder). */
static int remove_backup_dir(const wchar_t *dir)
{
    wchar_t p[MAX_PATH];
    int i;
    for (i = 0; i < SV_NFILES; i++) {
        swprintf(p, MAX_PATH, L"%s\\%s", dir, k_files[i]);
        SetFileAttributesW(p, FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(p);
    }
    swprintf(p, MAX_PATH, L"%s\\%s", dir, SV_INFO);
    DeleteFileW(p);
    return RemoveDirectoryW(dir) != 0;
}

/* ── the list ──────────────────────────────────────────────── */

static int cmp_desc(const void *a, const void *b)
{
    return -strcmp(((const saves_entry_t *)a)->name, ((const saves_entry_t *)b)->name);
}

static void read_why(const wchar_t *dir, char *why, size_t cap)
{
    wchar_t p[MAX_PATH];
    unsigned char *d;
    DWORD n;
    char *s, *e;

    snprintf(why, cap, "-");
    swprintf(p, MAX_PATH, L"%s\\%s", dir, SV_INFO);
    if (read_file(p, &d, &n) <= 0)
        return;
    d = (unsigned char *)realloc(d, n + 1);
    if (!d)
        return;
    d[n] = 0;
    s = strstr((char *)d, "why: ");
    if (s) {
        s += 5;
        e = s + strcspn(s, "\r\n");
        snprintf(why, cap, "%.*s", (int)(e - s), s);
    }
    free(d);
}

/* The backups on disk into g_list (worker or start). */
static void refresh_list(void)
{
    static saves_entry_t tmp[SV_MAX_LIST];
    WIN32_FIND_DATAW fd;
    wchar_t pat[MAX_PATH], dir[MAX_PATH];
    HANDLE h;
    int n = 0;

    swprintf(pat, MAX_PATH, L"%s\\*", g_bak);
    h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchLimitToDirectories, NULL, 0);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            SYSTEMTIME t;
            WIN32_FILE_ATTRIBUTE_DATA fa;
            saves_entry_t *e = &tmp[n];
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !backup_name(fd.cFileName, &t))
                continue;
            u8(fd.cFileName, e->name, sizeof(e->name));
            snprintf(e->when, sizeof(e->when), "%04u-%02u-%02u %02u:%02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour,
                     t.wMinute, t.wSecond);
            swprintf(dir, MAX_PATH, L"%s\\%s\\%s", g_bak, fd.cFileName, k_files[0]);
            e->size = GetFileAttributesExW(dir, GetFileExInfoStandard, &fa) ? fa.nFileSizeLow : 0;
            swprintf(dir, MAX_PATH, L"%s\\%s", g_bak, fd.cFileName);
            read_why(dir, e->why, sizeof(e->why));
            n++;
        } while (n < SV_MAX_LIST && FindNextFileW(h, &fd));
        FindClose(h);
    }
    qsort(tmp, n, sizeof(tmp[0]), cmp_desc);
    EnterCriticalSection(&g_cs);
    memcpy(g_list, tmp, n * sizeof(tmp[0]));
    g_n = n;
    LeaveCriticalSection(&g_cs);
}

/* Keep the newest SV_KEEP_LAST and the newest of each of the last
 * SV_KEEP_DAYS days (and a restore's target); delete the rest. */
static void prune(void)
{
    SYSTEMTIME now;
    long today;
    char last_day[11] = "";
    int i, n, gone = 0;
    static saves_entry_t list[SV_MAX_LIST];
    char pending[32];

    GetLocalTime(&now);
    today = day_number(now.wYear, now.wMonth, now.wDay);
    EnterCriticalSection(&g_cs);
    n = g_n;
    memcpy(list, g_list, n * sizeof(list[0]));
    memcpy(pending, g_pending, sizeof(pending));
    LeaveCriticalSection(&g_cs);
    for (i = 0; i < n; i++) {
        const char *nm = list[i].name;
        int keep = i < SV_KEEP_LAST || strcmp(nm, pending) == 0;
        if (strncmp(nm, last_day, 10) != 0) {   /* the newest of its day */
            long day = day_number((WORD)atoi(nm), (WORD)atoi(nm + 5), (WORD)atoi(nm + 8));
            snprintf(last_day, sizeof(last_day), "%.10s", nm);
            if (today - day < SV_KEEP_DAYS)
                keep = 1;
        }
        if (!keep) {
            wchar_t dir[MAX_PATH], wn[32];
            MultiByteToWideChar(CP_UTF8, 0, nm, -1, wn, 32);
            swprintf(dir, MAX_PATH, L"%s\\%s", g_bak, wn);
            if (remove_backup_dir(dir)) {
                fprintf(stderr, "[SAVE] deleted the backup %s (past the newest %d and the newest of each of %d days)\n",
                        nm, SV_KEEP_LAST, SV_KEEP_DAYS);
                gone++;
            } else {
                char eb[128];
                fprintf(stderr, "[SAVE] could not delete the old backup %s: %s\n", nm,
                        err_text(GetLastError(), eb, sizeof(eb)));
            }
        }
    }
    if (gone)
        refresh_list();
}

/* ── taking a backup ───────────────────────────────────────── */

static int ensure_backup_dir(void)
{
    return CreateDirectoryW(g_bak, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

/* The snapshot into a new backup folder, named after the local time.
 * Returns 1, or 0 with GetLastError. */
static int write_backup(const snap_t *s, const char *why, char made[32])
{
    SYSTEMTIME t;
    wchar_t base[32], part[MAX_PATH], dir[MAX_PATH], p[MAX_PATH], leaf[40];
    char info[256];
    DWORD e;
    int i, k;

    if (!ensure_backup_dir())
        return 0;
    GetLocalTime(&t);
    swprintf(base, 32, L"%04u-%02u-%02u_%02u-%02u-%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    swprintf(part, MAX_PATH, L"%s\\%s", g_bak, SV_PARTIAL);
    remove_backup_dir(part);            /* one a crash left half written */
    if (!CreateDirectoryW(part, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return 0;
    for (i = 0; i < SV_NFILES; i++) {
        if (!s->have[i])
            continue;
        swprintf(p, MAX_PATH, L"%s\\%s", part, k_files[i]);
        if (!write_file(p, s->d[i], s->n[i], 0))
            goto fail;
    }
    snprintf(info, sizeof(info), "Crash TWOC save backup (twoc/src/saves.c)\r\nwhy: %s\r\n", why);
    swprintf(p, MAX_PATH, L"%s\\%s", part, SV_INFO);
    if (!write_file(p, info, (DWORD)strlen(info), 0))
        goto fail;
    for (k = 1; k < 100; k++) {         /* two in one second: _2, _3 ... */
        if (k == 1)
            swprintf(leaf, 40, L"%s", base);
        else
            swprintf(leaf, 40, L"%s_%d", base, k);
        swprintf(dir, MAX_PATH, L"%s\\%s", g_bak, leaf);
        if (MoveFileW(part, dir)) {
            u8(leaf, made, 32);
            return 1;
        }
        e = GetLastError();
        if (e != ERROR_ALREADY_EXISTS && e != ERROR_FILE_EXISTS)
            break;
    }
fail:
    e = GetLastError();
    remove_backup_dir(part);
    SetLastError(e);
    return 0;
}

enum { BK_MADE = 1, BK_SAME = 0, BK_NONE = 2, BK_FAIL = -1, BK_BUSY = -2 };

/* A backup of the game's save unless the newest equals it. `ref` gets the
 * backup made, or the newest that equals it. */
static int backup(const char *why, int guarded, char ref[32], char *err, size_t errcap)
{
    snap_t s, nb;
    char newest[32];
    int r;

    ref[0] = 0;
    err[0] = 0;
    r = snap_game(&s, guarded);
    if (r == -2)
        return BK_BUSY;
    if (r == 0)
        return BK_NONE;
    if (r < 0) {
        err_text(GetLastError(), err, errcap);
        return BK_FAIL;
    }
    EnterCriticalSection(&g_cs);
    memcpy(newest, g_n ? g_list[0].name : "", sizeof(newest));
    LeaveCriticalSection(&g_cs);
    if (newest[0]) {
        wchar_t dir[MAX_PATH], wn[32];
        MultiByteToWideChar(CP_UTF8, 0, newest, -1, wn, 32);
        swprintf(dir, MAX_PATH, L"%s\\%s", g_bak, wn);
        if (snap_read(dir, &nb) > 0) {
            int same = snap_equal(&s, &nb);
            snap_free(&nb);
            if (same) {
                snap_free(&s);
                memcpy(ref, newest, 32);
                return BK_SAME;
            }
        }
    }
    r = write_backup(&s, why, ref);
    if (!r)
        err_text(GetLastError(), err, errcap);
    else
        fprintf(stderr, "[SAVE] backup %s: %s (crash.sav %lu bytes%s)\n", why, ref, (unsigned long)s.n[0],
                s.have[1] ? ", SaveMeta.xbx" : "");
    snap_free(&s);
    if (r) {
        refresh_list();
        prune();
    }
    return r ? BK_MADE : BK_FAIL;
}

/* "14:03:22" of a backup's name. */
static const char *clock_of(const char *name)
{
    static char buf[4][12];
    static int k;
    char *b = buf[k++ & 3];
    if (strlen(name) >= 19)
        snprintf(b, 12, "%.2s:%.2s:%.2s", name + 11, name + 14, name + 17);
    else
        snprintf(b, 12, "%s", name);
    return b;
}

/* "2026-10-17 14:03:22" of a backup's name. */
static const char *when_of(const char *name)
{
    static char buf[4][32];
    static int k;
    char *b = buf[k++ & 3];
    if (strlen(name) >= 19)
        snprintf(b, 32, "%.10s %s%s", name, clock_of(name), name + 19);
    else
        snprintf(b, 32, "%s", name);
    return b;
}

/* ── the restore, at the start ─────────────────────────────── */

static int read_pending(char name[32])
{
    wchar_t p[MAX_PATH], wn[32];
    unsigned char *d;
    DWORD n, i;

    name[0] = 0;
    swprintf(p, MAX_PATH, L"%s\\%s", g_bak, SV_PENDING);
    if (read_file(p, &d, &n) <= 0)
        return 0;
    for (i = 0; i < n && i < 31 && d[i] != '\r' && d[i] != '\n'; i++)
        name[i] = (char)d[i];
    name[i] = 0;
    free(d);
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wn, 32);
    if (!backup_name(wn, NULL)) {
        name[0] = 0;
        return -1;
    }
    return 1;
}

static int write_pending(const char *name)
{
    wchar_t p[MAX_PATH];
    char text[512];
    snprintf(text, sizeof(text),
             "%s\r\n\r\nAt its next start Crash TWOC replaces its save (game_files\\UDATA\\56550003\\4D5C926D6357) "
             "with the backup named on the first line, after backing up the save it replaces. Delete this file "
             "to cancel.\r\n", name);
    swprintf(p, MAX_PATH, L"%s\\%s", g_bak, SV_PENDING);
    return ensure_backup_dir() && write_file(p, text, (DWORD)strlen(text), 1);
}

static void delete_pending(void)
{
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%s\\%s", g_bak, SV_PENDING);
    DeleteFileW(p);
}

/* The save folder, made if missing (the game makes it at its first save). */
static int make_save_dir(void)
{
    wchar_t p[MAX_PATH];
    size_t i, n = wcslen(g_save);
    for (i = 3; i <= n; i++)
        if (g_save[i] == L'\\' || g_save[i] == 0) {
            wcsncpy(p, g_save, i);
            p[i] = 0;
            if (!CreateDirectoryW(p, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
                return 0;
        }
    return 1;
}

/* The backup `name` over the game's save. 1, or 0 with `err`. */
static int restore(const char *name, char *err, size_t errcap)
{
    wchar_t dir[MAX_PATH], wn[32], tmp[MAX_PATH], dst[MAX_PATH];
    snap_t s;
    int i;

    MultiByteToWideChar(CP_UTF8, 0, name, -1, wn, 32);
    swprintf(dir, MAX_PATH, L"%s\\%s", g_bak, wn);
    if (snap_read(dir, &s) < 0) {
        err_text(GetLastError(), err, errcap);
        return 0;
    }
    if (!s.have[0]) {
        snprintf(err, errcap, "the backup %s has no crash.sav (deleted?)", name);
        snap_free(&s);
        return 0;
    }
    if (!make_save_dir()) {
        err_text(GetLastError(), err, errcap);
        snap_free(&s);
        return 0;
    }
    for (i = 0; i < SV_NFILES; i++) {
        if (!s.have[i])
            continue;                   /* the backup hasn't it: the save's own stays */
        swprintf(tmp, MAX_PATH, L"%s\\%s.restoring", g_save, k_files[i]);
        swprintf(dst, MAX_PATH, L"%s\\%s", g_save, k_files[i]);
        if (!write_file(tmp, s.d[i], s.n[i], 1) ||
            !MoveFileExW(tmp, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DWORD e = GetLastError();
            DeleteFileW(tmp);
            err_text(e, err, errcap);
            snap_free(&s);
            return 0;
        }
    }
    snap_free(&s);
    return 1;
}

/* ── the worker ────────────────────────────────────────────── */

/* The next backup at `at`: the settle after a save counts from the game's
 * last close; one asked for by hand stays first. */
static void schedule(ULONGLONG at, const char *why)
{
    EnterCriticalSection(&g_cs);
    if (!g_due || strcmp(g_due_why, "by hand") != 0 || strcmp(why, "by hand") == 0) {
        g_due = at;
        g_due_why = why;
    }
    LeaveCriticalSection(&g_cs);
    SetEvent(g_wake);
}

static void do_backup(const char *why)
{
    char ref[32], err[160];
    int hand = strcmp(why, "by hand") == 0;
    int r = backup(why, 1, ref, err, sizeof(err));

    switch (r) {
    case BK_BUSY:
        fprintf(stderr, "[SAVE] backup %s: the game has a save file open, or opened one during the copy; again in %d s\n",
                why, SV_RETRY_MS / 1000);
        schedule(GetTickCount64() + SV_RETRY_MS, why);
        if (hand)
            say("The game has its save open; backing up in a moment.");
        return;
    case BK_MADE:
        say("Backed up %s at %s.", hand ? "by hand" : why, clock_of(ref));
        break;
    case BK_SAME:
        if (hand)
            say("Nothing changed since the backup of %s; no new copy.", clock_of(ref));
        else
            fprintf(stderr, "[SAVE] %s: the save equals the backup %s; no new copy\n", why, ref);
        break;
    case BK_NONE:
        if (hand)
            say("There is no save to back up yet.");
        break;
    default:
        fprintf(stderr, "[SAVE] backup %s FAILED: %s\n", why, err);
        say("The backup failed: %s.", err);
        break;
    }
    EnterCriticalSection(&g_cs);
    g_busy_ui = 0;
    LeaveCriticalSection(&g_cs);
}

static void do_restore_request(const char *name)
{
    char ref[32], err[160];
    int r = backup("before a restore", 1, ref, err, sizeof(err));

    if (r == BK_BUSY) {                 /* the game has its save open: again shortly */
        EnterCriticalSection(&g_cs);
        if (!g_rq_name[0])
            memcpy(g_rq_name, name, sizeof(g_rq_name));
        g_rq |= RQ_RESTORE;
        LeaveCriticalSection(&g_cs);
        Sleep(SV_RETRY_MS);
        SetEvent(g_wake);
        return;
    }
    if (r == BK_FAIL) {
        fprintf(stderr, "[SAVE] restore not scheduled: the save in use could not be backed up first: %s\n", err);
        say("Not scheduled: the save in use could not be backed up first (%s).", err);
    } else if (!write_pending(name)) {
        fprintf(stderr, "[SAVE] restore not scheduled: could not write %ls\\%ls: %s\n", g_bak, SV_PENDING,
                err_text(GetLastError(), err, sizeof(err)));
        say("Not scheduled: %s.", err);
    } else {
        EnterCriticalSection(&g_cs);
        memcpy(g_pending, name, sizeof(g_pending));
        if (r == BK_NONE)
            status("Scheduled: the backup of %s replaces the save at the next start. Quit the game and start it "
                   "again to play it. (There is no save now.)", when_of(name));
        else
            status("Scheduled: the backup of %s replaces the save at the next start. Quit the game and start it "
                   "again to play it. The save in use now is kept as the backup of %s.", when_of(name), clock_of(ref));
        LeaveCriticalSection(&g_cs);
        fprintf(stderr, "[SAVE] restore scheduled for the next start: %s (the save in use: %s)\n", name,
                r == BK_NONE ? "none" : ref);
    }
    EnterCriticalSection(&g_cs);
    g_busy_ui = 0;
    LeaveCriticalSection(&g_cs);
}

static DWORD WINAPI worker(void *arg)
{
    (void)arg;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);    /* ShellExecute */
    for (;;) {
        ULONGLONG now = GetTickCount64(), due;
        LONG rq;
        char name[32];
        const char *why;

        EnterCriticalSection(&g_cs);
        due = g_closing ? 0 : g_due;
        LeaveCriticalSection(&g_cs);
        WaitForSingleObject(g_wake, !due ? INFINITE : due > now ? (DWORD)(due - now) : 0);

        EnterCriticalSection(&g_cs);
        if (g_closing) {                /* step 97: quitting (saves_quiesce): nothing new */
            LeaveCriticalSection(&g_cs);
            continue;
        }
        InterlockedExchange(&g_working, 1);
        rq = g_rq;
        g_rq = 0;
        memcpy(name, g_rq_name, sizeof(name));
        g_rq_name[0] = 0;
        LeaveCriticalSection(&g_cs);
        if (rq & RQ_CANCEL) {
            delete_pending();
            EnterCriticalSection(&g_cs);
            if (g_pending[0])
                status("The restore of %s is cancelled; the save stays as it is.", when_of(g_pending));
            g_pending[0] = 0;
            g_busy_ui = 0;
            LeaveCriticalSection(&g_cs);
            fprintf(stderr, "[SAVE] the restore at the next start is cancelled\n");
        }
        if ((rq & RQ_RESTORE) && name[0])
            do_restore_request(name);
        if (rq & RQ_OPEN) {
            HINSTANCE h;
            ensure_backup_dir();
            h = ShellExecuteW(NULL, L"open", g_bak, NULL, NULL, SW_SHOWNORMAL);
            if ((INT_PTR)h <= 32)
                say("Could not open the folder (error %d).", (int)(INT_PTR)h);
            else
                say("Opened the backups folder in File Explorer (Alt+Tab to it).");
        }
        if (rq & RQ_REFRESH)
            refresh_list();

        EnterCriticalSection(&g_cs);
        now = GetTickCount64();
        why = NULL;
        if (g_due && now >= g_due) {
            why = g_due_why;
            g_due = 0;
        }
        LeaveCriticalSection(&g_cs);
        if (why)
            do_backup(why);
        InterlockedExchange(&g_working, 0);
    }
    return 0;
}

int saves_quiesce(unsigned ms, char *what, size_t cap)
{
    ULONGLONG t0 = GetTickCount64(), due = 0, now;
    int busy = 0, guest = 0;

    if (!g_init || !g_on) {
        snprintf(what, cap, "save backups off this run");
        return 1;
    }
    EnterCriticalSection(&g_cs);
    InterlockedExchange(&g_closing, 1);
    due = g_due;
    LeaveCriticalSection(&g_cs);
    SetEvent(g_wake);
    while ((g_working || g_copying || g_guest_busy) && GetTickCount64() - t0 < ms) {
        busy |= g_working || g_copying;
        guest |= g_guest_busy != 0;
        Sleep(10);
    }
    now = GetTickCount64();
    if (g_working || g_copying || g_guest_busy) {
        snprintf(what, cap, "%s still under way after %u ms (left)", g_guest_busy ? "the game's own save" : "a backup",
                 ms);
        return 0;
    }
    snprintf(what, cap, "%s%s%s", busy ? "a backup in progress finished first" : guest ? "" : "no backup in progress",
             guest ? (busy ? "; the game's save written first" : "the game's save written first") : "",
             due ? "; the backup due after the last save is left for the next start's" : "");
    if (busy || guest)
        snprintf(what + strlen(what), cap - strlen(what), " (%llu ms)", now - t0);
    return 1;
}

/* ── the start ─────────────────────────────────────────────── */

void saves_startup(void)
{
    const char *e = getenv("RECOMP_SAVE_BACKUPS");
    wchar_t gd[MAX_PATH];
    char ref[32], err[160], pend[32];
    LARGE_INTEGER f, t0, t1;
    int r = BK_NONE, pr;
    HANDLE th;

    if (!InterlockedExchange(&g_init, 1)) {
        InitializeCriticalSection(&g_cs);
        InitializeCriticalSection(&g_hcs);
    }
    if (e && e[0] == '0') {
        fprintf(stderr, "[SAVE] save backups off (RECOMP_SAVE_BACKUPS=0)\n");
        return;
    }
    if (!GetFullPathNameW(SV_GAME_DIR, MAX_PATH, gd, NULL))
        return;
    e = getenv("RECOMP_DEV_SAVE_COPY_DELAY_MS");
    if (e && atoi(e) > 0) {
        g_dev_copy_ms = (DWORD)atoi(e);
        fprintf(stderr, "[SAVE] test: each copy after the start takes %lu ms more (RECOMP_DEV_SAVE_COPY_DELAY_MS)\n",
                (unsigned long)g_dev_copy_ms);
    }
    swprintf(g_save, MAX_PATH, L"%s\\%s", gd, SV_SAVE_SUB);
    swprintf(g_bak, MAX_PATH, L"%s\\%s", gd, SV_BACKUP_SUB);
    u8(g_bak, g_dir_u8, sizeof(g_dir_u8));
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);

    refresh_list();
    pr = read_pending(pend);
    if (pr < 0) {
        fprintf(stderr, "[SAVE] %ls\\%ls names no backup; ignored and deleted\n", g_bak, SV_PENDING);
        delete_pending();
    }
    r = backup(pr > 0 ? "at start, before a restore" : "at start", 0, ref, err, sizeof(err));
    if (r == BK_NONE)
        fprintf(stderr, "[SAVE] at start: no save to back up (%ls\\crash.sav isn't there)\n", g_save);
    else if (r == BK_SAME)
        fprintf(stderr, "[SAVE] at start: the save equals the backup %s; no new copy\n", ref);
    else if (r == BK_FAIL)
        fprintf(stderr, "[SAVE] backup at start FAILED: %s\n", err);
    if (pr > 0) {
        memcpy(g_pending, pend, sizeof(g_pending));
        if (r == BK_FAIL) {
            /* the save it would replace isn't safe yet: leave both as they are */
            fprintf(stderr, "[SAVE] the restore of %s is NOT done: the save in use could not be backed up first; "
                    "it stays scheduled\n", pend);
            say("The restore of %s wasn't done: the save in use could not be backed up first (%s). It is "
                   "still scheduled.", when_of(pend), err);
        } else if (restore(pend, err, sizeof(err))) {
            delete_pending();
            g_pending[0] = 0;
            fprintf(stderr, "[SAVE] restored the backup %s into %ls (the save it replaced: %s)\n", pend, g_save,
                    r == BK_NONE ? "none" : ref);
            say("Restored the backup of %s at this start.", when_of(pend));
        } else {
            char msg[1024];
            delete_pending();
            g_pending[0] = 0;
            fprintf(stderr, "[SAVE] the restore of %s FAILED: %s; the save is as it was, the restore cancelled\n",
                    pend, err);
            say("The restore of %s failed (%s); the save is as it was.", when_of(pend), err);
            snprintf(msg, sizeof(msg), "The save backup of %s could not be restored:\n\n%s\n\nThe save is as it "
                     "was, and the restore is cancelled. The backups are in\n%s", when_of(pend), err, g_dir_u8);
            MessageBoxA(NULL, msg, "Crash TWOC", MB_ICONWARNING | MB_OK);
        }
    }
    prune();
    QueryPerformanceCounter(&t1);
    EnterCriticalSection(&g_cs);
    fprintf(stderr, "[SAVE] save backups in %s: %d, newest %s (%.1f ms)\n", g_dir_u8, g_n,
            g_n ? g_list[0].name : "none", (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart);
    LeaveCriticalSection(&g_cs);

    g_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    th = g_wake ? CreateThread(NULL, 0, worker, NULL, 0, NULL) : NULL;
    if (!th) {
        fprintf(stderr, "[SAVE] no worker thread: backups only at each start\n");
        return;
    }
    SetThreadPriority(th, THREAD_PRIORITY_BELOW_NORMAL);
    {
        typedef HRESULT(WINAPI * desc_t)(HANDLE, PCWSTR);
        desc_t sd = (desc_t)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription");
        if (sd)
            sd(th, L"twoc save backups");
    }
    CloseHandle(th);
    g_on = 1;
}

/* ── the guest's file calls ────────────────────────────────── */

/* `key` (lowercase, backslashes) at p[i], any case and either slash. */
static int match_at(const char *p, unsigned len, unsigned i, const char *key, unsigned kl)
{
    unsigned j;
    if (i + kl > len)
        return 0;
    for (j = 0; j < kl; j++) {
        char c = p[i + j];
        if (c == '/')
            c = '\\';
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c != key[j])
            return 0;
    }
    return 1;
}

/* The part after the title's save root when `p` is inside one of the
 * game's save folders (not the title's own folder, its files beside them).
 * XAPI opens them through its U: link (\??\U:\<save>\crash.sav), which the
 * kernel resolves to \Device\Harddisk0\partition1\UDATA\56550003. */
static const char *save_part(const char *p, unsigned len)
{
    static const char key[] = "\\udata\\56550003\\";
    const unsigned kl = sizeof(key) - 1;
    unsigned i, at = 0, rl, k;
    const char *rest;

    if (match_at(p, len, 0, "\\??\\u:\\", 7))
        at = 7;
    else if (match_at(p, len, 0, "u:\\", 3))
        at = 3;
    for (i = 0; !at && i + kl <= len; i++)
        if (match_at(p, len, i, key, kl))
            at = i + kl;
    if (!at)
        return NULL;
    rest = p + at;
    rl = len - at;
    for (k = 0; k < rl && rest[k] != '\\' && rest[k] != '/'; k++)
        ;
    if (k == 0)                         /* the save root itself */
        return NULL;
    if (k == rl && rl > 4 && _strnicmp(rest + rl - 4, ".xbx", 4) == 0)
        return NULL;                    /* TitleMeta.xbx and the like: the title's, not a save's */
    return rest;
}

int saves_guest_open_begin(const char *path, unsigned len, unsigned access, unsigned share, unsigned disposition,
                           unsigned options)
{
    const char *part;
    int write;

    if (!g_on || !path || !(part = save_part(path, len)))
        return 0;
    write = (access & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE)) != 0 ||
            disposition == 0 /* FILE_SUPERSEDE */ || disposition == 2 /* FILE_CREATE */ ||
            disposition == 4 /* FILE_OVERWRITE */ || disposition == 5 /* FILE_OVERWRITE_IF */;
    InterlockedIncrement(&g_guest_gen);
    InterlockedIncrement(&g_guest_busy);
    if (!(share & FILE_SHARE_READ) && InterlockedCompareExchange(&g_copying, 0, 0)) {
        /* a copy's handle would fail this open: let the copy finish (its read of two small files) */
        ULONGLONG t0 = GetTickCount64();
        while (InterlockedCompareExchange(&g_copying, 0, 0))
            SwitchToThread();
        fprintf(stderr, "[SAVE] the game's open below waited %llu ms for a copy to finish\n", GetTickCount64() - t0);
    }
    fprintf(stderr, "[SAVE] the game opens %.*s (%s, share %u%s)\n", (int)(len - (unsigned)(part - path)), part,
            write ? "to write" : "to read", share, options & 1 ? ", a folder" : "");
    return write ? 2 : 1;
}

void saves_guest_open_end(int token, int ok, uint32_t handle)
{
    if (!token)
        return;
    if (ok && handle) {
        EnterCriticalSection(&g_hcs);
        if (g_hn < SV_MAX_H) {
            g_h[g_hn].h = handle;
            g_h[g_hn].write = token == 2;
            g_hn++;
            InterlockedIncrement(&g_tracked);
            LeaveCriticalSection(&g_hcs);
            return;                     /* busy until its close */
        }
        LeaveCriticalSection(&g_hcs);
    }
    InterlockedDecrement(&g_guest_busy);
}

int saves_guest_tracking(void)
{
    return g_tracked > 0;
}

int saves_guest_close_begin(uint32_t handle)
{
    int i, r = 0;
    EnterCriticalSection(&g_hcs);
    for (i = 0; i < g_hn; i++)
        if (g_h[i].h == handle) {
            r = g_h[i].write ? 2 : 1;
            g_h[i] = g_h[--g_hn];
            InterlockedDecrement(&g_tracked);
            break;
        }
    LeaveCriticalSection(&g_hcs);
    return r;
}

void saves_guest_close_end(int token)
{
    if (!token)
        return;
    InterlockedDecrement(&g_guest_busy);
    if (token == 2) {
        fprintf(stderr, "[SAVE] the game closed a save file it wrote: a backup in %d s\n", SV_SETTLE_MS / 1000);
        schedule(GetTickCount64() + SV_SETTLE_MS, "after a save");
    }
}

/* ── the overlay ───────────────────────────────────────────── */

int saves_list(saves_entry_t *out, int max)
{
    int n;
    if (!g_init)
        return 0;
    EnterCriticalSection(&g_cs);
    n = g_n < max ? g_n : max;
    memcpy(out, g_list, n * sizeof(out[0]));
    LeaveCriticalSection(&g_cs);
    return n;
}

void saves_state(saves_state_t *st)
{
    memset(st, 0, sizeof(*st));
    if (!g_init)
        return;
    EnterCriticalSection(&g_cs);
    st->on = g_on;
    snprintf(st->dir, sizeof(st->dir), "%s", g_dir_u8);
    snprintf(st->status, sizeof(st->status), "%s", g_status);
    snprintf(st->pending, sizeof(st->pending), "%s", g_pending);
    st->busy = g_busy_ui;
    LeaveCriticalSection(&g_cs);
}

static void request(LONG rq, const char *name)
{
    if (!g_on)
        return;
    EnterCriticalSection(&g_cs);
    g_rq |= rq;
    if (name)
        snprintf(g_rq_name, sizeof(g_rq_name), "%s", name);
    if (rq & (RQ_RESTORE | RQ_CANCEL))
        g_busy_ui = 1;
    LeaveCriticalSection(&g_cs);
    SetEvent(g_wake);
}

void saves_refresh(void)
{
    request(RQ_REFRESH, NULL);
}

void saves_backup_now(void)
{
    if (!g_on)
        return;
    EnterCriticalSection(&g_cs);
    g_busy_ui = 1;
    LeaveCriticalSection(&g_cs);
    schedule(GetTickCount64(), "by hand");
}

void saves_restore_at_next_start(const char *name)
{
    request(RQ_RESTORE, name);
}

void saves_cancel_restore(void)
{
    request(RQ_CANCEL, NULL);
}

void saves_open_folder(void)
{
    request(RQ_OPEN, NULL);
}
