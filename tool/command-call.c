#include "command.h"
#include "interface.h"
#include "object.h"
#include "terminal-colors.h"
#include "util.h"

#include <errno.h>
#include <getopt.h>
#include <string.h>

static const struct option options[] = {
        { "address", required_argument, NULL, 'a' },
        { "help",    no_argument,       NULL, 'h' },
        {}
};

typedef struct {
        bool help;
        const char *address;

        const char *method;
        const char *parameters;
} CallArguments;

static long call_parse_arguments(int argc, char **argv, CallArguments *arguments) {
        int c;

        while ((c = getopt_long(argc, argv, ":a:fh", options, NULL)) >= 0) {
                switch (c) {
                        case 'a':
                                arguments->address = optarg;
                                break;
                        case 'h':
                                arguments->help = true;
                                break;
                        case '?':
                                return -CLI_ERROR_INVALID_ARGUMENT;
                        case ':':
                                return -CLI_ERROR_MISSING_ARGUMENT;
                        default:
                                return -CLI_ERROR_PANIC;
                }
        }

        if (optind >= argc)
                return -CLI_ERROR_MISSING_ARGUMENT;

        arguments->method = argv[optind];
        arguments->parameters = argv[optind + 1];

        return 0;
}

static long call_run(Cli *cli, int argc, char **argv) {
        CallArguments arguments = { 0 };
        _cleanup_(freep) char *address = NULL;
        _cleanup_(freep) char *interface = NULL;
        _cleanup_(freep) char *method = NULL;
        _cleanup_(freep) char *buffer = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *reply = NULL;
        _cleanup_(freep) char *reply_json = NULL;
        long r;

        r = call_parse_arguments(argc, argv, &arguments);
        switch (r) {
                case 0:
                        break;
                case -CLI_ERROR_MISSING_ARGUMENT:
                        fprintf(stderr, "Error: expecting INTERFACE.METHOD [ARGUMENTS]\n");
                        return CLI_ERROR_MISSING_ARGUMENT;
                default:
                        return CLI_ERROR_PANIC;
        }

        if (arguments.help) {
                printf("Usage: %s call INTERFACE.METHOD [ARGUMENTS]\n",
                       program_invocation_short_name);
                printf("\n");
                printf("Call METHOD on INTERFACE. ARGUMENTS must be valid JSON.\n");
                printf("\n");
                printf("  -a, --address=ADDRESS  connect to ADDRESS instead of resolving the interface\n");
                printf("  -h, --help             display this help text and exit\n");
                return EXIT_SUCCESS;
        }

        r = varlink_interface_parse_qualified_name(arguments.method, &interface, &method);
        if (r < 0) {
                fprintf(stderr, "Error: invalid interface or method name. Must be INTERFACE.METHOD.\n");
                return CLI_ERROR_INVALID_ARGUMENT;
        }

        if (arguments.address) {
                address = strdup(arguments.address);
        } else {
                r = cli_resolve(cli, interface, &address);
                if (r < 0) {
                        fprintf(stderr, "Error resolving interface: %s\n", interface);
                        return CLI_ERROR_CANNOT_RESOLVE;
                }
        }

        r = cli_connect(cli, address);
        if (r < 0) {
                fprintf(stderr, "Error connecting to: %s\n", interface);
                return CLI_ERROR_CANNOT_CONNECT;
        }

        if (!arguments.parameters) {
                arguments.parameters = "{}";

        } else if (strcmp(arguments.parameters, "-") == 0) {
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

                arguments.parameters = buffer;
        }

        r = varlink_object_new_from_json(&parameters, arguments.parameters);
        if (r < 0) {
                fprintf(stderr, "Unable to parse input parameters (must be valid JSON)\n");
                return CLI_ERROR_INVALID_JSON;
        }

        r = cli_call(cli, arguments.method, parameters, VARLINK_CALL_MORE);
        if (r < 0)
                return cli_exit_error(-r);

        for (;;) {
                _cleanup_(freep) char *error = NULL;
                long flags;

                r = cli_wait_reply(cli, &reply, &error, &flags);
                if (r < 0)
                        return cli_exit_error(-r);

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

static long call_complete(Cli *cli, int argc, char **argv, const char *current) {
        CallArguments arguments = { 0 };
        long r;

        r = call_parse_arguments(argc, argv, &arguments);
        switch (r) {
                case 0:
                case -CLI_ERROR_INVALID_ARGUMENT:
                case -CLI_ERROR_MISSING_ARGUMENT:
                        break;
                default:
                        return -r;
        }

        if (current[0] == '-')
                return cli_complete_options(cli, options, current);

        if (!arguments.method)
                return cli_complete_qualified_methods(cli, current);

        if (!arguments.parameters)
                cli_print_completion(current, "'{}'");

        return 0;
}

const CliCommand command_call = {
        .name = "call",
        .info = "Call a method",
        .run = call_run,
        .complete = call_complete
};
