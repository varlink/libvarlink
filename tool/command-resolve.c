#include "command.h"
#include "interface.h"
#include "object.h"
#include "terminal-colors.h"
#include "util.h"

#include <errno.h>
#include <getopt.h>
#include <string.h>

static long resolve(VarlinkCli *cli) {
        static const struct option options[] = {
                { "help", no_argument, NULL, 'h' },
                {}
        };
        _cleanup_(freep) char *address = NULL;
        const char *interface = NULL;
        int c;
        long r;

        while ((c = getopt_long(cli->argc, cli->argv, "h", options, NULL)) >= 0) {
                switch (c) {
                        case 'h':
                                printf("Usage: %s resolve INTERFACE\n", program_invocation_short_name);
                                printf("\n");
                                printf("Resolve INTERFACE to the varlink address that implements it.\n");
                                printf("\n");
                                printf("  -h, --help             display this help text and exit\n");
                                return EXIT_SUCCESS;

                        default:
                                return exit_error(CLI_ERROR_PANIC);
                }
        }

        interface = cli->argv[optind];
        if (!interface) {
                fprintf(stderr, "Error: expecting INTERFACE\n");

                return EXIT_FAILURE;
        }

        r = varlink_cli_resolve(cli, interface, &address);
        if (r < 0) {
                fprintf(stderr, "Error resolving interface %s: %s\n", interface, strerror(-r));
                return r;
        }

        printf("%s\n", address);

        return EXIT_SUCCESS;
}

const Command command_resolve = {
        .name = "resolve",
        .info = "Resolve an interface name to a varlink address",
        .function = resolve
};
