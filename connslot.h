/*
 *
 * Copyright (C) 2023 Hamish Coleman
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef HTTPD_H
#define HTTPD_H 1

#include "strbuf.h"

enum conn_state {
    EMPTY,
    READING,
    READY,
    SENDING,
};

typedef struct conn {
    int fd;
    enum conn_state state;
    strbuf_t *request;      // Request from remote
    strbuf_t *reply_header; // not shared reply data
    strbuf_t *reply;        // shared reply data (const struct)
    unsigned int reply_sendpos;
    time_t activity;        // timestamp of last txn
} conn_t;

void conn_zero(conn_t *);
int conn_init(conn_t *);
void conn_read(conn_t *);
ssize_t conn_write(conn_t *);
int conn_iswriter(conn_t *);
void conn_close(conn_t *);

int httpdslots_init(conn_t *, size_t);
int httpdslots_fdset(conn_t *, size_t, fd_set *, fd_set *);
int httpdslots_accept(conn_t *, size_t, int);
int httpdslots_closeidle(conn_t *, size_t);
#endif
