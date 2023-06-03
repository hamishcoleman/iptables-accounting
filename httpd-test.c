/*
 * Example and test for httpd library
 *
 * Copyright (C) 2023 Hamish Coleman
 * SPDX-License-Identifier: GPL-2.0-only
 */

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

void send_str(int fd, char *s) {
    write(fd,s,strlen(s));
}

#define NR_SLOTS 5
void httpd_test(int port) {
    slots_t *slots = slots_malloc(NR_SLOTS);
    if (!slots) {
        abort();
    }

    if (slots_listen_tcp(slots, port)!=0) {
        perror("slots_listen_tcp");
        exit(1);
    }

    strbuf_t *reply = sb_malloc(48);
    reply->capacity_max = 1000;
    sb_printf(reply, "Hello World\n");

    signal(SIGPIPE, SIG_IGN);

    int running = 1;
    while (running) {
        fd_set readers;
        fd_set writers;
        FD_ZERO(&readers);
        FD_ZERO(&writers);
        int fdmax = slots_fdset(slots, &readers, &writers);

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
            slots_closeidle(slots);
            continue;
        }

        // There is at least one event waiting
        int nr_ready = slots_fdset_loop(slots, &readers, &writers);

        switch (nr_ready) {
            case -1:
                perror("accept");
                exit(1);

            case -2:
                // No slots! - shouldnt happen, since we gate on nr_open
                printf("no slots\n");
                exit(1);

            case 0:
                continue;
        }

        for (int i=0; i<slots->nr_slots; i++) {
            if (slots->conn[i].fd == -1) {
                continue;
            }

            if (slots->conn[i].state == READY) {
                // TODO:
                // - parse request

                // generate reply
                slots->conn[i].reply = reply;
                strbuf_t *p = slots->conn[i].reply_header;
                p = sb_reprintf(p, "HTTP/1.1 200 OK\n");
                p = sb_reprintf(p, "x-slot: %i\n", i);
                p = sb_reprintf(p, "x-open: %i\n", slots->nr_open);
                p = sb_reprintf(p, "Content-Length: %i\n\n", reply->wr_pos);

                if (p) {
                    slots->conn[i].reply_header = p;

                    // Try to immediately start sending the reply
                    conn_write(&slots->conn[i]);

                } else {
                    // We filled up the reply_header strbuf
                    send_str(slots->conn[i].fd, "HTTP/1.0 500 \n\n");
                    slots->conn[i].state = EMPTY;
                    // TODO: we might have corrupted the ->reply_header
                }
            }
        }
    }
}

int main() {
    int port = 8080;
    printf("Running http test server on port %i\n", port);

    httpd_test(port);
}
