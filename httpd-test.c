/*
 * Example and test for httpd library
 *
 * Copyright (C) 2023 Hamish Coleman
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "strbuf.h"
#include "connslot.h"

int do_listen(int port) {
    int server;
    int on = 1;
    int off = 0;
    struct sockaddr_in6 addr = {
        .sin6_family = AF_INET6,
        .sin6_port = htons(port),
        .sin6_addr = IN6ADDR_ANY_INIT,
    };

    if ((server = socket(AF_INET6, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    setsockopt(server, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));

    if (bind(server, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    // backlog of 1 - low, but sheds load quickly when we run out of slots
    if (listen(server, 1) < 0) {
        perror("listen");
        exit(1);
    }
    return server;
}

void send_str(int fd, char *s) {
    write(fd,s,strlen(s));
}

#define NR_SLOTS 5
void httpd_test(int port) {
    int server = do_listen(port);
    conn_t slot[NR_SLOTS];
    int nr_open = 0;

    strbuf_t *reply = sb_malloc(48);
    reply->capacity_max = 1000;
    sb_printf(reply, "Hello World\n");

    httpdslots_init(slot, sizeof(slot));

    signal(SIGPIPE, SIG_IGN);

    int running = 1;
    while (running) {
        fd_set readers;
        fd_set writers;
        FD_ZERO(&readers);
        FD_ZERO(&writers);
        int fdmax = httpdslots_fdset(slot, sizeof(slot), &readers, &writers);

        // If we have room for more connections, we listen on the server socket
        if (nr_open < NR_SLOTS) {
            FD_SET(server, &readers);
            fdmax = (server > fdmax)? server : fdmax;
        }

        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;

        int nr = select(fdmax+1, &readers, &writers, NULL, &tv);

        if (nr == -1) {
            perror("select");
            exit(1);
        }
        if (nr == 0) {
            // Must be a timeout
            int nr_closed = httpdslots_closeidle(slot, sizeof(slot));
            nr_open -= nr_closed;
            if (nr_open < 0) {
                nr_open = 0;
                // should not happen
                printf("idle count mismatch\n");
            }
            continue;
        }

        // There is at least one event waiting

        if (FD_ISSET(server, &readers)) {
            // A new connection
            int slotnr = httpdslots_accept(slot, sizeof(slot), server);

            switch (slotnr) {
                case -1:
                    perror("accept");
                    exit(1);

                case -2:
                    // No slots! - shouldnt happen, since we gate on nr_open
                    printf("no slots\n");
                    break;

                default:
                    nr_open++;
                    // Try to immediately read the request
                    FD_SET(slot[slotnr].fd, &readers);
            }
        }

        for (int i=0; i<NR_SLOTS; i++) {
            if (slot[i].fd == -1) {
                continue;
            }

            if (FD_ISSET(slot[i].fd, &readers)) {
                conn_read(&slot[i]);
                // possibly sets state to READY
            }

            // After a read, we could be EMPTY or READY
            // we reach state READY once there is a full request buf
            if (slot[i].state == READY) {
                // TODO:
                // - parse request

                // generate reply

                if (strncmp("POST /api ",slot[i].request->str,10) == 0) {
                    slot[i].reply = slot[i].request;
                } else {
                    slot[i].reply = reply;
                }

                strbuf_t *p = slot[i].reply_header;
                p = sb_reprintf(p, "HTTP/1.1 200 OK\r\n");
                p = sb_reprintf(p, "x-slot: %i\r\n", i);
                p = sb_reprintf(p, "x-open: %i\r\n", nr_open);
                p = sb_reprintf(p, "Content-Length: %i\r\n\r\n", slot[i].reply->wr_pos);

                if (p) {
                    slot[i].reply_header = p;

                } else {
                    // We filled up the reply_header strbuf
                    send_str(slot[i].fd, "HTTP/1.0 500 \r\n\r\n");
                    slot[i].state = EMPTY;
                    // TODO: we might have corrupted the ->reply_header
                }

                // Try to immediately start sending the reply
                FD_SET(slot[i].fd, &writers);
            }

            // We cannot have got here if it started as an empty slot, so
            // it must have transitioned to empty - close the slot
            if (slot[i].state == EMPTY) {
                nr_open--;
                conn_close(&slot[i]);
                continue;
            }

            if (FD_ISSET(slot[i].fd, &writers)) {
                conn_write(&slot[i]);
            }

        }
    }
}

int main() {
    int port = 8080;
    printf("Running http test server on port %i\n", port);

    httpd_test(port);
}
