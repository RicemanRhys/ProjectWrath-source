/**
 * An Xbox disc image reader (step 107, notes 18.110): XDVDFS, the Xbox's
 * own disc file system, read from a .iso file for the first-run setup
 * (setup.c). Read-only: the image is opened for reading with read sharing
 * and nothing is ever written next to it.
 *
 * Layouts (the volume descriptor is looked for at each):
 *   - a trimmed XISO ("extract-xiso" style): the game partition at offset 0;
 *   - a full ("Redump-style") dump of an XGD1 disc: a video partition first,
 *     the game partition at 0x18300000; XGD2 at 0x0FD90000, XGD3 at
 *     0x02080000 (not this game's discs, but harmless to look for).
 *
 * XDVDFS: sector 32 of the partition holds "MICROSOFT*XBOX*MEDIA", the root
 * directory's sector and size, a time stamp and the magic again at its end.
 * A directory is a table of entries (left and right offsets in 4-byte
 * units, start sector, size, attributes, name length, name) forming a
 * binary (AVL) tree; 0xFF bytes pad a sector's end. Files are contiguous
 * runs of 2048-byte sectors.
 *
 * Everything read is checked: the volume descriptor's two magics, every
 * directory and file inside the image (a truncated image is named, with the
 * first file it cuts), offsets inside their tables, no entry visited twice
 * (a loop), names Windows can hold, no name twice in one folder, at most 64
 * folder levels and a million entries. Names are bytes; those above 0x7F are
 * read as Windows-1252 and kept as UTF-8.
 */
#ifndef TWOC_XDVDFS_H
#define TWOC_XDVDFS_H

#include <stdint.h>
#include <stddef.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XDVD_SECTOR 2048u

typedef struct {
    char *path;                 /* relative, '/' between names, UTF-8 */
    uint64_t offset;            /* in the image file */
    uint32_t size;
    uint32_t sector;            /* the partition's */
    int dir;                    /* 1: a folder */
} xdvd_entry_t;

typedef struct xdvd xdvd_t;

/* Opens and reads the whole directory tree. Returns 0 and *out, or -1 with
 * `err` saying what is wrong in plain words (not an Xbox image, truncated,
 * damaged, can't be opened and why). */
int xdvd_open(const wchar_t *path, xdvd_t **out, char *err, size_t cap);
void xdvd_close(xdvd_t *x);

/* The entries (files and folders), in tree order; `files` and `bytes` count
 * the files. */
const xdvd_entry_t *xdvd_entries(const xdvd_t *x, size_t *n);
void xdvd_totals(const xdvd_t *x, size_t *files, uint64_t *bytes);
/* "trimmed XISO (partition at offset 0)" and the like, and the image's size. */
const char *xdvd_layout(const xdvd_t *x);
uint64_t xdvd_image_size(const xdvd_t *x);
/* A file or folder by path ('/' or '\\', any case); NULL if not there. */
const xdvd_entry_t *xdvd_find(const xdvd_t *x, const char *path);
/* `n` bytes of an entry's data from `at`; never past the entry or the image.
 * Returns 0, or -1 with `err`. */
int xdvd_read(xdvd_t *x, const xdvd_entry_t *e, uint64_t at, void *buf, uint32_t n, char *err, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
