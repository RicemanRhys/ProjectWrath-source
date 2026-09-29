/* log_block.h: a block of diagnostic lines written to stderr in one write (notes 18.47).
 *
 * stderr is unbuffered (main.c), and the CRT buffers an unbuffered stream for
 * the length of one fprintf only when it is a console. Into a log file every
 * byte is its own WriteFile, so a periodic block of ~1.6 KB (the 2 s stat
 * lines) or ~2.6 KB (the 10 s [D3DB] lines) was thousands of system calls on
 * whichever game thread got there. A block is formatted here and handed to
 * _write once, under the stream's lock: the same bytes (text mode still turns
 * \n into \r\n), one call. A line that doesn't fit goes out through stdio as
 * before, after what is already buffered.
 */
#ifndef TWOC_LOG_BLOCK_H
#define TWOC_LOG_BLOCK_H

#include <io.h>
#include <stdarg.h>
#include <stdio.h>

typedef struct {
    int len;
    char buf[8192];
} log_block_t;

static void log_block_flush(log_block_t *b)
{
    if (b->len > 0) {
        _lock_file(stderr);
        fflush(stderr);
        _write(_fileno(stderr), b->buf, (unsigned)b->len);
        _unlock_file(stderr);
    }
    b->len = 0;
}

static void log_block_printf(log_block_t *b, const char *fmt, ...)
{
    int room = (int)sizeof b->buf - b->len, n;
    va_list ap;

    va_start(ap, fmt);
    n = vsnprintf(b->buf + b->len, (size_t)room, fmt, ap);
    va_end(ap);
    if (n >= 0 && n < room) {
        b->len += n;
        return;
    }
    log_block_flush(b);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

#endif
