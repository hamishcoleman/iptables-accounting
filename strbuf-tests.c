/*
 * Tests for the string buffer abstraction
 *
 * Copyright (C) 2023 Hamish Coleman
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "strbuf.h"

// Tests are silent and return if everything is OK, or abort if issues
void strbuf_tests() {
    strbuf_t *p = sb_malloc(10);
    assert(p);
    assert(p->wr_pos==0);
    assert(p->capacity==10);
    assert(p->capacity_max==10);

    int r;
    r = sb_append(p, "abcd", 4);
    assert(r==4);
    assert(!memcmp(p->str, "abcd", 4));

    r = sb_append(p, "efgh", 4);
    assert(r==8);
    assert(!memcmp(p->str, "abcdefgh", 8));

    r = sb_append(p, "abcd", 4);
    assert(r==-1);
    assert(p->wr_pos==8);

    strbuf_t *p2 = sb_realloc(p, 5);
    assert(p2);
    p = p2;
    assert(p->wr_pos==5);
    assert(p->str[5]==0);
    assert(!memcmp(p->str, "abcde", 5));

    p2 = sb_reappend(p, "abcde", 5);
    assert(p2);
    p = p2;
    assert(p->wr_pos==10);
    assert(p->str[10]==0);
    assert(!memcmp(p->str, "abcdeabcd", 9));

    sb_zero(p);
    assert(p->wr_pos==0);
    assert(p->capacity==10);

    r = sb_printf(p, "0x%02x", 10);
    assert(r==4);
    assert(r==(int)p->wr_pos);
    assert(!strncmp(p->str, "0x0a", 5));

    r = sb_printf(p, "0x%02x", 20);
    assert(r==8);
    assert(r==(int)p->wr_pos);
    assert(!strncmp(p->str, "0x0a0x14", 9));

    // This will fail to append the entire string
    r = sb_printf(p, "0x%02x", 40);
    assert(r==12);
    assert(p->wr_pos==8);
    assert(!strncmp(p->str, "0x0a0x140", 10));

    // This print just fits into the buffer
    r = sb_printf(p, "Z");
    assert(r==9);
    assert(r==(int)p->wr_pos);
    assert(!strncmp(p->str, "0x0a0x14Z", 10));

    // The max capacity is still set to the size at malloc
    strbuf_t *np = sb_reprintf(p, "%05i", 1024);
    assert(!np);
    assert(p->wr_pos==9);

    p->capacity_max = 1000;
    p = sb_reprintf(p, "%05i", 1024);
    assert(p);
    assert(p->wr_pos==14);
    assert(p->capacity==15);
    assert(!strncmp(p->str, "0x0a0x14Z01024", 15));

    free(p);
}

int main() {
    printf("Running strbuf tests\n");

    // Many sizes are acceptable, so this is informational only
    printf("sizeof(strbuf_t) = %li\n", sizeof(strbuf_t));

    strbuf_tests();
}
