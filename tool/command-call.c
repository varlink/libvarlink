#include "command.h"
#include "interface.h"
#include "object.h"
#include "terminal-colors.h"
#include "util.h"

#include <errno.h>
#include <getopt.h>
#include <string.h>

static long call(VarlinkCli *cli) {
        static const struct option options[] = {
                { "address", required_argument, NULL, 'a' },
                { "help",    no_argument,       NULL, 'h' },
                {}
        };
        _cleanup_(freep) char *address = NULL;
        const char *qualified_method;
        _cleanup_(freep) char *interface = NULL;
        _cleanup_(freep) char *method = NULL;
        const char *parameters_json;
        _cleanup_(freep) char *buffer = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *reply = NULL;
        _cleanup_(freep) char *reply_json = NULL;
        int c;
        long r;

        while ((c = getopt_long(cli->argc, cli->argv, "a:fh", options, NULL)) >= 0) {
                switch (c) {
                        case 'a':
                                address = strdup(optarg);
                                break;

                        case 'h':
                                printf("Usage: %s call INTERFACE.METHOD [ARGUMENTS]\n",
                                       program_invocation_short_name);
                                printf("\n");
                                printf("Call METHOD on INTERFACE. ARGUMENTS must be valid JSON.\n");
                                printf("\n");
                                printf("  -a, --address=ADDRESS  connect to ADDRESS instead of resolving the interface\n");
                                printf("  -h, --help             display this help text and exit\n");
                                return EXIT_SUCCESS;

                        default:
                                return exit_error(CLI_ERROR_PANIC);
                }
        }

        qualified_method = cli->argv[optind];
        if (!qualified_method) {
                fprintf(stderr, "Error: expecting INTERFACE.METHOD [ARGUMENTS]\n");
                return CLI_ERROR_MISSING_ARGUMENT;
        }

        r = varlink_interface_parse_qualified_name(qualified_method, &interface, &method);
        if (r < 0) {
                fprintf(stderr, "Error: invalid interface or method name. Must be INTERFACE.METHOD.\n");
                return CLI_ERROR_INVALID_ARGUMENT;
        }

        if (!address) {
                r = varlink_cli_resolve(cli, interface, &address);
                if (r < 0) {
                        fprintf(stderr, "Error resolving interface: %s\n", interface);
                        return CLI_ERROR_CANNOT_RESOLVE;
                }
        }

        r = varlink_cli_connect(cli, address);
        if (r < 0) {
                fprintf(stderr, "Error connecting to: %s\n", interface);
                return CLI_ERROR_CANNOT_CONNECT;
        }

        parameters_json = cli->argv[optind + 1];
        if (!parameters_json) {
                parameters_json = "{}";

        } else if (strcmp(parameters_json, "-") == 0) {
                unsigned long buffer_size = 0;
                unsigned long size = 0;

                for (;;) {
                        if (size == buffer_size) {
                                buffer_size = MAX(buffer_size * 2, 1024);
                                buffer = realloc(buffer, buffer_size);
                        }

                        r = read(STDIN_FILENO, buffer + size, buffer_size - size);
                        if (r <= 0)
                                break;

                        size += r;
                }

                buffer[size] = '\0';

                parameters_json = buffer;
        }

        r = varlink_object_new_from_json(&parameters, parameters_json);
        if (r < 0) {
                fprintf(stderr, "Unable to parse input parameters (must be valid JSON)\n");
                return CLI_ERROR_INVALID_JSON;
        }

        r = varlink_cli_call(cli, qualified_method, parameters, VARLINK_CALL_MORE);
        if (r < 0)
                return exit_error(-r);

        for (;;) {
                _cleanup_(freep) char *error = NULL;
                long flags;

                r = varlink_cli_wait_reply(cli, &reply, &error, &flags);
                if (r < 0)
                        return exit_error(-r);

                if (error)
                        fprintf(stderr, "Error: %s\n", error);

                if (reply) {
                        r = varlink_object_to_pretty_json(reply,
                                                          &reply_json,
                                                          0,
                                                          terminal_color(TERMINAL_CYAN),
                                                          terminal_color(TERMINAL_NORMAL),
                                                          terminal_color(TERMINAL_MAGENTA),
                                                          terminal_color(TERMINAL_NORMAL));
                        if (r < 0) {
                                fprintf(stderr, "Error decoding reply\n");
                                return CLI_ERROR_INVALID_JSON;
                        }

                        printf("%s\n", reply_json);
                }

                if (error)
                        return CLI_ERROR_REMOTE_ERROR;

                if (!(flags & VARLINK_REPLY_CONTINUES))
                        break;
        }

        return EXIT_SUCCESS;
}

const Command command_call = {
        .name = "call",
        .info = "Call a method",
        .function = call
};
