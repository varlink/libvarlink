// SPDX-License-Identifier: Apache-2.0

#include "command.h"
#include "object.h"
#include "util.h"

#include <errno.h>
#include <getopt.h>
#include <string.h>

static long print_interfaces(Cli *cli) {
        _cleanup_(varlink_connection_freep) VarlinkConnection *connection = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *info = NULL;
        _cleanup_(freep) char *error = NULL;
        VarlinkArray *interfaces;
        unsigned long n_interfaces;
        long r;

        r = varlink_connection_new(&connection, cli->resolver);
        if (r < 0) {
                fprintf(stderr, "Unable to connect: %s\n", varlink_error_string(-r));
                return r;
        }

        r = cli_call(cli,
                     connection,
                     "org.varlink.resolver.GetInfo",
                     NULL,
                     0,
                     &error,
                     &info);
        if (r < 0) {
                fprintf(stderr, "Unable to call method: %s\n", cli_error_string(-r));
                return r;
        }

        if (error) {
                fprintf(stderr, "Call failed with error: %s\n", error);
                return -CLI_ERROR_REMOTE_ERROR;
        }

        if (varlink_object_get_array(info, "interfaces", &interfaces) < 0) {
                fprintf(stderr, "Unable to parse reply\n");
                return -CLI_ERROR_INVALID_MESSAGE;
        }

        n_interfaces = varlink_array_get_n_elements(interfaces);
        for (unsigned long i = 0; i < n_interfaces; i += 1) {
                const char *interface;

                if (varlink_array_get_string(interfaces, i, &interface) < 0) {
                        fprintf(stderr, "Unable to parse reply\n");
                        return -CLI_ERROR_INVALID_MESSAGE;
                }

                printf("%s\n", interface);
        }

        return 0;
}

static long resolve_run(Cli *cli, int argc, char **argv) {
        static const struct option options[] = {
                { "help", no_argument, NULL, 'h' },
                {}
        };
        _cleanup_(freep) char *address = NULL;
        const char *interface = NULL;
        int c;
        long r;

        while ((c = getopt_long(argc, argv, "h", options, NULL)) >= 0) {
                switch (c) {
                        case 'h':
                                printf("Usage: %s resolve INTERFACE\n", program_invocation_short_name);
                                printf("\n");
                                printf("Resolve INTERFACE to the varlink address that implements it.\n");
                                printf("\n");
                                printf("  -h, --help             display this help text and exit\n");
                                return 0;

                        default:
                                return -CLI_ERROR_PANIC;
                }
        }

        interface = argv[optind];
        if (!interface) {
                r = print_interfaces(cli);
                if (r < 0) {
                        fprintf(stderr, "Error retrieving interfaces\n");
                        return -CLI_ERROR_CANNOT_RESOLVE;
                }

                return 0;
        }

        r = cli_resolve(cli, interface, &address);
        if (r < 0) {
                fprintf(stderr, "Error resolving interface %s\n", interface);
                return -CLI_ERROR_CANNOT_RESOLVE;
        }

        printf("%s\n", address);

        return 0;
}

static long resolve_complete(Cli *cli, int argc, char **UNUSED(argv), const char *current) {
        if (argc != 1)
                return 0;

        return cli_complete_interfaces(cli, current, false);
}

const CliCommand command_resolve = {
        .name = "resolve",
        .info = "Resolve an interface name to a varlink address",
        .run = resolve_run,
        .complete = resolve_complete
};
