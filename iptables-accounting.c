/*
 *
 */

#include <arpa/inet.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "strbuf.h"
#include "connslot.h"

/* FIXME: globals */
int service_port = 8088;
#define MODE_SERVICE 1
#define MODE_TEST 2
#define MODE_DUMP 3
int mode = MODE_SERVICE;

void argparser(int argc, char **argv) {
    int error = 0;

    static struct option long_options[] = {
        {"port",    required_argument, 0,  'p' },
        {"test",    no_argument,       0,  't' },
        {"dump",    no_argument,       0,  'd' },
        {"help",    no_argument,       0,  'h' },
        {0,         0,                 0,  0 }
    };

    while (1) {
        int option_index = 0;

        int c = getopt_long(argc, argv, "p:tdh", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'p':
                service_port = atoi(optarg);
                mode = MODE_SERVICE;
                break;
            case 't':
                mode = MODE_TEST;
                break;
            case 'd':
                mode = MODE_DUMP;
                break;
            case 'h':
                printf("Usage:\n");
                printf("    %s [args]\n", argv[0]);
                int i = 0;
                while (long_options[i].name) {
                    printf("  --%s\n", long_options[i].name);
                    i++;
                }
                // TODO: arg indicator and short options
                exit(0);
            default:
                printf("Unknown option\n");
                error++;
        }

        if (optind < argc) {
            printf("Unknown args\n");
            error++;
        }
    }

    if (error) {
        exit(1);
    }
}

struct linedata {
    char *packets;
    char *bytes;
    char *chain;
    char *proto;
    char *port;
    int matched;
    // -1 = bad syntax
    // 0 = good syntax but not matched
    // 1 = matched
};

struct linedata iptables_oneline(char *s) {
    struct linedata d;

    if (*s != '[') {
        d.matched = -1;
        return d;
    }

    s++;

    d.packets = strtok(s,":");
    d.bytes = strtok(NULL,"]");
    d.chain = NULL;
    d.proto = NULL;
    d.port = NULL;
    d.matched = 0;

    char *opt;

    while ((opt = strtok(NULL," ")) != NULL) {
        optarg = NULL;

        if (*opt != '-') {
            // We dont understand this, skip it
            continue;
        }
        opt++;

        if (*opt == '-') {
            // a long opt
            opt++;
            if (strcmp("dport",opt)==0) {
                char *dport = strtok(NULL," ");
                if (!d.port) {
                    d.port = dport;
                }
            } else if (strcmp("sport",opt)==0) {
                char *sport = strtok(NULL," ");
                if (!d.port) {
                    d.port = sport;
                }
            } else if (strcmp("comment",opt)==0) {
                char *comment = strtok(NULL," ");
                // TODO: doesnt handle comments with spaces

                // Check if our tag is here
                d.matched = (strstr(comment, "ACCT")==NULL)?0:1;
            }
            continue;
        }

        switch (*opt) {
            case 'A':
                d.chain = strtok(NULL," ");
                break;
            case 'p':
                d.proto = strtok(NULL," ");
                break;
            case 'm': // module
                strtok(NULL," ");
                break;
        }

    }
    return d;
}

strbuf_t *generate_prom(FILE *input, strbuf_t *p) {
    // [0:0] -A INPUT -f
    // [501:38322] -A INPUT -p tcp -m tcp --dport 22 -m comment --comment "Failsafe SSH" -j ACCEPT
    //
    // table inet counting {
    //   chain INPUT {
    //     type filter hook input priority filter; policy accept;
    //     tcp dport 22 counter packets 0 bytes 0

    struct linedata d;

    p = sb_reprintf(p,"# TYPE iptables_acct_packets_total counter\n");
    p = sb_reprintf(p,"# TYPE iptables_acct_bytes_total counter\n");

    char buf1[100];
    int lines = 0;

    while (!feof(input)) {
        char *s = (char *)&buf1;
        *s = 0;

        if (fgets(s, sizeof(buf1), input) == NULL) {
            break;
        }
        lines++;

        d = iptables_oneline(s);

        if (d.matched != 1) {
            // We didnt match on this line, so skip output
            continue;
        }

        char buf2[100];
        char *labels = (char *)&buf2;
        snprintf(labels, sizeof(buf2),
                "chain=\"%s\",proto=\"%s\",port=\"%s\"",
                d.chain, d.proto, d.port);

        p = sb_reprintf(p,"iptables_acct_packets_total{%s} %s\n",
                labels,
                d.packets
        );
        p = sb_reprintf(p,"iptables_acct_bytes_total{%s} %s\n",
                labels,
                d.bytes
        );
    }

    p = sb_reprintf(p,"iptables_read_lines %i\n", lines);
    p = sb_reprintf(p,"buffer_capacity_bytes %i\n", p->capacity);
    p = sb_reprintf(p,"buffer_used_bytes %i\n", p->wr_pos);

    return p;
}

// Rounds the given time up to the closest multiple of interval
time_t time_round(time_t time, time_t interval) {
    return ((time / interval) + 1) * interval;
}

// FIXME: globals
time_t p_expires = 0;
time_t inject_now = 0;
FILE *inject_input = NULL;

strbuf_t *cache_generate_prom(strbuf_t *p) {
    time_t now = time(NULL);
    if (now >= p_expires) {
        // Refresh the cache
        sb_zero(p);
        p_expires = time_round(now,10);

        FILE *input;
        if (inject_now) {
            // Effectively mock the iptables-save command for automated tests
            now = inject_now;
            input = inject_input;
        } else {
            input = popen("/sbin/iptables-save -c -t raw", "r");
        }
        p = generate_prom(input, p);
        p = sb_reprintf(p, "buffer_timestamp %li\n", now);
        fclose(input);
    }

    return p;
}

void send_str(int fd, char *s) {
    write(fd,s,strlen(s));
}

strbuf_t *http_request(conn_t *conn, strbuf_t *body) {
    strbuf_t *p = conn->reply_header;

    if (strncmp("GET /metrics ",conn->request->str,13) != 0) {
        p = sb_reprintf(p, "HTTP/1.1 404 Not Found\r\n");
        p = sb_reprintf(p, "Content-Length: 0\r\n\r\n");
        conn->reply = NULL;
        goto out;
    }

    body = cache_generate_prom(body);

    if (!body) {
        // We filled up the body strbuf
        p = sb_reprintf(p, "HTTP/1.1 500 overflow\r\n\r\n");
        p = sb_reprintf(p, "buffer_overflow 1\n");
        conn->reply = NULL;
        goto out;
    }

    conn->reply = body;
    p = sb_reprintf(p, "HTTP/1.1 200 OK\r\n");
    p = sb_reprintf(p, "Content-Length: %i\r\n\r\n", conn->reply->wr_pos);

out:
    if (!p) {
        // We filled up the reply_header strbuf
        send_str(conn->fd, "HTTP/1.0 500 \r\n\r\n");
        conn_close(conn);
        return body;
    }

    conn->reply_header = p;
    conn_write(conn);
    return body;
}

#define NR_SLOTS 5

void mode_service(int port, strbuf_t *p) {
    slots_t *slots = slots_malloc(NR_SLOTS);
    if (!slots) {
        abort();
    }

    if (slots_listen_tcp(slots, port)!=0) {
        perror("slots_listen_tcp");
        exit(1);
    }

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

            if (slots->conn[i].state == CONN_READY) {
                p = http_request(&slots->conn[i], p);
            }
        }
    }
}

int main(int argc, char **argv) {
    int outfd = 1;   // stdout

    argparser(argc, argv);

    // Create the cache buffer with a reasonable max size
    strbuf_t *p = sb_malloc(1000);
    p->capacity_max = 200000;

    switch (mode) {
        case MODE_SERVICE:
            mode_service(service_port, p);
            break;
        case MODE_TEST:
            inject_now = 1644144574;
            inject_input = stdin;
        /* FALL THROUGH */
        case MODE_DUMP: {
            p = cache_generate_prom(p);
            if (!p) {
                send_str(outfd, "HTTP/1.0 500 overflow\n\nbuffer_overflow 1\n");
                return -1;
            }

            sb_write(outfd, p, 0, -1);
            break;
        }
    }

    return 0;
}
