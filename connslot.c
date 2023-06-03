/*
 *
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include "connslot.h"

void conn_zero(conn_t *conn) {
    conn->fd = -1;
    conn->state = EMPTY;
    conn->reply = NULL;
    conn->reply_sendpos = 0;
    conn->activity = 0;

    if (conn->request) {
        sb_zero(conn->request);
    }
    if (conn->reply_header) {
        sb_zero(conn->reply_header);
    }
}

int conn_init(conn_t *conn) {
    // TODO: make capacity flexible
    conn->request = sb_malloc(48);
    conn->request->capacity_max = 1000;
    conn->reply_header = sb_malloc(48);
    conn->reply_header->capacity_max = 1000;

    conn_zero(conn);

    if (!conn->request || !conn->reply_header) {
        return -1;
    }
    return 0;
}

void conn_read(conn_t *conn) {
    conn->state = READING;

    // If no space available, try increasing our capacity
    if (!sb_avail(conn->request)) {
        strbuf_t *new = sb_realloc(conn->request, conn->request->capacity + 16);
        if (!new) {
            abort(); // FIXME: do something smarter?
        }
        conn->request = new;
    }

    ssize_t size = sb_read(conn->fd, conn->request);

    if (size == 0) {
        // TODO: confirm what other times we can get zero on a ready fd
        conn->state = EMPTY;
        return;
    }

    conn->activity = time(NULL);

    if (conn->request->wr_pos<4) {
        // Not enough bytes to match the memmem()
        return;
    }

    void *p = memmem(conn->request->str, conn->request->wr_pos, "\r\n\r\n", 4);
    if (p) {
        conn->state = READY;
    }
}

ssize_t conn_write(conn_t *conn) {
    ssize_t sent;
    int nr = 0;
    unsigned int reply_pos = 0;
    unsigned int end_pos = 0;

    conn->state = SENDING;

    struct iovec vecs[2];

    if (conn->reply_header) {
        end_pos += conn->reply_header->wr_pos;
        if (conn->reply_sendpos < conn->reply_header->wr_pos) {
            size_t size = conn->reply_header->wr_pos - conn->reply_sendpos;
            vecs[nr].iov_base = &conn->reply_header->str[conn->reply_sendpos];
            vecs[nr].iov_len = size;
            nr++;
        } else {
            reply_pos = conn->reply_sendpos - conn->reply_header->wr_pos;
        }
    }

    if (conn->reply) {
        end_pos += conn->reply->wr_pos;
        vecs[nr].iov_base = &conn->reply->str[reply_pos];
        vecs[nr].iov_len = conn->reply->wr_pos - reply_pos;
        nr++;
    }

    sent = writev(conn->fd, &vecs[0], nr);

#if NOVEC
    if (conn->reply_sendpos < conn->reply_header->wr_pos) {
        sent = sb_write(
            conn->fd,
            conn->reply_header,
            conn->reply_sendpos,
            -1
            );
    } else {
        sent = sb_write(
            conn->fd,
            conn->reply,
            conn->reply_sendpos - conn->reply_header->wr_pos,
            -1
            );
    }
    unsigned int end_pos = conn->reply_header->wr_pos + conn->reply->wr_pos;
#endif

    conn->reply_sendpos += sent;

    if (conn->reply_sendpos >= end_pos) {
        // We have sent the last bytes of this reply
        conn->state = EMPTY;
        conn->reply_sendpos = 0;
        sb_zero(conn->reply_header);
        sb_zero(conn->request);
    }

    conn->activity = time(NULL);
    return sent;
}

int conn_iswriter(conn_t *conn) {
    switch (conn->state) {
        case SENDING:
            return 1;
        default:
            return 0;
    }
}

void conn_close(conn_t *conn) {
    close(conn->fd);
    conn_zero(conn);
}

int httpdslots_init(conn_t *slot, size_t size) {
    int i;
    int nr_slots = size / sizeof(conn_t);

    int r = 0;
    for (i=0; i < nr_slots; i++) {
        r += conn_init(&slot[i]);
    }

    return r;
}

int httpdslots_fdset(conn_t *slot, size_t size, fd_set *readers, fd_set *writers) {
    int i;
    int nr_slots = size / sizeof(conn_t);
    int fdmax = 0;

    for (i=0; i<nr_slots; i++) {
        if (slot[i].fd == -1) {
            continue;
        }
        FD_SET(slot[i].fd, readers);
        if (conn_iswriter(&slot[i])) {
            FD_SET(slot[i].fd, writers);
        }
        fdmax = (slot[i].fd > fdmax)? slot[i].fd : fdmax;
    }
    return fdmax;
}

int httpdslots_accept(conn_t *slot, size_t size, int server) {
    int i;
    int nr_slots = size / sizeof(conn_t);

    // TODO: remember previous checked slot and dont start at zero
    for (i=0; i<nr_slots; i++) {
        if (slot[i].fd == -1) {
            break;
        }
    }

    if (i == nr_slots) {
        // No room, inform the caller
        return -2;
    }

    int client = accept(server, NULL, 0);
    if (client == -1) {
        return -1;
    }

    fcntl(client, F_SETFL, O_NONBLOCK);

    slot[i].activity = time(NULL);
    slot[i].fd = client;
    return i;
}

int httpdslots_closeidle(conn_t *slot, size_t size) {
    int i;
    int nr_slots = size / sizeof(conn_t);
    int nr_closed = 0;
    int timeout = 60; // seconds
    int min_activity = time(NULL) - timeout;

    for (i=0; i<nr_slots; i++) {
        if (slot[i].fd == -1) {
            continue;
        }
        if (slot[i].activity < min_activity) {
            conn_close(&slot[i]);
            nr_closed++;
        }
    }
    return nr_closed;
}
