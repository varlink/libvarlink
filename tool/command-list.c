#include "command.h"
#include "interface.h"
#include "object.h"
#include "terminal-colors.h"
#include "util.h"

#include <errno.h>
#include <getopt.h>
#include <string.h>

static long print_interfaces(VarlinkCli *cli, const char *field) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *out = NULL;
        _cleanup_(freep) char *error = NULL;
        VarlinkArray *interfaces;
        long n_interfaces;
        long r;

        r = varlink_cli_connect(cli, cli->resolver);
        if (r < 0)
                return r;

        r = varlink_cli_call(cli, "org.varlink.resolver.GetInterfaces", NULL, 0);
        if (r < 0)
                return r;

        r = varlink_cli_wait_reply(cli, &out, &error, NULL);
        if (r < 0)
                return r;

        if (error)
                return -CLI_ERROR_CANNOT_RESOLVE;

        r = varlink_cli_disconnect(cli);
        if (r < 0)
                return r;

        r = varlink_object_get_array(out, "interfaces", &interfaces);
        if (r < 0)
                return -CLI_ERROR_CANNOT_RESOLVE;

        n_interfaces = varlink_array_get_n_elements(interfaces);
        for (long i = 0; i < n_interfaces; i += 1) {
                VarlinkObject *interface;
                const char *s;

                varlink_array_get_object(interfaces, i, &interface);
                varlink_object_get_string(interface, field, &s);
                printf("%s\n", s);
        }

        return 0;
}

static long list(VarlinkCli *cli) {
        static const struct option options[] = {
                { "help",             no_argument, NULL, 'h' },
                {}
        };
        const char *type = NULL;
        int c;

        while ((c = getopt_long(cli->argc, cli->argv, "h", options, NULL)) >= 0) {
                switch (c) {
                        case 'h':
                                printf("Usage: %s list interfaces|addresses\n", program_invocation_short_name);
                                printf("\n");
                                printf("List available interfaces, addresses\n");
                                printf("\n");
                                printf("  -h, --help             display this help text and exit\n");
                                return EXIT_SUCCESS;

                        default:
                                return exit_error(CLI_ERROR_PANIC);
                }
        }

        type = cli->argv[optind];
        if (!type) {
                fprintf(stderr, "Error: expecting addresses|interfaces\n");

                return CLI_ERROR_MISSING_ARGUMENT;
        }

        if (strcmp(type, "interfaces") == 0)
                return print_interfaces(cli, "interface");
        else if (strcmp(type, "addresses") == 0)
                return print_interfaces(cli, "address");

        return exit_error(CLI_ERROR_INVALID_ARGUMENT);
}

const Command command_list = {
        .name = "list",
        .info = "List interfaces, addresses",
        .function = list
};
