/*
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>

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

int main(int argc, char **argv) {
    argparser(argc, argv);
}
