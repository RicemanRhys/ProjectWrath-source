/**
 * Saves outside the package (step 110, notes 18.113). See userdata.h.
 *
 * Test aids (inert unless set):
 *   RECOMP_DEV_SAVES_FAIL=write       the data folder's write test fails as a full disk would
 *   RECOMP_DEV_SAVES_FAIL=copy@N      the migration's Nth file fails to copy as on a full disk
 *   RECOMP_DEV_SAVES_FAIL=hash@N      the migration's Nth copy reads back different from its source
 *   RECOMP_DEV_SAVES_FAIL=move@N      the commit's Nth move into the saves folder is refused
 *   RECOMP_DEV_SAVES_HOLD=PHASE@N:MS  waits MS ms and logs it, so a test can kill the process there: copy@N (after
 *       N files copied), commit@N (after N items moved into place; commit@0 before the first)
 */
#include <windows.h>
#include <bcrypt.h>
#include <ctype.h>
#include <shlobj.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "userdata.h"
#include "setup.h"
#include "names.h"

/* The toolkit's link table (xboxrecomp kernel_io.c): what the title linked \??\X: to. */
extern const char *xbox_LookupSymbolicLink(const char *link);

#define UD_MAX_DIR 190u                 /* characters: the deepest path below it, \saves.migrating\UDATA_backups\
                                         * <date>_NN\SaveMeta.xbx (+66; the kernel's \\?\ form +51, saves.c's
                                         * .restoring +57, the cache +40), stays under 260 */
#define N_SAVES   L"saves"
#define N_STAGE   L"saves.migrating"
#define N_JOURNAL L"migration in progress.txt"
#define N_ABOUT   L"About this folder.txt"
#define N_SEEN    L"Old save copies.txt"

static int g_outside;
static wchar_t g_user[SETUP_PATH_MAX], g_saves[SETUP_PATH_MAX];
static char g_user8[SETUP_PATH_MAX * 3], g_saves8[SETUP_PATH_MAX * 3], g_xbox8[SETUP_PATH_MAX * 3];
static char g_kdir[SETUP_PATH_MAX * 3]; /* \\?\<saves>, as the toolkit's MultiByteToWideChar(CP_ACP) reads it back */
static char g_msg[1600], g_note[1600];

/* ── Small things ──────────────────────────────────────────────────────── */

static void u8(const wchar_t *w, char *out, size_t cap)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL))
        out[0] = 0;
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

static void join(const wchar_t *a, const wchar_t *b, wchar_t *out)
{
    _snwprintf_s(out, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", a, b);
}

static const char *err_words(DWORD e)
{
    switch (e) {
    case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: return "the disk is full";
    case ERROR_ACCESS_DENIED: return "access is denied (the folder may be read-only or protected)";
    case ERROR_WRITE_PROTECT: return "the disk is write-protected";
    case ERROR_SHARING_VIOLATION: case ERROR_LOCK_VIOLATION: return "another program has a file open";
    case ERROR_PATH_NOT_FOUND: return "a folder on the way isn't there";
    case ERROR_FILENAME_EXCED_RANGE: return "the path is too long";
    case ERROR_CRC: return "a copy read back different from its source";
    case ERROR_NOT_READY: case ERROR_DEV_NOT_EXIST: return "the disk was removed or isn't ready";
    default: return "Windows couldn't do it";
    }
}

/* RECOMP_DEV_SAVES_FAIL=what@n (or "write"): 1 when this is the point to fail. */
static int dev_fail(const char *what, unsigned n)
{
    const char *v = getenv("RECOMP_DEV_SAVES_FAIL");
    char w[16];
    unsigned at = 0;
    if (!v || !*v)
        return 0;
    if (sscanf(v, "%15[a-z]@%u", w, &at) < 1)
        return 0;
    return !strcmp(w, what) && (at == n || !strcmp(what, "write"));
}

static void dev_hold(const char *phase, unsigned n)
{
    const char *v = getenv("RECOMP_DEV_SAVES_HOLD");
    char p[16];
    unsigned at, ms;
    if (!v || sscanf(v, "%15[a-z]@%u:%u", p, &at, &ms) != 3 || strcmp(p, phase) || at != n)
        return;
    fprintf(stderr, "[SAVES] test hold (RECOMP_DEV_SAVES_HOLD): %s@%u, %u ms\n", phase, n, ms);
    fflush(stderr);
    Sleep(ms);
}

/* `dir` can be written to: a file made, written, flushed and deleted. 0 or a Windows error. */
static DWORD write_test(const wchar_t *dir)
{
    wchar_t p[SETUP_PATH_MAX];
    HANDLE h;
    DWORD put = 0, e = 0;
    static const char t[] = "Project Wrath: a test that the saves folder can be written to.\r\n";

    if (dev_fail("write", 0))
        return ERROR_DISK_FULL;
    _snwprintf_s(p, SETUP_PATH_MAX, _TRUNCATE, L"%s\\write-test.%lu.tmp", dir, (unsigned long)GetCurrentProcessId());
    h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                    NULL);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError();
    if (!WriteFile(h, t, (DWORD)sizeof(t) - 1, &put, NULL) || put != sizeof(t) - 1 || !FlushFileBuffers(h))
        e = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
    CloseHandle(h);
    return e;
}

/* ── Hashes ────────────────────────────────────────────────────────────── */

/* SHA-256 of a file read from the disk. 1, or 0 with GetLastError. */
static int file_sha256(const wchar_t *p, unsigned char out[32], unsigned long long *size)
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hh = NULL;
    unsigned char *buf = NULL;
    HANDLE f;
    DWORD got, e = 0;
    int ok = 0;

    *size = 0;
    f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 &&
        BCryptCreateHash(alg, &hh, NULL, 0, NULL, 0, 0) == 0 && (buf = (unsigned char *)malloc(1 << 16)) != NULL) {
        ok = 1;
        for (;;) {
            if (!ReadFile(f, buf, 1 << 16, &got, NULL)) {
                e = GetLastError();
                ok = 0;
                break;
            }
            if (!got)
                break;
            BCryptHashData(hh, buf, got, 0);
            *size += got;
        }
        if (ok)
            ok = BCryptFinishHash(hh, out, 32, 0) == 0;
    }
    free(buf);
    if (hh)
        BCryptDestroyHash(hh);
    if (alg)
        BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    if (!ok)
        SetLastError(e ? e : ERROR_READ_FAULT);
    return ok;
}

/* ── What a saves folder holds ─────────────────────────────────────────── */

/* Files below `dir` (recursively, links not followed), at least `depth` folders down. Stops at `stop`. */
static unsigned count_files(const wchar_t *dir, int depth, unsigned stop)
{
    wchar_t pat[SETUP_PATH_MAX + 4], p[SETUP_PATH_MAX];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    unsigned n = 0;

    _snwprintf_s(pat, sizeof(pat) / sizeof(pat[0]), _TRUNCATE, L"%s\\*", dir);
    h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..") ||
            (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            join(dir, fd.cFileName, p);
            n += count_files(p, depth > 0 ? depth - 1 : 0, stop ? stop - n : 0);
        } else if (depth <= 0)
            n++;
    } while ((!stop || n < stop) && FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

/* A save: a file in a save's folder (UDATA\56550003\<save>\...), not the title's own .xbx beside them. */
static int has_save(const wchar_t *saves)
{
    wchar_t p[SETUP_PATH_MAX];
    _snwprintf_s(p, SETUP_PATH_MAX, _TRUNCATE, L"%s\\UDATA\\56550003", saves);
    return count_files(p, 1, 1) > 0;
}

/* A backup: a file in a folder of UDATA_backups (saves.c's). */
static int has_backup(const wchar_t *saves)
{
    wchar_t p[SETUP_PATH_MAX];
    _snwprintf_s(p, SETUP_PATH_MAX, _TRUNCATE, L"%s\\UDATA_backups", saves);
    return count_files(p, 1, 1) > 0;
}

/* Anything of the player's to copy: a file in UDATA, UDATA_backups or TDATA. */
static unsigned files_to_copy(const wchar_t *saves)
{
    static const wchar_t *const k[3] = { L"UDATA", L"UDATA_backups", L"TDATA" };
    wchar_t p[SETUP_PATH_MAX];
    unsigned n = 0, i;
    for (i = 0; i < 3; i++) {
        join(saves, k[i], p);
        n += count_files(p, 0, 0);
    }
    return n;
}

/* ── The migration: copied into saves.migrating, checked, then moved into saves ── */

#define MIG_MAX_MOVED 512

typedef struct {
    unsigned files, moved, kept, links;
    unsigned long long bytes;
    char why[700];
    wchar_t *done[MIG_MAX_MOVED];       /* what the commit moved into saves (for a rollback); NULL past the limit */
} mig_t;

static void mig_free(mig_t *m)
{
    unsigned i;
    for (i = 0; i < m->moved && i < MIG_MAX_MOVED; i++)
        free(m->done[i]);
}

static void mig_say(mig_t *m, const wchar_t *p, DWORD e, const char *what)
{
    char a[SETUP_PATH_MAX * 3];
    u8(p, a, sizeof(a));
    snprintf(m->why, sizeof(m->why), "%s %s: %s (Windows error %lu)", what, a, err_words(e), (unsigned long)e);
}

/* One file: copied, flushed, then both read back and their SHA-256 compared. 0, or a Windows error. */
static DWORD copy_one(const wchar_t *a, const wchar_t *b, mig_t *m)
{
    unsigned char ha[32], hb[32];
    unsigned long long na, nb;
    HANDLE h;
    DWORD e;

    m->files++;
    if (dev_fail("copy", m->files)) {
        mig_say(m, b, ERROR_DISK_FULL, "copying to");
        return ERROR_DISK_FULL;
    }
    if (!CopyFileExW(a, b, NULL, NULL, NULL, COPY_FILE_FAIL_IF_EXISTS)) {
        e = GetLastError();
        mig_say(m, b, e, "copying to");
        return e;
    }
    h = CreateFileW(b, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE || !FlushFileBuffers(h)) {
        e = GetLastError();
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
        if (e != ERROR_ACCESS_DENIED) {     /* a read-only file can't be opened to write: CopyFileEx wrote it */
            mig_say(m, b, e, "writing");
            return e;
        }
    } else
        CloseHandle(h);
    if (!file_sha256(a, ha, &na)) {
        e = GetLastError();
        mig_say(m, a, e, "reading");
        return e;
    }
    if (!file_sha256(b, hb, &nb)) {
        e = GetLastError();
        mig_say(m, b, e, "reading back");
        return e;
    }
    if (na != nb || memcmp(ha, hb, 32) || dev_fail("hash", m->files)) {
        mig_say(m, b, ERROR_CRC, "checking");
        return ERROR_CRC;
    }
    m->bytes += na;
    dev_hold("copy", m->files);
    return 0;
}

static DWORD copy_tree(const wchar_t *from, const wchar_t *to, mig_t *m)
{
    wchar_t pat[SETUP_PATH_MAX + 4], a[SETUP_PATH_MAX], b[SETUP_PATH_MAX];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    DWORD e = 0;

    if (!CreateDirectoryW(to, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        e = GetLastError();
        mig_say(m, to, e, "making the folder");
        return e;
    }
    _snwprintf_s(pat, sizeof(pat) / sizeof(pat[0]), _TRUNCATE, L"%s\\*", from);
    h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
            continue;
        join(from, fd.cFileName, a);
        join(to, fd.cFileName, b);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            m->links++;                 /* never followed */
        else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            e = copy_tree(a, b, m);
        else
            e = copy_one(a, b, m);
    } while (!e && FindNextFileW(h, &fd));
    FindClose(h);
    return e;
}

/* `from` into `to`: moved whole when `to` has nothing of that name, folders merged, a file `to` already has kept
 * there (the staging copy stays, and is deleted with the staging folder). 0, or a Windows error. */
static DWORD merge_move(const wchar_t *from, const wchar_t *to, mig_t *m)
{
    wchar_t pat[SETUP_PATH_MAX + 4], a[SETUP_PATH_MAX], b[SETUP_PATH_MAX];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    DWORD e = 0;

    if (!exists(from))
        return 0;
    if (!exists(to)) {
        if (dev_fail("move", m->moved + 1)) {
            mig_say(m, to, ERROR_ACCESS_DENIED, "moving the copy into");
            return ERROR_ACCESS_DENIED;
        }
        if (!MoveFileExW(from, to, MOVEFILE_WRITE_THROUGH)) {
            e = GetLastError();
            mig_say(m, to, e, "moving the copy into");
            return e;
        }
        if (m->moved < MIG_MAX_MOVED)
            m->done[m->moved] = _wcsdup(to);
        m->moved++;
        dev_hold("commit", m->moved);
        return 0;
    }
    if (!is_dir(from) || !is_dir(to)) {
        m->kept++;
        return 0;
    }
    _snwprintf_s(pat, sizeof(pat) / sizeof(pat[0]), _TRUNCATE, L"%s\\*", from);
    h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
            continue;
        join(from, fd.cFileName, a);
        join(to, fd.cFileName, b);
        e = merge_move(a, b, m);
    } while (!e && FindNextFileW(h, &fd));
    FindClose(h);
    return e;
}

/* Deletes the staging folder (this code's own copies) and everything in it; a link only as the link. */
static void delete_tree(const wchar_t *dir)
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
            join(dir, fd.cFileName, a);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                delete_tree(a);
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

static int write_text(const wchar_t *p, const char *t, DWORD disposition)
{
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, disposition, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD put = 0;
    int ok;
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    ok = WriteFile(h, t, (DWORD)strlen(t), &put, NULL) && put == strlen(t) && FlushFileBuffers(h);
    CloseHandle(h);
    return ok;
}

/* The copies in the staging folder moved into saves: the backups and the title data first, the saves last, so a save
 * appears in the data folder only once everything else has. The journal says a commit is under way; a start that
 * finds it finishes the commit from the staging folder (whose copies were checked). A failure with `rollback` (the
 * first try, in a running start) takes back exactly what this commit moved in (its own copies), the staging folder
 * and the journal, so the data folder is as it was and a later start copies again from the saves as they are then;
 * a failure finishing an earlier commit keeps everything for the next start. 0, or a Windows error. */
static DWORD commit(const wchar_t *stage, mig_t *m, int rollback)
{
    static const wchar_t *const order[3] = { L"UDATA_backups", L"TDATA", L"UDATA" };
    wchar_t a[SETUP_PATH_MAX], b[SETUP_PATH_MAX], j[SETUP_PATH_MAX];
    char t[SETUP_PATH_MAX * 3 + 400];
    DWORD e = 0;
    int i;

    join(g_saves, N_JOURNAL, j);
    snprintf(t, sizeof(t), "%s: copies of your saves are being moved in here from\r\n%s\r\n(checked copies, from the "
             "game's folder). If this file is still here at the next start, that start finishes the move.\r\n",
             twoc_app_name(), g_saves8);
    if (!exists(j) && !write_text(j, t, CREATE_NEW)) {
        e = GetLastError();
        mig_say(m, j, e, "writing");
        return e;
    }
    dev_hold("commit", 0);
    for (i = 0; i < 3 && !e; i++) {
        join(stage, order[i], a);
        join(g_saves, order[i], b);
        e = merge_move(a, b, m);
    }
    if (e) {
        unsigned k;
        if (!rollback || m->moved > MIG_MAX_MOVED)
            return e;
        for (k = m->moved; k-- > 0;) {
            if (is_dir(m->done[k]))
                delete_tree(m->done[k]);
            else {
                SetFileAttributesW(m->done[k], FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(m->done[k]);
            }
        }
        delete_tree(stage);
        DeleteFileW(j);
        fprintf(stderr, "[SAVES] the copy's %u moves into %s taken back\n", m->moved, g_saves8);
        return e;
    }
    delete_tree(stage);
    DeleteFileW(j);
    return 0;
}

/* ── The data folder ───────────────────────────────────────────────────── */

static int ascii(const wchar_t *w)
{
    for (; *w; w++)
        if (*w > 0x7E)
            return 0;
    return 1;
}

/* `path`'s \\?\ form, in bytes the toolkit turns back into it with CP_ACP (UTF-8 under the exe's manifest; otherwise
 * the short 8.3 name when the path isn't plain ASCII). 0 when there is no such form. */
static int kform(const wchar_t *path, char *out, size_t cap)
{
    wchar_t w[SETUP_PATH_MAX], s[SETUP_PATH_MAX];
    const wchar_t *src = path;

    if (GetACP() != CP_UTF8 && !ascii(path)) {
        DWORD n = GetShortPathNameW(path, s, SETUP_PATH_MAX);
        if (!n || n >= SETUP_PATH_MAX || !ascii(s))
            return 0;
        src = s;
    }
    if (src[0] == L'\\' && src[1] == L'\\')
        _snwprintf_s(w, SETUP_PATH_MAX, _TRUNCATE, L"\\\\?\\UNC\\%s", src + 2);
    else
        _snwprintf_s(w, SETUP_PATH_MAX, _TRUNCATE, L"\\\\?\\%s", src);
    return WideCharToMultiByte(GetACP() == CP_UTF8 ? CP_UTF8 : CP_ACP, 0, w, -1, out, (int)cap, NULL, NULL) != 0;
}

/* g_kdir from g_saves (g_kok: there is one). */
static int g_kok;
static int kernel_form(void)
{
    g_kok = kform(g_saves, g_kdir, sizeof(g_kdir));
    return g_kok;
}

/* ── The game's save writes, made atomic (both layouts) ───────────────────
 * The game opens a save file (crash.sav, SaveMeta.xbx in UDATA\<title>\<save>\) to write, writes it and closes it;
 * a process killed in between would leave it half-written. So the open is handed a copy in <saves>\.saving\ instead
 * (a fresh file, or the old one's copy when the game opens it to change it in place), and when the game closes it
 * the copy is flushed and renamed over the save (MoveFileEx: replaced whole). A kill before the rename leaves the old
 * save; after it, the new one. The next start deletes what a kill left in .saving (never a save). */
#define N_SAVING L".saving"
#define UD_WRITES 8
static SRWLOCK g_wlock = SRWLOCK_INIT;
static volatile LONG g_wpending;
static struct { int used; uint32_t handle; wchar_t tmp[SETUP_PATH_MAX], fin[SETUP_PATH_MAX]; char name[80]; } g_w[UD_WRITES];

static void saving_cleanup(void)
{
    wchar_t d[SETUP_PATH_MAX], pat[SETUP_PATH_MAX + 4], p[SETUP_PATH_MAX];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    unsigned n = 0;

    join(g_saves, N_SAVING, d);
    _snwprintf_s(pat, sizeof(pat) / sizeof(pat[0]), _TRUNCATE, L"%s\\*", d);
    h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        join(d, fd.cFileName, p);
        SetFileAttributesW(p, FILE_ATTRIBUTE_NORMAL);
        if (DeleteFileW(p))
            n++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    RemoveDirectoryW(d);
    if (n)
        fprintf(stderr, "[SAVES] %u half-written save file(s) from a run that ended during a save deleted (%s\\.saving); "
                "the saves are as they were before that save\n", n, g_saves8);
}

/* The data folder's path: RECOMP_DATA_DIR, else %LOCALAPPDATA%\ProjectWrath (a test's stand-in for
 * %LOCALAPPDATA%: RECOMP_DEV_LOCALAPPDATA, as setup.c's). 0 when none can be made out. */
static int user_dir(wchar_t *out)
{
    const wchar_t *v = _wgetenv(L"RECOMP_DATA_DIR"), *dev = _wgetenv(L"RECOMP_DEV_LOCALAPPDATA");
    wchar_t la[MAX_PATH], w[SETUP_PATH_MAX];
    size_t n;

    if (v && *v)
        _snwprintf_s(w, SETUP_PATH_MAX, _TRUNCATE, L"%s", v);
    else if (dev && *dev)
        _snwprintf_s(w, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", dev, twoc_data_folder_w());
    else if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, la)))
        _snwprintf_s(w, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", la, twoc_data_folder_w());
    else
        return 0;
    if (!GetFullPathNameW(w, SETUP_PATH_MAX, out, NULL))
        return 0;
    n = wcslen(out);
    while (n > 3 && (out[n - 1] == L'\\' || out[n - 1] == L'/'))
        out[--n] = 0;
    return 1;
}

static void about_file(void)
{
    wchar_t p[SETUP_PATH_MAX];
    char t[2400];

    join(g_user, N_ABOUT, p);
    if (exists(p))
        return;
    snprintf(t, sizeof(t),
             "%s's data folder\r\n\r\n"
             "  saves\\UDATA          the game's saves (every slot is in one file, crash.sav)\r\n"
             "  saves\\UDATA_backups  copies of the save, taken at each start and after each save\r\n"
             "  saves\\TDATA          the game's title data\r\n"
             "  %s     your settings (the F1 menu)\r\n"
             "  cache                the game's cache and its emulated hard disk: safe to delete while the game\r\n"
             "                       is closed (it is made again)\r\n\r\n"
             "To back up your saves or move them to another PC, close the game and copy the saves folder (and\r\n"
             "%s if you want your settings too). On the other PC, put them in the same place:\r\n"
             "%%LOCALAPPDATA%%\\%ls (paste that into File Explorer's address bar).\r\n\r\n"
             "Every copy of %s on this PC uses this folder, unless it has a file named portable.txt next to\r\n"
             "its exe (then it keeps everything in its own folder). Replacing or deleting the game's folder\r\n"
             "doesn't touch this one; delete it yourself to remove your saves and settings.\r\n",
             twoc_app_name(), twoc_ini_name(), twoc_ini_name(), twoc_data_folder_w(), twoc_app_name());
    write_text(p, t, CREATE_NEW);
}

/* The package's layout for this run, saying why (`why` "" for a quiet choice). */
static int in_package(const wchar_t *pkg, const char *why, const char *player_msg)
{
    g_outside = 0;
    g_user[0] = 0;
    g_user8[0] = 0;
    g_xbox8[0] = 0;
    wcscpy_s(g_saves, SETUP_PATH_MAX, pkg);
    u8(g_saves, g_saves8, sizeof(g_saves8));
    kernel_form();                      /* for the atomic save writes (none without it: the game writes in place) */
    fprintf(stderr, "[SAVES] in the game's folder this run: %s\\UDATA%s%s%s\n", g_saves8, why[0] ? " (" : "", why,
            why[0] ? ")" : "");
    saving_cleanup();
    if (player_msg && player_msg[0]) {
        snprintf(g_msg, sizeof(g_msg), "%s", player_msg);
        snprintf(g_note, sizeof(g_note), "%s", player_msg);
    }
    fflush(stderr);
    return 0;
}

/* "Old save copies.txt" lists the game folders whose saves were seen beside the data folder's (or copied from):
 * 1 when `pkg8` is listed already, else it is added (with the file's first lines when new) and 0 returned. */
static int seen_listed(const char *pkg8)
{
    wchar_t p[SETUP_PATH_MAX];
    char line[SETUP_PATH_MAX * 3 + 8], *seen = NULL;
    int listed;
    HANDLE h;

    join(g_user, N_SEEN, p);
    snprintf(line, sizeof(line), "%s\r\n", pkg8);
    h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER sz;
        DWORD got = 0;
        if (GetFileSizeEx(h, &sz) && sz.QuadPart < (1 << 20) && (seen = (char *)calloc(1, (size_t)sz.QuadPart + 1)))
            ReadFile(h, seen, (DWORD)sz.QuadPart, &got, NULL);
        CloseHandle(h);
    }
    listed = seen && strstr(seen, line);
    if (!listed) {
        char t[SETUP_PATH_MAX * 3 + 600];
        if (!seen)
            snprintf(t, sizeof(t), "%s keeps its saves in this data folder (saves\\). These game folders also hold "
                     "saves of an older version (copied here at their first start, or found beside these ones): the "
                     "game leaves them as they are. Copies to look at or delete yourself:\r\n%s", twoc_app_name(),
                     line);
        else
            snprintf(t, sizeof(t), "%s", line);
        h = CreateFileW(p, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD put;
            WriteFile(h, t, (DWORD)strlen(t), &put, NULL);
            CloseHandle(h);
        }
    }
    free(seen);
    return listed != 0;
}

/* Both layouts hold saves: the data folder's are used, nothing merged. The first start that finds a given old copy
 * (not one this folder's saves were copied from) says so. */
static void both(const char *pkg8)
{
    fprintf(stderr, "[SAVES] saves in both places: the game uses %s; the copy in %s\\UDATA is left as it is (never "
            "merged or overwritten)\n", g_saves8, pkg8);
    snprintf(g_note, sizeof(g_note), "The game's folder also holds saves (%s\\UDATA): the game uses the ones here, "
             "and that copy is left as it is.", pkg8);
    if (!seen_listed(pkg8))
        snprintf(g_msg, sizeof(g_msg), "Saves were found in two places. The game uses the ones in %s; the copy in "
                 "%s\\UDATA was left as it is.", g_saves8, pkg8);
}

int twoc_userdata_init(void)
{
    const char *in_env = getenv("RECOMP_SAVES_IN_PACKAGE"), *out_env = getenv("RECOMP_SAVES_OUTSIDE");
    wchar_t pkg[SETUP_PATH_MAX], exe[SETUP_PATH_MAX], p[SETUP_PATH_MAX], stage[SETUP_PATH_MAX], j[SETUP_PATH_MAX];
    char pkg8[SETUP_PATH_MAX * 3], why[900];
    wchar_t *slash;
    DWORD e, n;
    int portable = 0;

    join(twoc_data_dir(), L"game_files", pkg);
    u8(pkg, pkg8, sizeof(pkg8));
    if ((n = GetModuleFileNameW(NULL, exe, SETUP_PATH_MAX)) > 0 && n < SETUP_PATH_MAX &&
        (slash = wcsrchr(exe, L'\\')) != NULL) {
        *slash = 0;
        join(exe, L"portable.txt", p);
        portable = exists(p);
    }
    if (in_env && strcmp(in_env, "1") == 0)
        return in_package(pkg, "RECOMP_SAVES_IN_PACKAGE=1", NULL);
    if (portable)
        return in_package(pkg, "portable.txt is next to the exe", NULL);
    if (twoc_names_dev() && !(out_env && strcmp(out_env, "1") == 0))
        return in_package(pkg, "", NULL);   /* the developer's crash_twoc.exe, as before (RECOMP_SAVES_OUTSIDE=1 moves them) */

    if (!user_dir(g_user))
        return in_package(pkg, "no data folder could be made out", "Your saves stay in the game's folder this run: "
                          "the data folder in your user folder couldn't be found.");
    u8(g_user, g_user8, sizeof(g_user8));
    join(g_user, N_SAVES, g_saves);
    u8(g_saves, g_saves8, sizeof(g_saves8));
    if (wcslen(g_user) > UD_MAX_DIR) {
        snprintf(why, sizeof(why), "Your saves stay in the game's folder this run: the data folder's path (%s) is "
                 "longer than %u characters.", g_user8, UD_MAX_DIR);
        return in_package(pkg, "the data folder's path is too long", why);
    }
    e = (DWORD)SHCreateDirectoryExW(NULL, g_saves, NULL);
    if (e != ERROR_SUCCESS && e != ERROR_ALREADY_EXISTS && e != ERROR_FILE_EXISTS) {
        snprintf(why, sizeof(why), "Your saves stay in the game's folder (%s\\UDATA) this run: the folder %s couldn't "
                 "be made (%s).", pkg8, g_saves8, err_words(e));
        return in_package(pkg, "the data folder couldn't be made", why);
    }
    if ((e = write_test(g_saves)) != 0) {
        snprintf(why, sizeof(why), "Your saves stay in the game's folder (%s\\UDATA) this run: %s can't be written to "
                 "(%s). Make it writable, or free some space, and start the game again.", pkg8, g_saves8,
                 err_words(e));
        return in_package(pkg, "the data folder can't be written to", why);
    }
    if (!kernel_form()) {
        snprintf(why, sizeof(why), "Your saves stay in the game's folder this run: the data folder's name (%s) can't "
                 "be handed to the game on this version of Windows.", g_user8);
        return in_package(pkg, "the data folder's name can't be handed to the game", why);
    }

    /* A migration that stopped part-way. */
    join(g_user, N_STAGE, stage);
    join(g_saves, N_JOURNAL, j);
    if (exists(j)) {
        mig_t m;
        memset(&m, 0, sizeof(m));
        if (is_dir(stage)) {
            e = commit(stage, &m, 0);
            mig_free(&m);
            if (e) {
                snprintf(why, sizeof(why), "Your saves stay in the game's folder (%s\\UDATA) this run: the copy into "
                         "%s that stopped last time couldn't be finished (%s). Nothing was deleted; the next start "
                         "tries again.", pkg8, g_saves8, m.why);
                return in_package(pkg, "the last copy couldn't be finished", why);
            }
            fprintf(stderr, "[SAVES] the copy of the saves that stopped last time was finished: %u items moved into "
                    "%s\n", m.moved, g_saves8);
            snprintf(g_msg, sizeof(g_msg), "Your saves were copied to %s. The old copy was left where it was.",
                     g_saves8);
            seen_listed(pkg8);          /* the old copy noted (below: not a second set of saves) */
        }
        DeleteFileW(j);
    } else if (is_dir(stage)) {
        fprintf(stderr, "[SAVES] a copy of the saves that stopped part-way (%s, this game's own): deleted, made again\n",
                g_user8);
        delete_tree(stage);
    }

    /* The first start with the data folder: the package's saves copied in. */
    if (!has_save(g_saves) && !has_backup(g_saves) && files_to_copy(pkg)) {
        static const wchar_t *const k[3] = { L"UDATA", L"UDATA_backups", L"TDATA" };
        mig_t m;
        LARGE_INTEGER f, t0, t1;
        int i;

        memset(&m, 0, sizeof(m));
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&t0);
        delete_tree(stage);
        e = 0;
        if (!CreateDirectoryW(stage, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
            e = GetLastError();
            mig_say(&m, stage, e, "making the folder");
        }
        for (i = 0; i < 3 && !e; i++) {
            wchar_t a[SETUP_PATH_MAX], b[SETUP_PATH_MAX];
            join(pkg, k[i], a);
            join(stage, k[i], b);
            if (is_dir(a))
                e = copy_tree(a, b, &m);
        }
        if (e)
            delete_tree(stage);         /* this code's own copies: the old saves were only read */
        else if ((e = commit(stage, &m, 1)) != 0 && !exists(j))
            delete_tree(stage);         /* taken back, or the journal couldn't be written: nothing of it is left */
        /* else (more moves than it can take back): the journal and the checked copies stay, and the next start
         * finishes the commit (above) before the data folder is used */
        mig_free(&m);
        if (e) {
            fprintf(stderr, "[SAVES] the saves couldn't be copied from %s to %s: %s; this run uses them where they "
                    "are, nothing deleted\n", pkg8, g_saves8, m.why);
            snprintf(why, sizeof(why), "Your saves couldn't be copied to %s (%s), so this run keeps using them in the "
                     "game's folder (%s\\UDATA). Nothing was deleted.", g_saves8, m.why, pkg8);
            return in_package(pkg, "the copy failed", why);
        }
        QueryPerformanceCounter(&t1);
        fprintf(stderr, "[SAVES] the saves copied from %s to %s: %u files, %.1f KB, every file's SHA-256 checked, %u "
                "items moved in%s%s (%.0f ms); the old copy was left where it was\n", pkg8, g_saves8, m.files,
                (double)m.bytes / 1024.0, m.moved, m.kept ? ", files already there kept" : "",
                m.links ? ", links not followed" : "", (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart);
        snprintf(g_msg, sizeof(g_msg), "Your saves were copied to %s. The old copy was left where it was.", g_saves8);
        snprintf(g_note, sizeof(g_note), "Copied from %s\\UDATA at this start (the old copy was left there).", pkg8);
        seen_listed(pkg8);              /* the old copy noted, so later starts don't take it for a second set */
    } else if (has_save(g_saves) && has_save(pkg))
        both(pkg8);

    g_outside = 1;
    saving_cleanup();
    join(g_user, L"cache", p);
    u8(p, g_xbox8, sizeof(g_xbox8));
    about_file();
    fprintf(stderr, "[SAVES] outside the game's folder: saves and backups in %s, settings and the toolkit's disk "
            "(cache) in %s%s\n", g_saves8, g_user8, has_save(g_saves) ? "" : " (no save yet)");
    fflush(stderr);
    return 1;
}

int twoc_saves_outside(void)
{
    return g_outside;
}

const wchar_t *twoc_saves_dir(void)
{
    return g_saves;
}

const char *twoc_saves_dir_utf8(void)
{
    return g_saves8;
}

const wchar_t *twoc_user_dir(void)
{
    return g_user;
}

const char *twoc_user_dir_utf8(void)
{
    return g_user8;
}

const char *twoc_xbox_dir_utf8(void)
{
    return g_xbox8[0] ? g_xbox8 : NULL;
}

const char *twoc_userdata_message(void)
{
    return g_msg;
}

const char *twoc_userdata_note(void)
{
    return g_note;
}

/* ── The guest's opens ─────────────────────────────────────────────────── */

/* RECOMP_DEV_SAVE_WRITE_HOLD_MS (a test aid): the game's save file stays written but not yet put in place this long
 * at its close, logged, so a test can kill the process there. */
static DWORD dev_write_hold(void)
{
    const char *v = getenv("RECOMP_DEV_SAVE_WRITE_HOLD_MS");
    return v && atoi(v) > 0 ? (DWORD)atoi(v) : 0;
}

int twoc_userdata_redirect(const char *path, unsigned len, uint32_t root, uint32_t access, uint32_t disposition,
                           uint32_t options, char *out, size_t cap, char *eff, size_t ecap, int *wtok)
{
    static const char dev[] = "\\Device\\Harddisk0\\Partition1\\";
    const size_t dl = sizeof(dev) - 1;
    char p[600], *o;
    const char *q, *rest, *t;
    size_t n;
    int parts, k;

    *wtok = -1;
    if (!g_saves[0] || !path || !len || len >= sizeof(p))
        return 0;
    memcpy(p, path, len);
    p[len] = 0;
    if (root && root != 0xFFFFFFFDu && p[0] != '\\' && !(p[0] && p[1] == ':'))
        return 0;                       /* relative to a handle: the handle's own folder decides */
    /* the toolkit's resolve_symlink: \??\X: or X: through the link the title made */
    q = (_strnicmp(p, "\\??\\", 4) == 0 && p[4] && p[5] == ':') ? p + 4 : p;
    if (q[0] && q[1] == ':') {
        char link[8] = { '\\', '?', '?', '\\', (char)toupper((unsigned char)q[0]), ':', 0, 0 };
        t = xbox_LookupSymbolicLink(link);
        if (!t || !t[0])
            return 0;
        rest = q + 2;
        while (*rest == '\\' || *rest == '/')
            rest++;
        n = strlen(t);
        snprintf(eff, ecap, "%s%s%s", t, n && t[n - 1] != '\\' && t[n - 1] != '/' ? "\\" : "", rest);
    } else
        snprintf(eff, ecap, "%s", p);
    if (_strnicmp(eff, dev, dl) != 0)
        return 0;
    rest = eff + dl;
    if ((_strnicmp(rest, "UDATA", 5) != 0 && _strnicmp(rest, "TDATA", 5) != 0) ||
        (rest[5] && rest[5] != '\\' && rest[5] != '/'))
        return 0;

    /* A save file opened to write (UDATA\<title>\<save>\<file>, not a folder, not to delete): to a copy in .saving,
     * put in place at its close (above). Needs the kernel form of the saves folder. */
    for (parts = 1, o = (char *)rest; *o; o++)
        if ((*o == '\\' || *o == '/') && o[1] && o[1] != '\\' && o[1] != '/')
            parts++;
    if (g_kok && parts == 4 && _strnicmp(rest, "UDATA", 5) == 0 && !(options & 0x1 /* FILE_DIRECTORY_FILE */) &&
        !(options & 0x1000 /* FILE_DELETE_ON_CLOSE */) && !(access & 0x10000 /* DELETE */) &&
        (access & (0x40000000u /* GENERIC_WRITE */ | 0x10000000u /* GENERIC_ALL */ | 0x2 | 0x4 /* WRITE/APPEND_DATA */))) {
        wchar_t fin[SETUP_PATH_MAX], tmp[SETUP_PATH_MAX], rw[600], *c;
        const char *save, *file;
        int there, fresh = -1;

        MultiByteToWideChar(CP_UTF8, 0, rest, -1, rw, 600);
        for (c = rw; *c; c++)
            if (*c == L'/')
                *c = L'\\';
        _snwprintf_s(fin, SETUP_PATH_MAX, _TRUNCATE, L"%s\\%s", g_saves, rw);
        there = exists(fin);
        switch (disposition) {          /* the kernel's own answer when the copy can't stand in for it */
        case 0: case 5: fresh = 1; break;                           /* SUPERSEDE, OVERWRITE_IF */
        case 3: fresh = !there; break;                              /* OPEN_IF */
        case 1: fresh = there ? 0 : -1; break;                      /* OPEN: missing stays missing */
        case 4: fresh = there ? 1 : -1; break;                      /* OVERWRITE: missing stays missing */
        case 2: fresh = there ? -1 : 1; break;                      /* CREATE: there stays a collision */
        }
        file = strrchr(rest, '\\');
        file = file ? file + 1 : rest;
        save = file - 2;
        while (save > rest && *save != '\\')
            save--;
        if (*save == '\\')
            save++;
        if (fresh >= 0) {
            AcquireSRWLockExclusive(&g_wlock);
            for (k = 0; k < UD_WRITES && g_w[k].used; k++)
                ;
            if (k < UD_WRITES) {
                wchar_t d[SETUP_PATH_MAX], flat[200];
                _snwprintf_s(flat, 200, _TRUNCATE, L"%.*S.%S", (int)(file - 1 - save), save, file);
                join(g_saves, N_SAVING, d);
                CreateDirectoryW(d, NULL);
                join(d, flat, tmp);
                SetFileAttributesW(tmp, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(tmp);
                if ((fresh || CopyFileW(fin, tmp, FALSE)) && kform(tmp, out, cap) && strlen(out) <= 250) {
                    g_w[k].used = 1;
                    g_w[k].handle = 0;
                    wcscpy_s(g_w[k].tmp, SETUP_PATH_MAX, tmp);
                    wcscpy_s(g_w[k].fin, SETUP_PATH_MAX, fin);
                    snprintf(g_w[k].name, sizeof(g_w[k].name), "%.*s\\%s", (int)(file - 1 - save), save, file);
                    *wtok = k;
                }
            }
            ReleaseSRWLockExclusive(&g_wlock);
            if (*wtok >= 0)
                return 1;
            fprintf(stderr, "[SAVES] the game's write of %s goes straight to the save (no copy could be made first)\n",
                    file);
        }
    }

    if (!g_outside)
        return 0;                       /* the package's layout: the toolkit maps it to game_files as before */
    if (snprintf(out, cap, "%s\\%s", g_kdir, rest) >= (int)cap || strlen(out) > 250)
        return -1;                      /* too long for the toolkit's 260: refused, never sent to the game's folder */
    for (o = out; *o; o++)
        if (*o == '/')
            *o = '\\';
    n = strlen(out);
    while (n > strlen(g_kdir) && out[n - 1] == '\\')
        out[--n] = 0;
    return 1;
}

void twoc_userdata_write_opened(int tok, int ok, uint32_t handle)
{
    if (tok < 0 || tok >= UD_WRITES)
        return;
    AcquireSRWLockExclusive(&g_wlock);
    if (ok && handle) {
        g_w[tok].handle = handle;
        InterlockedIncrement(&g_wpending);
    } else {
        DeleteFileW(g_w[tok].tmp);      /* the open failed: the copy is nobody's */
        g_w[tok].used = 0;
    }
    ReleaseSRWLockExclusive(&g_wlock);
}

int twoc_userdata_closing(uint32_t handle)
{
    int k, r = -1;
    DWORD hold;

    if (!g_wpending)
        return -1;
    AcquireSRWLockShared(&g_wlock);
    for (k = 0; k < UD_WRITES; k++)
        if (g_w[k].used && g_w[k].handle == handle)
            r = k;
    ReleaseSRWLockShared(&g_wlock);
    if (r >= 0 && (hold = dev_write_hold()) != 0) {
        fprintf(stderr, "[SAVES] test hold (RECOMP_DEV_SAVE_WRITE_HOLD_MS): %s written, %lu ms before it is put in "
                "place\n", g_w[r].name, (unsigned long)hold);
        fflush(stderr);
        Sleep(hold);
    }
    return r;
}

void twoc_userdata_closed(int tok)
{
    HANDLE h;
    int i;
    DWORD e = 0;

    if (tok < 0 || tok >= UD_WRITES)
        return;
    h = CreateFileW(g_w[tok].tmp, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(h);
        CloseHandle(h);
    }
    for (i = 0; i < 20; i++) {          /* an antivirus or the indexer reading the save: a few tries */
        if (MoveFileExW(g_w[tok].tmp, g_w[tok].fin, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            e = 0;
            break;
        }
        e = GetLastError();
        Sleep(50 + 25 * i);
    }
    if (e && CopyFileW(g_w[tok].tmp, g_w[tok].fin, FALSE)) {
        DeleteFileW(g_w[tok].tmp);
        fprintf(stderr, "[SAVES] the game's write of %s couldn't be renamed into place (Windows error %lu): copied over "
                "it instead\n", g_w[tok].name, (unsigned long)e);
    } else if (e)
        fprintf(stderr, "[SAVES] the game's write of %s couldn't be put in place (Windows error %lu): the save is the "
                "one before; the written copy is in %s\\.saving until the next start\n", g_w[tok].name,
                (unsigned long)e, g_saves8);
    else
        fprintf(stderr, "[SAVES] the game's write of %s put in place (written to a copy, then renamed over it)\n",
                g_w[tok].name);
    AcquireSRWLockExclusive(&g_wlock);
    g_w[tok].used = 0;
    g_w[tok].handle = 0;
    if (InterlockedDecrement(&g_wpending) == 0) {   /* the last write in flight: .saving goes when it is empty */
        wchar_t d[SETUP_PATH_MAX];
        join(g_saves, N_SAVING, d);
        RemoveDirectoryW(d);
    }
    ReleaseSRWLockExclusive(&g_wlock);
}
