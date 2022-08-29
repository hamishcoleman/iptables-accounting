/*
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

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

void output_prom(FILE *input, FILE *output) {
    // [0:0] -A INPUT -f
    // [501:38322] -A INPUT -p tcp -m tcp --dport 22 -m comment --comment "Failsafe SSH" -j ACCEPT

    fprintf(output,"# TYPE iptables_acct_packets_total counter\n");
    fprintf(output,"# TYPE iptables_acct_bytes_total counter\n");

    char buf1[100];

    while (!feof(input)) {
        char *s = (char *)&buf1;
        *s = 0;

        if (fgets(s, sizeof(buf1), input) == NULL) {
            return;
        }

        if (*s != '[') {
            continue;
        }

        s++;

        char *packets = strtok(s,":");
        char *bytes = strtok(NULL,"]");

        char *opt;
        char *chain = NULL;
        char *proto = NULL;
        char *port = NULL;
        char *matchtag = NULL;

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
                    if (!port) {
                        port = dport;
                    }
                } else if (strcmp("sport",opt)==0) {
                    char *sport = strtok(NULL," ");
                    if (!port) {
                        port = sport;
                    }
                } else if (strcmp("comment",opt)==0) {
                    char *comment = strtok(NULL," ");
                    // TODO: doesnt handle comments with spaces

                    // Check if our tag is here
                    matchtag = strstr(comment, "ACCT");
                }
                continue;
            }

            switch (*opt) {
                case 'A':
                    chain = strtok(NULL," ");
                    break;
                case 'p':
                    proto = strtok(NULL," ");
                    break;
                case 'm': // module
                    strtok(NULL," ");
                    break;
            }

        }

        if (!matchtag) {
            // We didnt match on this line, so skip output
            continue;
        }

        char buf2[100];
        char *labels = (char *)&buf2;
        snprintf(labels, sizeof(buf2),
                 "chain=\"%s\",proto=\"%s\",port=\"%s\"",
                 chain, proto, port);

        fprintf(output,"iptables_acct_packets_total{%s} %s\n", labels, packets);
        fprintf(output,"iptables_acct_bytes_total{%s} %s\n", labels, bytes);
    }
}

void http_connection(int fd) {
    char buf1[100];

    FILE *reader = fdopen(fd, "r");
    FILE *output = fdopen(fd, "w");

    char *s = (char *)&buf1;
    *s = 0;
    if (fgets(s, sizeof(buf1), reader) == NULL) {
        goto out1;
    }
    if (strncmp("GET /metrics ",s,13) != 0) {
        goto out1;
    }

    s = (char *)&buf1;
    while (fgets(s, sizeof(buf1), reader)) {
        if (s[0]=='\r' && s[1]=='\n') {
            // just the newline
            break;
        }
    }

    // TODO: cache the output

    FILE *input = popen("/sbin/iptables-save -c -t raw", "r");

    fprintf(output,"HTTP/1.0 200 OK\n\n");
    output_prom(input, output);

    fclose(input);
out1:
    fclose(output);
    fclose(reader);
}

void mode_service(int port) {
    int server;
    int client;
    int on = 1;
    struct sockaddr_in addr;

    if ((server = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(server, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(server, 3) < 0) {
        perror("listen");
        exit(1);
    }

    while ((client = accept(server, NULL, 0))) {
        http_connection(client);
        close(client);
    }
}

int main(int argc, char **argv) {
    FILE *input;

    argparser(argc, argv);

    switch (mode) {
        case MODE_SERVICE:
            mode_service(service_port);
            break;
        case MODE_TEST:
            output_prom(stdin, stdout);
            break;
        case MODE_DUMP:
            input = popen("iptables-save -c -t raw", "r");
            output_prom(input, stdout);
            break;
    }

    return 0;
}
