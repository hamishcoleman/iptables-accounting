/*
 * Example and test for httpd library
 *
 * Copyright (C) 2023 Hamish Coleman
 * SPDX-License-Identifier: GPL-2.0-only
 */

#define _GNU_SOURCE
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
#include "jsonrpc.h"

strbuf_t *do_jsonrpc(strbuf_t *request, strbuf_t *reply) {
    // Dont bother looking at the header fields
    // Assume the body is zero terminated text

    sb_zero(reply);

    char *body = memmem(request->str, request->wr_pos, "\r\n\r\n", 4);
    if (!body) {
        reply = sb_reprintf(reply, "Error: no body\n");
        return reply;
    }
    body += 4;

    jsonrpc_t json;

    if (jsonrpc_parse(body, &json) != 0) {
        reply = sb_reprintf(reply, "Error: parsing json\n");
        return reply;
    }

    reply = sb_reprintf(reply, "dump:\n");
    reply = sb_reprintf(reply, "method=%s\n", json.method);
    reply = sb_reprintf(reply, "params=%s\n", json.params);
    reply = sb_reprintf(reply, "id=%s\n", json.id);
    return reply;
}

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

    if (slots_listen_unix(slots, "test.sock")!=0) {
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
                strbuf_t *p;
                // TODO:
                // - parse request

                // generate reply

                if (strncmp("POST /echo ",slots->conn[i].request->str,10) == 0) {
                    slots->conn[i].reply = slots->conn[i].request;
                } else if (strncmp("POST /jsonrpc ",slots->conn[i].request->str,13) == 0) {
                    // TODO: helper to extract http body
                    p = sb_reappend(slots->conn[i].request, "\0", 1);
                    slots->conn[i].request = p;

                    reply = do_jsonrpc(slots->conn[i].request, reply);
                    slots->conn[i].reply = reply;
                } else {
                    slots->conn[i].reply = reply;
                }

                p = slots->conn[i].reply_header;
                p = sb_reprintf(p, "HTTP/1.1 200 OK\r\n");
                p = sb_reprintf(p, "x-slot: %i\r\n", i);
                p = sb_reprintf(p, "x-open: %i\r\n", slots->nr_open);
                p = sb_reprintf(p, "Content-Length: %i\r\n\r\n", slots->conn[i].reply->wr_pos);

                if (p) {
                    slots->conn[i].reply_header = p;

                    // Try to immediately start sending the reply
                    conn_write(&slots->conn[i]);

                } else {
                    // We filled up the reply_header strbuf
                    send_str(slots->conn[i].fd, "HTTP/1.0 500 \r\n\r\n");
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
