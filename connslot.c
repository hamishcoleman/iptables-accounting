/*
 *
 */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <time.h>
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

    // case protocol==HTTP

    if (conn->request->wr_pos<4) {
        // Not enough bytes to match the end of header check
        return;
    }

    // retrieve the cached expected length, if any
    unsigned int expected_length = conn->request->rd_pos;

    if (expected_length == 0) {
        char *p = memmem(conn->request->str, conn->request->wr_pos, "\r\n\r\n", 4);
        if (!p) {
            // As yet, we dont have an entire header
            return;
        }

        int body_pos = p - conn->request->str + 4;

        // Determine if we need to read a body
        p = memmem(
                conn->request->str,
                conn->request->wr_pos,
                "Content-Length:",
                15
        );

        if (!p) {
            // We have an end of header, and the header has no content length field
            // so assume there is no body to read
            conn->state = READY;
            return;
        }

        p+=15; // Skip the field name
        unsigned int content_length = strtoul(p, NULL, 10);
        expected_length = body_pos + content_length;
    }

    // By this point we must have an expected_length

    // cache the calcaulated total length in the conn
    conn->request->rd_pos = expected_length;

    if (conn->request->wr_pos < expected_length) {
        // Dont have enough length
        return;
    }

    // Do have enough length
    conn->state = READY;
    conn->request->rd_pos = 0;
    return;
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

slots_t *slots_malloc(int nr_slots) {
    size_t bytes = sizeof(slots_t) + nr_slots * sizeof(conn_t);
    slots_t *slots = malloc(bytes);
    if (!slots) {
        return NULL;
    }

    slots->nr_slots = nr_slots;

    // Set any defaults
    slots->timeout = 60;

    for (int i=0; i < SLOTS_LISTEN; i++) {
        slots->listen[i] = -1;
    }

    int r = 0;
    for (int i=0; i < nr_slots; i++) {
        r += conn_init(&slots->conn[i]);
    }

    if (r!=0) {
        // TODO: free all the successful strbuf mallocs
        abort();
    }
    return slots;
}

int slots_listen_tcp(slots_t *slots, int port) {
    int listen_nr;
    for (listen_nr=0; listen_nr < SLOTS_LISTEN; listen_nr++) {
        if (slots->listen[listen_nr] == -1) {
            break;
        }
    }
    if (listen_nr == SLOTS_LISTEN) {
        // All listen slots full
        return -2;
    }

    int server;
    int on = 1;
    int off = 0;
    struct sockaddr_in6 addr = {
        .sin6_family = AF_INET6,
        .sin6_port = htons(port),
        .sin6_addr = IN6ADDR_ANY_INIT,
    };

    if ((server = socket(AF_INET6, SOCK_STREAM, 0)) < 0) {
        return -1;
    }
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    setsockopt(server, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));

    if (bind(server, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        return -1;
    }

    // backlog of 1 - low, but sheds load quickly when we run out of slots
    if (listen(server, 1) < 0) {
        return -1;
    }

    slots->listen[listen_nr] = server;
    return 0;
}

int slots_fdset(slots_t *slots, fd_set *readers, fd_set *writers) {
    int i;
    int fdmax = 0;

    for (i=0; i<slots->nr_slots; i++) {
        if (slots->conn[i].fd == -1) {
            continue;
        }
        int fd = slots->conn[i].fd;
        FD_SET(fd, readers);
        if (conn_iswriter(&slots->conn[i])) {
            FD_SET(fd, writers);
        }
        fdmax = (fd > fdmax)? fd : fdmax;
    }

    // If we have room for more connections, we listen on the server socket(s)
    if (slots->listen[0] && slots->nr_open < slots->nr_slots) {
        for (i=0; i<SLOTS_LISTEN; i++) {
            if (slots->listen[i] == -1) {
                continue;
            }
            int fd = slots->listen[i];
            FD_SET(fd, readers);
            fdmax = (fd > fdmax)? fd : fdmax;
        }
    }

    return fdmax;
}

int slots_accept(slots_t *slots, int listen_nr) {
    int i;

    // TODO: remember previous checked slot and dont start at zero
    for (i=0; i<slots->nr_slots; i++) {
        if (slots->conn[i].fd == -1) {
            break;
        }
    }

    if (i == slots->nr_slots) {
        // No room, inform the caller
        return -2;
    }

    int client = accept(slots->listen[listen_nr], NULL, 0);
    if (client == -1) {
        return -1;
    }

    fcntl(client, F_SETFL, O_NONBLOCK);

    slots->nr_open++;
    slots->conn[i].activity = time(NULL);
    slots->conn[i].fd = client;
    return i;
}

int slots_closeidle(slots_t *slots) {
    int i;
    int nr_closed = 0;
    int min_activity = time(NULL) - slots->timeout;

    for (i=0; i<slots->nr_slots; i++) {
        if (slots->conn[i].fd == -1) {
            continue;
        }
        if (slots->conn[i].activity < min_activity) {
            conn_close(&slots->conn[i]);
            nr_closed++;
        }
    }
    slots->nr_open -= nr_closed;
    if (slots->nr_open < 0) {
        slots->nr_open = 0;
        // should not happen
    }

    return nr_closed;
}

int slots_fdset_loop(slots_t *slots, fd_set *readers, fd_set *writers) {
    for (int i=0; i<SLOTS_LISTEN; i++) {
        if (FD_ISSET(slots->listen[i], readers)) {
            // A new connection
            int slotnr = slots_accept(slots, i);

            switch (slotnr) {
                case -1:
                case -2:
                    return slotnr;

                default:
                    // Schedule slot for immediately reading
                    FD_SET(slots->conn[slotnr].fd, readers);
            }
        }
    }

    int nr_ready = 0;

    for (int i=0; i<slots->nr_slots; i++) {
        if (slots->conn[i].fd == -1) {
            continue;
        }

        if (FD_ISSET(slots->conn[i].fd, readers)) {
            conn_read(&slots->conn[i]);
            // possibly sets state to READY
        }

        // After a read, we could be EMPTY or READY
        // we reach state READY once there is a full request buf
        if (slots->conn[i].state == READY) {
            nr_ready++;
            // TODO:
            // - parse request
            // - possibly callback to generate reply
        }

        // We cannot have got here if it started as an empty slot, so
        // it must have transitioned to empty - close the slot
        if (slots->conn[i].state == EMPTY) {
            slots->nr_open--;
            conn_close(&slots->conn[i]);
            continue;
        }

        if (FD_ISSET(slots->conn[i].fd, writers)) {
            conn_write(&slots->conn[i]);
        }
    }

    return nr_ready;
}
