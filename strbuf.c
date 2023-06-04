/*
 * Implentation for the string buffer abstraction
 *
 * Copyright (C) 2023 Hamish Coleman
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "strbuf.h"

void sb_zero(strbuf_t *p) {
    p->wr_pos = 0;
    p->str[0] = 0;
}

strbuf_t *sb_malloc(size_t size) {
    size_t headersize = sizeof(strbuf_t);
    strbuf_t *p = malloc(headersize+size);
    if (p) {
        p->capacity = size;
        p->capacity_max = size;
        sb_zero(p);
    }
    return p;
}

strbuf_t *sb_realloc(strbuf_t *p, size_t size) {
    size_t headersize = sizeof(strbuf_t);
    if (size > p->capacity_max) {
        return NULL;
    }

    p = realloc(p, headersize + size);
    if (p) {
        p->capacity = size;
        if (p->wr_pos > p->capacity) {
            p->wr_pos = p->capacity;
            p->str[p->wr_pos] = 0;
        }
    }
    return p;
}

size_t sb_len(strbuf_t *p) {
    return p->wr_pos;
}

size_t sb_avail(strbuf_t *p) {
    return p->capacity - p->wr_pos;
}

size_t sb_append(strbuf_t *p, void *buf, size_t bufsize) {
    if ((p->capacity - p->wr_pos) < bufsize) {
        return -1;
    }
    memcpy(&p->str[p->wr_pos], buf, bufsize);
    p->wr_pos += bufsize;
    return p->wr_pos;
}

strbuf_t *sb_reappend(strbuf_t *p, void *buf, size_t bufsize) {
    size_t needed = p->wr_pos + bufsize;
    if (needed > p->capacity) {
        p = sb_realloc(p, needed);
        if (!p) {
            return NULL;
        }
    }
    sb_append(p, buf, bufsize);
    return p;
}

size_t sb_vprintf(strbuf_t *p, const char *format, va_list ap) {
    size_t avail = sb_avail(p);

    size_t size = vsnprintf(&p->str[p->wr_pos], avail, format, ap);

    if (size < avail) {
        // The new data fit in the buffer
        p->wr_pos += size;

        return sb_len(p);
    }

    // Return the size the buffer would need to be
    return p->wr_pos + size;
}

size_t sb_printf(strbuf_t *p, const char *format, ...) {
    va_list ap;

    va_start(ap, format);
    return sb_vprintf(p, format, ap);
    va_end(ap);
}

strbuf_t *sb_reprintf(strbuf_t *p, const char *format, ...) {
    va_list ap;
    size_t size;

    if (!p) {
        return NULL;
    }

    int loop = 0;
    while(1) {

        va_start(ap, format);
        size = sb_vprintf(p, format, ap);
        va_end(ap);

        if (size < p->capacity) {
            // The new data fit in the buffer
            return p;
        }

        if (loop) {
            // Something went wrong, we already passed through the realloc
            // and still dont have enough buffer space for the printf
            return NULL;
        }

        p = sb_realloc(p, size + 1);
        if (!p) {
            return NULL;
        }
        loop = 1;
    }
}

ssize_t sb_read(int fd, strbuf_t *p) {
    ssize_t size;

    size = read(fd, &p->str[p->wr_pos], sb_avail(p));
    if (size != -1) {
        p->wr_pos += size;
    }
    return size;
}

ssize_t sb_write(int fd, strbuf_t *p, int index, ssize_t size) {
    if (size == -1) {
        size = p->wr_pos - index;
    }

    return write(fd, &p->str[index], size);
}

void sb_dump(strbuf_t *p) {
    if (!p) {
        printf("NULL\n");
        return;
    }
    printf("%p(%u) (%u) = '%s' ", p, p->capacity, p->wr_pos, p->str);
    for (int i=0; i<16; i++) {
        printf("%02x ",p->str[i]);
    }
    printf("\n");
}
