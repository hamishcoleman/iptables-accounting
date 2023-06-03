/*
 * Internal interface definitions for the strbuf abstraction
 *
 * Copyright (C) 2023 Hamish Coleman
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef STRBUF_H
#define STRBUF_H 1

#include <stdarg.h>

typedef struct strbuf {
    unsigned int capacity;
    unsigned int capacity_max;  // When auto reallocing, what is the largest
    unsigned int wr_pos;        // str[] append position (arriving data)
    unsigned int rd_pos;        // str[] read position (processing data)
    char str[];
} strbuf_t;

// Initialise the strbuf pointer buf to point at the storage area p
// p must be a known sized object
#define STRBUF_INIT(buf,p) do { \
        buf = (void *)p; \
        buf->capacity = sizeof(p) - sizeof(strbuf_t); \
        buf->capacity_max = buf->capacity; \
        buf->wr_pos = 0; \
} while(0)

void sb_zero(strbuf_t *);
strbuf_t *sb_malloc(size_t) __attribute__ ((malloc));
strbuf_t *sb_realloc(strbuf_t *, size_t);
size_t sb_len(strbuf_t *);
size_t sb_avail(strbuf_t *);
size_t sb_append(strbuf_t *, void *, size_t);
size_t sb_vprintf(strbuf_t *, const char *, va_list);
size_t sb_printf(strbuf_t *, const char *, ...)
__attribute__ ((format (printf, 2, 3)));
strbuf_t *sb_reprintf(strbuf_t *, const char *, ...)
__attribute__ ((format (printf, 2, 3)));
ssize_t sb_read(int, strbuf_t *);
ssize_t sb_write(int, strbuf_t *, int, ssize_t);
void sb_dump(strbuf_t *);

#endif
