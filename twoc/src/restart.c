/**
 * Quitting and starting again from the F1 menu (step 97). See restart.h.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wchar.h>

#include "restart.h"
#include "saves.h"
#include "settings.h"

static wchar_t *g_env0;                 /* the environment at the start, without TWOC_RESTART_PARENT */
static size_t g_env0_n;                 /* ... its wchar_ts, the last NUL included */
static wchar_t g_cwd0[MAX_PATH * 2];
static wchar_t *g_cmd0;
static char g_launcher[64];
static volatile LONG g_ending;
static PROCESS_INFORMATION g_child;     /* a restart without a launcher: the new copy */

typedef struct {
    int restart, self;
    char why[256];
    HWND hwnd;
} end_t;

const char *twoc_restart_launcher(void)
{
    return g_launcher[0] ? g_launcher : NULL;
}

int twoc_ending(void)
{
    return g_ending != 0;
}

void twoc_restart_init(void)
{
    wchar_t *e = GetEnvironmentStringsW(), *p;
    size_t n = 0;
    DWORD parent = 0;
    const char *l = getenv("TWOC_LAUNCHER");

    if (l && *l)
        snprintf(g_launcher, sizeof(g_launcher), "%s", l);
    for (p = e; p && *p; p += wcslen(p) + 1)
        n += wcslen(p) + 1;
    g_env0 = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
    if (g_env0) {
        for (p = e; p && *p; p += wcslen(p) + 1) {
            size_t k = wcslen(p) + 1;
            if (_wcsnicmp(p, L"TWOC_RESTART_PARENT=", 20) == 0) {
                parent = wcstoul(p + 20, NULL, 10);
                continue;
            }
            memcpy(g_env0 + g_env0_n, p, k * sizeof(wchar_t));
            g_env0_n += k;
        }
        g_env0[g_env0_n++] = 0;
    }
    if (e)
        FreeEnvironmentStringsW(e);
    if (!GetCurrentDirectoryW(sizeof(g_cwd0) / sizeof(g_cwd0[0]), g_cwd0))
        g_cwd0[0] = 0;
    g_cmd0 = _wcsdup(GetCommandLineW());
    if (parent) {                       /* started again by the copy before: never two at once */
        HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, parent);
        ULONGLONG t0 = GetTickCount64();
        DWORD w = h ? WaitForSingleObject(h, 30000) : WAIT_OBJECT_0;
        SetEnvironmentVariableW(L"TWOC_RESTART_PARENT", NULL);
        _putenv("TWOC_RESTART_PARENT=");
        if (h)
            CloseHandle(h);
        if (w == WAIT_TIMEOUT) {
            fprintf(stderr, "[RESTART] the copy before (pid %lu) is still running after 30 s: this one ends\n",
                    (unsigned long)parent);
            MessageBoxA(NULL, "The game was asked to start again, but its earlier copy is still running after 30 "
                        "seconds, so this copy closes (two must never run at once). Close the other one and start "
                        "the game again.", "Crash TWOC", MB_ICONWARNING | MB_OK);
            TerminateProcess(GetCurrentProcess(), 1);
        }
        fprintf(stderr, "[RESTART] started again by the game itself (Restart now): the copy before (pid %lu) %s"
                " %llu ms\n", (unsigned long)parent, h ? "was gone after" : "had already gone;", GetTickCount64() - t0);
    } else if (g_launcher[0]) {
        fprintf(stderr, "[RESTART] started by %s (TWOC_LAUNCHER): Restart now ends with code %d for it\n", g_launcher,
                TWOC_EXIT_RESTART);
    }
}

/* The loop guard: the restarts of the last minute (%TEMP%\crash_twoc_restarts.txt, one time a line). With
 * `add` and fewer than TWOC_RESTARTS_A_MINUTE, this one is added; returns the count before it. */
static int history(int add)
{
    wchar_t path[MAX_PATH + 32];
    unsigned long long now, t, kept[16];
    int n = 0, i;
    FILETIME ft;
    FILE *f;

    GetSystemTimeAsFileTime(&ft);
    now = (((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000000ULL;
    if (!GetTempPathW(MAX_PATH, path))
        return 0;
    wcscat(path, L"crash_twoc_restarts.txt");
    if ((f = _wfopen(path, L"r")) != NULL) {
        while (n < 16 && fscanf(f, "%llu", &t) == 1)
            if (t <= now && now - t < 60)
                kept[n++] = t;
        fclose(f);
    }
    if (add && n < TWOC_RESTARTS_A_MINUTE && (f = _wfopen(path, L"w")) != NULL) {
        for (i = 0; i < n; i++)
            fprintf(f, "%llu\n", kept[i]);
        fprintf(f, "%llu\n", now);
        fclose(f);
    }
    return n;
}

/* The new copy: the same exe, command line, working folder and environment, the same console and standard
 * handles (the log files go on). It waits for this one to be gone before it does anything
 * (TWOC_RESTART_PARENT, twoc_restart_init), so it is made before anything here is closed: a failure leaves
 * the game running, and should this copy die on the way out, the new one still starts. */
static int spawn(char *err, size_t cap)
{
    wchar_t exe[MAX_PATH * 2], add[48], *env, *cmd;
    HANDLE std[3];
    STARTUPINFOW si;
    size_t an;
    int i;
    BOOL ok;

    if (!g_env0 || !g_cmd0 || !GetModuleFileNameW(NULL, exe, sizeof(exe) / sizeof(exe[0]))) {
        snprintf(err, cap, "the game couldn't start itself again (what it was started with wasn't kept)");
        return -1;
    }
    if (getenv("RECOMP_DEV_RESTART_FAIL") && *getenv("RECOMP_DEV_RESTART_FAIL")) {
        snprintf(err, cap, "the game couldn't start itself again: RECOMP_DEV_RESTART_FAIL (a test)");
        return -1;                      /* test only: the start that fails */
    }
    swprintf(add, sizeof(add) / sizeof(add[0]), L"TWOC_RESTART_PARENT=%lu", (unsigned long)GetCurrentProcessId());
    an = wcslen(add) + 1;
    env = (wchar_t *)malloc((g_env0_n + an + 1) * sizeof(wchar_t));
    cmd = _wcsdup(g_cmd0);
    if (!env || !cmd) {
        free(env);
        free(cmd);
        snprintf(err, cap, "the game couldn't start itself again (out of memory)");
        return -1;
    }
    memcpy(env, g_env0, (g_env0_n - 1) * sizeof(wchar_t));     /* without the block's last NUL */
    memcpy(env + g_env0_n - 1, add, an * sizeof(wchar_t));
    env[g_env0_n - 1 + an] = 0;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    std[0] = GetStdHandle(STD_INPUT_HANDLE);
    std[1] = GetStdHandle(STD_OUTPUT_HANDLE);
    std[2] = GetStdHandle(STD_ERROR_HANDLE);
    for (i = 0; i < 3; i++)
        if (std[i] && std[i] != INVALID_HANDLE_VALUE)
            SetHandleInformation(std[i], HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = std[0];
    si.hStdOutput = std[1];
    si.hStdError = std[2];
    ok = CreateProcessW(exe, cmd, NULL, NULL, TRUE, CREATE_UNICODE_ENVIRONMENT, env,
                        g_cwd0[0] ? g_cwd0 : NULL, &si, &g_child);
    free(env);
    free(cmd);
    if (!ok) {
        DWORD e = GetLastError();
        char msg[160];
        if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0, msg,
                            sizeof(msg), NULL))
            snprintf(msg, sizeof(msg), "error %lu", (unsigned long)e);
        msg[strcspn(msg, "\r\n")] = 0;
        snprintf(err, cap, "the game couldn't start itself again: %s", msg);
        return -1;
    }
    return 0;
}

static DWORD WINAPI end_thread(void *arg)
{
    end_t *e = (end_t *)arg;
    const char *tag = e->restart ? "[RESTART]" : "[QUIT]";
    char what[256];

    saves_quiesce(10000, what, sizeof(what));
    fprintf(stderr, "%s save backups: %s\n", tag, what);
    twoc_audio_close();
    fprintf(stderr, "%s audio: the game's voices silenced, their output played out\n", tag);
    if (e->hwnd)
        ShowWindowAsync(e->hwnd, SW_HIDE);
    if (!e->restart)
        fprintf(stderr, "[QUIT] the game quits from the F1 menu's Quit game (%s): the window closed, exit code 0\n",
                e->why);
    else if (!e->self)
        fprintf(stderr, "[RESTART] Restart now from the F1 menu (%s): the window closed, exit code %d for %s, which"
                " starts the game again\n", e->why, TWOC_EXIT_RESTART, g_launcher);
    else
        fprintf(stderr, "[RESTART] Restart now from the F1 menu (%s): the window closed; the new copy (pid %lu)"
                " starts once this one has ended, exit code 0\n", e->why, (unsigned long)g_child.dwProcessId);
    fflush(stdout);
    fflush(stderr);
    if (e->self) {
        CloseHandle(g_child.hThread);
        CloseHandle(g_child.hProcess);
    }
    TerminateProcess(GetCurrentProcess(), e->restart && !e->self ? TWOC_EXIT_RESTART : 0);
    return 0;
}

int twoc_end_begin(int restart, const char *why, HWND hwnd, char *err, size_t cap)
{
    static end_t e;
    HANDLE th;

    err[0] = 0;
    if (g_ending)
        return 0;
    memset(&e, 0, sizeof(e));
    e.restart = restart;
    e.hwnd = hwnd;
    snprintf(e.why, sizeof(e.why), "%s", why && *why ? why : restart ? "a setting from the next start" : "asked");
    if (restart) {
        int n = history(0);
        if (n >= TWOC_RESTARTS_A_MINUTE) {
            snprintf(err, cap, "Not restarted: %d restarts in the last minute. Quit and start it yourself.", n);
            fprintf(stderr, "[RESTART] refused: %d restarts in the last minute (the loop guard)\n", n);
            return -1;
        }
        if (g_launcher[0]) {
            wchar_t exe[MAX_PATH * 2];
            if (!GetModuleFileNameW(NULL, exe, sizeof(exe) / sizeof(exe[0])) ||
                GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES) {
                snprintf(err, cap, "Not restarted: crash_twoc.exe can't be found to start again.");
                fprintf(stderr, "[RESTART] refused: the exe's file isn't there\n");
                return -1;
            }
        } else if (spawn(err, cap) != 0) {
            fprintf(stderr, "[RESTART] %s; the game goes on\n", err);
            snprintf(err, cap, "Not restarted: the game couldn't start itself again (the log says why).");
            return -1;
        }
        e.self = !g_launcher[0];
        history(1);
    }
    InterlockedExchange(&g_ending, 1);
    fprintf(stderr, "%s %s from the F1 menu (%s)%s\n", restart ? "[RESTART]" : "[QUIT]",
            restart ? "Restart now" : "Quit game", e.why,
            !restart ? "" : e.self ? ": the exe starts itself again (no launcher)" : ": by the launcher");
    th = CreateThread(NULL, 0, end_thread, &e, 0, NULL);
    if (!th) {                          /* no thread: the same steps here */
        end_thread(&e);
        return 0;
    }
    CloseHandle(th);
    return 0;
}
