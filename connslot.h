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

#define SLOTS_LISTEN 2
typedef struct slots {
    int nr_slots;
    int nr_open;
    int listen[SLOTS_LISTEN];
    int timeout;
    conn_t conn[];
} slots_t;

void conn_zero(conn_t *);
int conn_init(conn_t *);
void conn_read(conn_t *);
ssize_t conn_write(conn_t *);
int conn_iswriter(conn_t *);
void conn_close(conn_t *);

slots_t *slots_malloc(int nr_slots);
int slots_listen_tcp(slots_t *, int);
int slots_fdset(slots_t *, fd_set *, fd_set *);
int slots_accept(slots_t *, int);
int slots_closeidle(slots_t *);
int slots_fdset_loop(slots_t *, fd_set *, fd_set *);
#endif
