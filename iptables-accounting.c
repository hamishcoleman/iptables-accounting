/*
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>

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
        int this_option_optind = optind ? optind : 1;
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

void mode_service(int port) {
    // create listening port service_port
    // loop wsiting for connection
    //  read http request headers
    //  if we are happy, call service handler with fd
}

int main(int argc, char **argv) {
    argparser(argc, argv);

    switch (mode) {
        case MODE_SERVICE:
            mode_service(service_port);
            break;
        case MODE_TEST:
            output_prom(stdin, stdout);
            break;
        case MODE_DUMP:
            // input = popen( "iptables-save -c -t raw" )
            // output_prom( input, stdout )
            break;
    }

    return 0;
}
