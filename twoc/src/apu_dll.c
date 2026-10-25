/*
 * apu_dll.c -- the exe's side of xemu_apu.dll, the game's audio library (step 108, notes 18.111).
 *
 * Before step 108 xboxrecomp's emulated APU (src/apu: from xemu, GNU LGPL 2.1 or later) was linked into the exe
 * as the static library xbox_apu. It is now xemu_apu.dll, built from the same files (twoc/apu_dll), so that a
 * player can rebuild it from its source and put their copy in its place. Here:
 *
 *   - twoc_apu_dll_load(): at the start, the DLL from the exe's own folder, by full path, with only that folder
 *     and System32 searched for what it imports (LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | ..._SYSTEM32): never the
 *     current folder or PATH. Its interface version is checked (apu_dll/xemu_apu_api.h). Missing, unloadable or
 *     another version: a message box in plain words (what, where, what to do), the log, and the process ends.
 *     Its size and SHA-256 go into the log ([APU-DLL]), so a log says which DLL ran.
 *   - the functions the rest of the exe always called the APU by, each passing the call on to the DLL's table
 *     (main.c's init and register trap, host_audio.cpp's gains and HRTF, overlay.cpp's and recomp_manual.c's
 *     channel count). Before the DLL is loaded they do nothing (no APU), as the static library did with no init.
 */
#include <windows.h>
#include <bcrypt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

#include "apu_dll.h"
#include "xemu_apu_api.h"
#include "names.h"

typedef struct MCPXAPUState MCPXAPUState;

static const XemuApuApi *g_api;
MCPXAPUState *g_apu_state;              /* main.c's: the APU, or NULL (no RECOMP_AC97_READY, or init failed) */

/* A wide string as UTF-8 for the log (the process's narrow strings are UTF-8, notes 18.110). */
static const char *utf8(const wchar_t *w, char *buf, int cap)
{
    if (WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, cap, NULL, NULL) <= 0)
        buf[0] = 0;
    return buf;
}

/* `todo` may name the exe with %ls (twice at most): the running exe's own file name goes there (step 109:
 * ProjectWrath.exe in the packages, names.h). */
static void apu_dll_fail(const wchar_t *path, const wchar_t *what, const wchar_t *todo_fmt)
{
    wchar_t msg[4096], todo[1024], title[96];
    static char u1[2048], u2[4 * 32768];

    _snwprintf(todo, sizeof(todo) / sizeof(todo[0]) - 1, todo_fmt, twoc_exe_name_w(), twoc_exe_name_w());
    todo[sizeof(todo) / sizeof(todo[0]) - 1] = 0;
    _snwprintf(title, sizeof(title) / sizeof(title[0]) - 1, L"%ls", twoc_app_name_w());
    title[sizeof(title) / sizeof(title[0]) - 1] = 0;
    _snwprintf(msg, sizeof(msg) / sizeof(msg[0]) - 1,
               L"The game can't start: %ls\n\n"
               L"The file:\n%ls\n\n"
               L"%ls\n\n"
               L"(xemu_apu.dll is the game's audio library, the Xbox sound chip's emulation taken from the xemu "
               L"emulator. It is under the GNU LGPL; its source is in the Share package's Source folder.)",
               what, path, todo);
    msg[sizeof(msg) / sizeof(msg[0]) - 1] = 0;
    fprintf(stderr, "[APU-DLL] The game can't start: %s The file: %s\n", utf8(what, u1, sizeof(u1)),
            utf8(path, u2, sizeof(u2)));
    fflush(stderr);
    MessageBoxW(NULL, msg, title, MB_ICONERROR | MB_OK | MB_SETFOREGROUND);
}

/* The file's size and SHA-256 (hex), for the log. 0 if it can't be read. */
static int apu_dll_hash(const wchar_t *path, char hex[65], unsigned long long *size)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE h = NULL;
    unsigned char buf[65536], d[32];
    DWORD n;
    int ok = 0, i;

    *size = 0;
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 &&
            BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0) == 0) {
        while (ReadFile(f, buf, sizeof(buf), &n, NULL) && n) {
            BCryptHashData(h, buf, n, 0);
            *size += n;
        }
        if (BCryptFinishHash(h, d, sizeof(d), 0) == 0) {
            for (i = 0; i < 32; i++)
                sprintf(hex + 2 * i, "%02x", d[i]);
            ok = 1;
        }
    }
    if (h)
        BCryptDestroyHash(h);
    if (alg)
        BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    return ok;
}

int twoc_apu_dll_load(void)
{
    static wchar_t path[32768];
    wchar_t what[512], *slash;
    DWORD n = GetModuleFileNameW(NULL, path, 32768 - 32), err;
    HMODULE h;
    XemuApuGetApiFn get;
    uint32_t dll_version = 0;
    const XemuApuApi *api;
    char hex[65] = "?";
    static char u8[4 * 32768];
    unsigned long long size = 0;

    if (n == 0 || n >= 32768 - 32 || !(slash = wcsrchr(path, L'\\')))
        return apu_dll_fail(L"(the game's own folder couldn't be found)", L"its folder couldn't be worked out",
                            L"Start %ls from its folder."), 0;
    wcscpy(slash + 1, L"" XEMU_APU_DLL_NAME);

    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        apu_dll_fail(path, L"its audio library, xemu_apu.dll, is missing.",
                     L"It has to be in the same folder as %ls. Copy it back from the package the game "
                     L"came in (or unpack the package again), then start the game again.");
        return 0;
    }
    h = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h) {
        err = GetLastError();
        _snwprintf(what, 511, L"its audio library, xemu_apu.dll, couldn't be loaded (Windows error %lu%ls).",
                   (unsigned long)err, err == ERROR_BAD_EXE_FORMAT ? L": not a 64-bit Windows DLL, or damaged" :
                   err == ERROR_MOD_NOT_FOUND ? L": a file it needs is missing" : L"");
        what[511] = 0;
        apu_dll_fail(path, what,
                     L"Put back the xemu_apu.dll that came with this %ls (from the same package), then "
                     L"start the game again. If you built it yourself, build it again for 64-bit Windows "
                     L"(the source's build.cmd).");
        return 0;
    }
    get = (XemuApuGetApiFn)(void *)GetProcAddress(h, XEMU_APU_GET_API_NAME);
    if (!get) {
        apu_dll_fail(path, L"the file named xemu_apu.dll isn't the game's audio library (it has no "
                           L"xemu_apu_get_api).",
                     L"Put back the xemu_apu.dll that came with this %ls (from the same package), then "
                     L"start the game again.");
        FreeLibrary(h);
        return 0;
    }
    api = get(XEMU_APU_INTERFACE_VERSION, &dll_version);
    if (!api || api->version != XEMU_APU_INTERFACE_VERSION || api->size < sizeof(XemuApuApi)) {
        _snwprintf(what, 511, L"its audio library, xemu_apu.dll, is a different version: it offers interface "
                              L"version %lu, and this %ls needs version %d.",
                   (unsigned long)(api ? api->version : dll_version), twoc_exe_name_w(), XEMU_APU_INTERFACE_VERSION);
        what[511] = 0;
        apu_dll_fail(path, what,
                     L"Use the xemu_apu.dll that came with this %ls (from the same package). If you "
                     L"built it yourself, build it from the source that came with this %ls.");
        FreeLibrary(h);
        return 0;
    }
    g_api = api;
    apu_dll_hash(path, hex, &size);
    fprintf(stderr, "[APU-DLL] %s loaded: interface %u, %llu bytes, SHA-256 %s\n",
            utf8(path, u8, sizeof(u8)), (unsigned)api->version, size, hex);
    return 1;
}

/* The names the exe has always called the APU by (see the top). */

MCPXAPUState *mcpx_apu_init_standalone(uint8_t *ram_ptr)
{
    return g_api ? (MCPXAPUState *)g_api->init(ram_ptr) : NULL;
}

bool apu_hook_handle_mmio(PCONTEXT ctx, uintptr_t fault_addr, uint32_t fault_xbox_va, int is_write)
{
    return g_api && g_api->handle_mmio(ctx, fault_addr, fault_xbox_va, is_write);
}

void mcpx_apu_set_voice_gain(int stream, float gain)
{
    if (g_api)
        g_api->set_voice_gain(stream, gain);
}

void mcpx_apu_set_hrtf(int on)
{
    if (g_api)
        g_api->set_hrtf(on);
}

int mcpx_apu_get_hrtf(void)
{
    return g_api ? g_api->get_hrtf() : 0;
}

int mcpx_apu_output_channels(int *device_channels)
{
    if (!g_api) {
        if (device_channels)
            *device_channels = 0;
        return 0;
    }
    return g_api->output_channels(device_channels);
}
