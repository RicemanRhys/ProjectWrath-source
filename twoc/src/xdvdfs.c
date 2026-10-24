/**
 * An Xbox disc image reader (step 107, notes 18.110). See xdvdfs.h.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xdvdfs.h"

#define XDVD_MAX_DEPTH      64
#define XDVD_MAX_ENTRIES    1000000
#define XDVD_MAX_TABLE      (64u << 20)     /* one folder's table */
#define XDVD_MAX_PATH       1024            /* bytes of a relative path */

static const char k_magic[20] = "MICROSOFT*XBOX*MEDIA";

/* The partition offsets a volume descriptor is looked for at, with their names. */
static const struct { uint64_t base; const char *name; } k_layouts[] = {
    { 0x00000000ull, "trimmed XISO (the game partition at offset 0)" },
    { 0x18300000ull, "full disc dump, Redump-style (XGD1: the game partition at 0x18300000)" },
    { 0x0FD90000ull, "full disc dump (XGD2: the game partition at 0x0FD90000)" },
    { 0x02080000ull, "full disc dump (XGD3: the game partition at 0x02080000)" },
};

struct xdvd {
    HANDLE h;
    uint64_t size, base;
    const char *layout;
    xdvd_entry_t *e;
    size_t n, cap;
    size_t files;
    uint64_t bytes;
    size_t *sorted;             /* entry indices by path, any case (xdvd_find) */
};

static void mb(uint64_t v, char *buf, size_t cap)
{
    snprintf(buf, cap, "%.1f MB", (double)v / 1048576.0);
}

/* n bytes at `off`, never past the image's end. */
static int rd(xdvd_t *x, uint64_t off, void *buf, uint32_t n, char *err, size_t cap)
{
    uint32_t done = 0;

    if (off > x->size || n > x->size - off) {
        char a[32], b[32];
        mb(x->size, a, sizeof(a));
        mb(off + n, b, sizeof(b));
        snprintf(err, cap, "the disc image is cut short (truncated): it ends at %s, and the disc's data goes on to "
                 "at least %s", a, b);
        return -1;
    }
    while (done < n) {
        OVERLAPPED ov;
        DWORD got = 0;
        uint64_t at = off + done;
        memset(&ov, 0, sizeof(ov));
        ov.Offset = (DWORD)at;
        ov.OffsetHigh = (DWORD)(at >> 32);
        if (!ReadFile(x->h, (char *)buf + done, n - done, &got, &ov)) {
            DWORD e = GetLastError();
            snprintf(err, cap, "the disc image couldn't be read at %.1f MB (Windows error %lu%s)", (double)at / 1048576.0,
                     (unsigned long)e, e == ERROR_CRC ? ": a damaged disk or file" : e == ERROR_HANDLE_EOF ?
                     ": it ends there" : "");
            return -1;
        }
        if (!got) {
            snprintf(err, cap, "the disc image ended early at %.1f MB while reading it", (double)at / 1048576.0);
            return -1;
        }
        done += got;
    }
    return 0;
}

/* A name Windows can hold as a file or folder name ("" if fine, else why not). */
static const char *bad_name(const unsigned char *s, unsigned n)
{
    static const char *const dev[] = { "CON", "PRN", "AUX", "NUL" };
    char base[8];
    unsigned i, bl;

    if (n == 0)
        return "an empty name";
    if ((n == 1 && s[0] == '.') || (n == 2 && s[0] == '.' && s[1] == '.'))
        return "a name of dots";
    for (i = 0; i < n; i++)
        if (s[i] < 0x20 || s[i] == 0x7F || strchr("\\/:*?\"<>|", s[i]))
            return "a character Windows doesn't allow in names";
    if (s[n - 1] == ' ' || s[n - 1] == '.')
        return "a name ending in a space or dot";
    for (bl = 0; bl < n && bl < 5 && s[bl] != '.'; bl++)
        base[bl] = (char)s[bl];
    base[bl] = 0;
    if (bl < n && s[bl] != '.')
        return "";
    for (i = 0; i < 4; i++)
        if (_stricmp(base, dev[i]) == 0)
            return "a name Windows keeps for devices";
    if (bl == 4 && (_strnicmp(base, "COM", 3) == 0 || _strnicmp(base, "LPT", 3) == 0) && base[3] >= '1' && base[3] <= '9')
        return "a name Windows keeps for devices";
    return "";
}

/* The name's bytes as UTF-8 (bytes above 0x7F as Windows-1252). */
static int name_utf8(const unsigned char *s, unsigned n, char *out, size_t cap)
{
    wchar_t w[256];
    int k = MultiByteToWideChar(1252, 0, (const char *)s, (int)n, w, 256);
    int u = k > 0 ? WideCharToMultiByte(CP_UTF8, 0, w, k, out, (int)cap - 1, NULL, NULL) : 0;
    if (u <= 0)
        return -1;
    out[u] = 0;
    return 0;
}

static int add(xdvd_t *x, const char *path, uint32_t sector, uint32_t size, int dir, char *err, size_t cap)
{
    xdvd_entry_t *e;

    if (x->n >= XDVD_MAX_ENTRIES) {
        snprintf(err, cap, "the disc image's folders list more than %u entries: it is damaged", XDVD_MAX_ENTRIES);
        return -1;
    }
    if (x->n == x->cap) {
        size_t nc = x->cap ? x->cap * 2 : 4096;
        xdvd_entry_t *ne = (xdvd_entry_t *)realloc(x->e, nc * sizeof(*ne));
        if (!ne) {
            snprintf(err, cap, "out of memory reading the disc image's folders");
            return -1;
        }
        x->e = ne;
        x->cap = nc;
    }
    e = &x->e[x->n];
    e->path = _strdup(path);
    if (!e->path) {
        snprintf(err, cap, "out of memory reading the disc image's folders");
        return -1;
    }
    e->sector = sector;
    e->size = size;
    e->dir = dir;
    e->offset = x->base + (uint64_t)sector * XDVD_SECTOR;
    x->n++;
    if (!dir) {
        x->files++;
        x->bytes += size;
    }
    return 0;
}

static int cmp_name(const void *a, const void *b)
{
    return _stricmp(*(const char *const *)a, *(const char *const *)b);
}

static int walk(xdvd_t *x, uint32_t sector, uint32_t size, const char *prefix, int depth, char *err, size_t cap)
{
    unsigned char *buf = NULL, *seen = NULL;
    uint32_t *stack = NULL, sp = 0, slots;
    const char **names = NULL;
    size_t nn = 0, ncap = 0, i;
    uint64_t off = x->base + (uint64_t)sector * XDVD_SECTOR;
    int r = -1;
    struct { uint32_t sector, size; char path[XDVD_MAX_PATH]; } *subs = NULL;
    size_t nsub = 0, subcap = 0;

    if (size == 0)
        return 0;                       /* an empty folder */
    if (depth > XDVD_MAX_DEPTH) {
        snprintf(err, cap, "the disc image's folders go more than %d levels deep: it is damaged", XDVD_MAX_DEPTH);
        return -1;
    }
    if (size > XDVD_MAX_TABLE) {
        snprintf(err, cap, "a folder table on the disc image (%s) claims %u bytes: it is damaged",
                 prefix[0] ? prefix : "the top folder", size);
        return -1;
    }
    if (off > x->size || size > x->size - off) {
        char a[32];
        mb(x->size, a, sizeof(a));
        snprintf(err, cap, "the disc image is cut short (truncated): it ends at %s, before the folder %s", a,
                 prefix[0] ? prefix : "at the top");
        return -1;
    }
    slots = size / 4;
    buf = (unsigned char *)malloc(size);
    seen = (unsigned char *)calloc(slots + 1, 1);
    stack = (uint32_t *)malloc((slots + 1) * sizeof(uint32_t));
    if (!buf || !seen || !stack) {
        snprintf(err, cap, "out of memory reading the disc image's folders");
        goto done;
    }
    if (rd(x, off, buf, size, err, cap))
        goto done;
    stack[sp++] = 0;
    while (sp) {
        uint32_t o = stack[--sp], p = o * 4, s, sz;
        uint16_t left, right;
        unsigned nl, attr;
        const char *why;
        char nm[600], full[XDVD_MAX_PATH + 600];

        if (p + 14 > size) {
            snprintf(err, cap, "a folder table on the disc image (%s) points outside itself: it is damaged",
                     prefix[0] ? prefix : "the top folder");
            goto done;
        }
        if (seen[o]) {
            snprintf(err, cap, "a folder table on the disc image (%s) loops back on itself: it is damaged",
                     prefix[0] ? prefix : "the top folder");
            goto done;
        }
        seen[o] = 1;
        memcpy(&left, buf + p, 2);
        memcpy(&right, buf + p + 2, 2);
        if (left == 0xFFFF) {
            if (o == 0 && nn == 0)
                break;                  /* a folder with nothing in it (one sector of padding) */
            snprintf(err, cap, "a folder table on the disc image (%s) points at padding: it is damaged",
                     prefix[0] ? prefix : "the top folder");
            goto done;
        }
        memcpy(&s, buf + p + 4, 4);
        memcpy(&sz, buf + p + 8, 4);
        attr = buf[p + 12];
        nl = buf[p + 13];
        if (p + 14 + nl > size) {
            snprintf(err, cap, "a name in a folder table on the disc image (%s) runs past its end: it is damaged",
                     prefix[0] ? prefix : "the top folder");
            goto done;
        }
        why = bad_name(buf + p + 14, nl);
        if (*why || name_utf8(buf + p + 14, nl, nm, sizeof(nm))) {
            snprintf(err, cap, "a name on the disc image (in %s) can't be used on Windows (%s): it is damaged or not "
                     "an Xbox game disc", prefix[0] ? prefix : "the top folder", *why ? why : "unreadable");
            goto done;
        }
        if (strlen(prefix) + 1 + strlen(nm) >= XDVD_MAX_PATH) {
            snprintf(err, cap, "a path on the disc image is longer than %d characters: it is damaged", XDVD_MAX_PATH);
            goto done;
        }
        snprintf(full, sizeof(full), "%s%s%s", prefix, prefix[0] ? "/" : "", nm);
        if (nn == ncap) {
            const char **nw = (const char **)realloc(names, (ncap = ncap ? ncap * 2 : 64) * sizeof(*nw));
            if (!nw) {
                snprintf(err, cap, "out of memory reading the disc image's folders");
                goto done;
            }
            names = nw;
        }
        if (attr & 0x10) {              /* FILE_ATTRIBUTE_DIRECTORY */
            if (add(x, full, s, sz, 1, err, cap))
                goto done;
            if (nsub == subcap) {
                void *ns = realloc(subs, (subcap = subcap ? subcap * 2 : 16) * sizeof(*subs));
                if (!ns) {
                    snprintf(err, cap, "out of memory reading the disc image's folders");
                    goto done;
                }
                subs = ns;
            }
            subs[nsub].sector = s;
            subs[nsub].size = sz;
            snprintf(subs[nsub].path, sizeof(subs[nsub].path), "%s", full);
            nsub++;
        } else {
            uint64_t fo = x->base + (uint64_t)s * XDVD_SECTOR;
            if (fo > x->size || sz > x->size - fo) {
                char a[32];
                mb(x->size, a, sizeof(a));
                snprintf(err, cap, "the disc image is cut short (truncated): it ends at %s, before the end of the file "
                         "%s (%.1f MB on)", a, full, (double)(fo + sz) / 1048576.0);
                goto done;
            }
            if (add(x, full, s, sz, 0, err, cap))
                goto done;
        }
        names[nn++] = x->e[x->n - 1].path + (prefix[0] ? strlen(prefix) + 1 : 0);
        if (left) {
            if ((uint32_t)left >= slots) {
                snprintf(err, cap, "a folder table on the disc image (%s) points outside itself: it is damaged",
                         prefix[0] ? prefix : "the top folder");
                goto done;
            }
            stack[sp++] = left;
        }
        if (right) {
            if ((uint32_t)right >= slots) {
                snprintf(err, cap, "a folder table on the disc image (%s) points outside itself: it is damaged",
                         prefix[0] ? prefix : "the top folder");
                goto done;
            }
            stack[sp++] = right;
        }
        if (sp > slots) {
            snprintf(err, cap, "a folder table on the disc image (%s) is damaged", prefix[0] ? prefix : "the top folder");
            goto done;
        }
    }
    qsort(names, nn, sizeof(*names), cmp_name);
    for (i = 1; i < nn; i++)
        if (_stricmp(names[i - 1], names[i]) == 0) {
            snprintf(err, cap, "the disc image has two entries named \"%s\" in %s: it is damaged", names[i],
                     prefix[0] ? prefix : "the top folder");
            goto done;
        }
    for (i = 0; i < nsub; i++)
        if (walk(x, subs[i].sector, subs[i].size, subs[i].path, depth + 1, err, cap))
            goto done;
    r = 0;
done:
    free(buf);
    free(seen);
    free(stack);
    free((void *)names);
    free(subs);
    return r;
}

static const xdvd_t *g_sort_x;

static int cmp_idx(const void *a, const void *b)
{
    return _stricmp(g_sort_x->e[*(const size_t *)a].path, g_sort_x->e[*(const size_t *)b].path);
}

int xdvd_open(const wchar_t *path, xdvd_t **out, char *err, size_t cap)
{
    xdvd_t *x;
    LARGE_INTEGER sz;
    unsigned char vd[XDVD_SECTOR];
    uint32_t root_sector = 0, root_size = 0;
    size_t i;
    DWORD at;

    *out = NULL;
    at = GetFileAttributesW(path);
    if (at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY)) {
        snprintf(err, cap, "that is a folder, not a disc image file");
        return -1;
    }
    x = (xdvd_t *)calloc(1, sizeof(*x));
    if (!x) {
        snprintf(err, cap, "out of memory");
        return -1;
    }
    /* Read-only, with read sharing: another program may read it meanwhile, nothing may write it. */
    x->h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (x->h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        snprintf(err, cap, "the file couldn't be opened: %s (Windows error %lu)",
                 e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? "it isn't there" :
                 e == ERROR_ACCESS_DENIED ? "access is denied" :
                 e == ERROR_SHARING_VIOLATION ? "another program has it open for writing (a download or a copy still "
                 "under way?)" : "Windows couldn't open it", (unsigned long)e);
        free(x);
        return -1;
    }
    if (!GetFileSizeEx(x->h, &sz)) {
        snprintf(err, cap, "the file's size couldn't be read (Windows error %lu)", (unsigned long)GetLastError());
        xdvd_close(x);
        return -1;
    }
    x->size = (uint64_t)sz.QuadPart;
    for (i = 0; i < sizeof(k_layouts) / sizeof(k_layouts[0]); i++) {
        uint64_t o = k_layouts[i].base + 32ull * XDVD_SECTOR;
        if (o + XDVD_SECTOR > x->size)
            continue;
        if (rd(x, o, vd, XDVD_SECTOR, err, cap))
            continue;
        if (memcmp(vd, k_magic, 20) == 0) {
            x->base = k_layouts[i].base;
            x->layout = k_layouts[i].name;
            break;
        }
    }
    if (!x->layout) {
        unsigned char iso[6];
        if (x->size < 34ull * XDVD_SECTOR)
            snprintf(err, cap, "this isn't an Xbox disc image: the file is only %llu bytes (an Xbox game image is "
                     "hundreds of MB)", (unsigned long long)x->size);
        else if (x->size > 0x8006 && rd(x, 0x8001, iso, 5, err, cap) == 0 && memcmp(iso, "CD001", 5) == 0)
            snprintf(err, cap, "this is an ordinary ISO 9660 disc image (a PC, PlayStation or other disc), not an "
                     "Xbox disc image: no Xbox file system (XDVDFS) on it");
        else
            snprintf(err, cap, "this isn't an Xbox disc image: no Xbox file system (XDVDFS) at any of the places "
                     "one starts (offset 0 for a trimmed XISO, 0x18300000 for a full dump)%s",
                     x->size < 0x18300000ull ? "" : "");
        xdvd_close(x);
        return -1;
    }
    if (memcmp(vd + 0x7EC, k_magic, 20) != 0) {
        snprintf(err, cap, "the disc image's volume header is damaged (its closing mark is missing)");
        xdvd_close(x);
        return -1;
    }
    memcpy(&root_sector, vd + 20, 4);
    memcpy(&root_size, vd + 24, 4);
    if (walk(x, root_sector, root_size, "", 0, err, cap)) {
        xdvd_close(x);
        return -1;
    }
    if (!x->files) {
        snprintf(err, cap, "the disc image's file system is empty");
        xdvd_close(x);
        return -1;
    }
    x->sorted = (size_t *)malloc(x->n * sizeof(size_t));
    if (!x->sorted) {
        snprintf(err, cap, "out of memory");
        xdvd_close(x);
        return -1;
    }
    for (i = 0; i < x->n; i++)
        x->sorted[i] = i;
    g_sort_x = x;                       /* qsort has no context argument; opened on one thread at a time */
    qsort(x->sorted, x->n, sizeof(size_t), cmp_idx);
    *out = x;
    return 0;
}

void xdvd_close(xdvd_t *x)
{
    size_t i;

    if (!x)
        return;
    if (x->h && x->h != INVALID_HANDLE_VALUE)
        CloseHandle(x->h);
    for (i = 0; i < x->n; i++)
        free(x->e[i].path);
    free(x->e);
    free(x->sorted);
    free(x);
}

const xdvd_entry_t *xdvd_entries(const xdvd_t *x, size_t *n)
{
    *n = x->n;
    return x->e;
}

void xdvd_totals(const xdvd_t *x, size_t *files, uint64_t *bytes)
{
    *files = x->files;
    *bytes = x->bytes;
}

const char *xdvd_layout(const xdvd_t *x)
{
    return x->layout;
}

uint64_t xdvd_image_size(const xdvd_t *x)
{
    return x->size;
}

const xdvd_entry_t *xdvd_find(const xdvd_t *x, const char *path)
{
    char p[XDVD_MAX_PATH + 1];
    size_t lo = 0, hi = x->n, i;

    while (*path == '/' || *path == '\\')
        path++;
    if (strlen(path) >= sizeof(p))
        return NULL;
    for (i = 0; path[i]; i++)
        p[i] = path[i] == '\\' ? '/' : path[i];
    p[i] = 0;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = _stricmp(x->e[x->sorted[mid]].path, p);
        if (c == 0)
            return &x->e[x->sorted[mid]];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

int xdvd_read(xdvd_t *x, const xdvd_entry_t *e, uint64_t at, void *buf, uint32_t n, char *err, size_t cap)
{
    if (e->dir || at > e->size || n > e->size - at) {
        snprintf(err, cap, "a read past the end of %s (a bug)", e->path);
        return -1;
    }
    return rd(x, e->offset + at, buf, n, err, cap);
}
