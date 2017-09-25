#include "command.h"
#include "connection.h"
#include "error.h"
#include "interface.h"
#include "object.h"
#include "terminal-colors.h"
#include "util.h"

#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <sys/socket.h>

static const struct option options[] = {
        { "help",    no_argument,       NULL, 'h' },
        { "more",    no_argument,       NULL, 'm' },
        { "oneway",  no_argument,       NULL, 'o' },
        {}
};

typedef struct {
        bool help;

        uint64_t flags;

        char *host;
        int port;

        const char *method;
        const char *parameters;
} CallArguments;

static CallArguments *call_arguments_free(CallArguments *arguments) {
        free(arguments->host);
        free(arguments);

        return NULL;
}

static void call_arguments_freep(CallArguments **argumentsp) {
        if (*argumentsp)
                call_arguments_free(*argumentsp);
}

static long call_arguments_new(CallArguments **argumentsp) {
        _cleanup_(call_arguments_freep) CallArguments *arguments = NULL;

        arguments = calloc(1, sizeof(CallArguments));

        *argumentsp = arguments;
        arguments = NULL;

        return 0;
}

static long call_parse_url(CallArguments *arguments, const char *url) {
        /* varlink:// */
        if (strncmp(url, "varlink://", 10) == 0) {
                if (!strchr(url + 10, '/'))
                        return -CLI_ERROR_INVALID_ARGUMENT;

                //FIXME: URL-decode slashes in unix path

                arguments->method = url + 10;

                return 1;
        }

        /* ssh://[host][:port]/ */
        if (strncmp(url, "ssh://", 6) == 0) {
                char *s;
                char *host;
                int port;

                s = strchr(url + 6, '/');
                if (!s)
                        return -CLI_ERROR_INVALID_ARGUMENT;

                arguments->host = strndup(url + 6, s - (url + 6));
                arguments->method = s + 1;

                /* Extract optional port number */
                if (sscanf(arguments->host, "%m[^:]:%d", &host, &port) == 2) {
                        free(arguments->host);
                        arguments->host = host;
                        arguments->port = port;
                }

                return 1;
        }

        return 0;
}

static long call_parse_arguments(int argc, char **argv, CallArguments *arguments) {
        int c;
        long r;

        while ((c = getopt_long(argc, argv, ":a:fhmo", options, NULL)) >= 0) {
                switch (c) {
                        case 'h':
                                arguments->help = true;
                                return 0;

                        case 'm':
                                arguments->flags |= VARLINK_CALL_MORE;
                                continue;

                        case 'o':
                                arguments->flags |= VARLINK_CALL_ONEWAY;
                                continue;

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

        r = call_parse_url(arguments, argv[optind]);
        if (r < 0)
                return -CLI_ERROR_INVALID_ARGUMENT;

        /* Not a URL */
        if (r == 0)
                arguments->method = argv[optind];

        arguments->parameters = argv[optind + 1];

        return 0;
}

static void reply_callback(VarlinkConnection *connection,
                           const char *error,
                           VarlinkObject *parameters,
                           uint64_t flags,
                           void *userdata) {
        unsigned long *errorp = userdata;
        _cleanup_(freep) char *json = NULL;
        long r;

        if (error) {
                fprintf(stderr, "Error: %s\n", error);
                *errorp = CLI_ERROR_REMOTE_ERROR;
                varlink_connection_close(connection);
                return;
        }

        r = varlink_object_to_pretty_json(parameters,
                                          &json,
                                          0,
                                          terminal_color(TERMINAL_CYAN),
                                          terminal_color(TERMINAL_NORMAL),
                                          terminal_color(TERMINAL_MAGENTA),
                                          terminal_color(TERMINAL_NORMAL));
        if (r < 0) {
                fprintf(stderr, "Error: InvalidJson\n");
                *errorp = CLI_ERROR_INVALID_JSON;
                varlink_connection_close(connection);
                return;
        }

        printf("%s\n", json);

        if (!(flags & VARLINK_REPLY_CONTINUES))
                varlink_connection_close(connection);
}

static long connection_new_ssh(VarlinkConnection **connectionp, CallArguments *arguments) {
        int sp[2];
        pid_t pid;
        long r;

        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0)
                return -CLI_ERROR_PANIC;

        pid = fork();
        if (pid < 0) {
                close(sp[0]);
                close(sp[1]);
                return -CLI_ERROR_PANIC;
        }

        if (pid == 0) {
                const char *arg[9];
                long i = 0;
                char port[8];

                arg[i++] = "ssh";
                arg[i++] = "-xT";

                /* Add custom port number */
                if (arguments->port > 0) {
                        arg[i++] = "-p";

                        sprintf(port, "%d", arguments->port);
                        arg[i++] = port;
                }

                arg[i++] = "--";
                arg[i++] = arguments->host;
                arg[i++] = "varlink";
                arg[i++] = "bridge";
                arg[i] = NULL;

                close(sp[0]);

                if (dup2(sp[1], STDIN_FILENO) != STDIN_FILENO ||
                    dup2(sp[1], STDOUT_FILENO) != STDOUT_FILENO)
                        return -CLI_ERROR_PANIC;

                close(sp[1]);

                execvp(arg[0], (char **) arg);
                _exit(EXIT_FAILURE);
        }

        close(sp[1]);

        r = varlink_connection_new_from_socket(connectionp, sp[0]);
        if (r < 0)
                return -CLI_ERROR_CANNOT_CONNECT;

        return 0;
}

static long call_run(Cli *cli, int argc, char **argv) {
        _cleanup_(call_arguments_freep) CallArguments *arguments = NULL;
        _cleanup_(freep) char *address = NULL;
        const char *method = NULL;
        _cleanup_(varlink_connection_freep) VarlinkConnection *connection = NULL;
        _cleanup_(freep) char *buffer = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *out = NULL;
        long error = 0;
        long r;

        call_arguments_new(&arguments);

        r = call_parse_arguments(argc, argv, arguments);
        switch (r) {
                case 0:
                        break;

                case -CLI_ERROR_MISSING_ARGUMENT:
                        fprintf(stderr, "Missing argument, INTERFACE.METHOD [ARGUMENTS] expected\n");
                        return CLI_ERROR_MISSING_ARGUMENT;

                case -CLI_ERROR_INVALID_ARGUMENT:
                        fprintf(stderr, "Invalid argument, INTERFACE.METHOD [ARGUMENTS] expected\n");
                        return CLI_ERROR_INVALID_ARGUMENT;

                default:
                        fprintf(stderr, "Unhandled exception.\n");
                        return CLI_ERROR_PANIC;
        }

        if (arguments->help) {
                printf("Usage: %s call [ADDRESS/]INTERFACE.METHOD [ARGUMENTS]\n", program_invocation_short_name);
                printf("\n");
                printf("Call METHOD on INTERFACE at ADDRESS. ARGUMENTS must be valid JSON.\n");
                printf("\n");
                printf("  -h, --help             display this help text and exit\n");
                printf("  -m, --more             wait for multiple method returns if supported\n");
                printf("  -o, --oneway           do not request a reply\n");
                return EXIT_SUCCESS;
        }

        if (!arguments->parameters) {
                arguments->parameters = "{}";

        } else if (strcmp(arguments->parameters, "-") == 0) {
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

                arguments->parameters = buffer;
        }

        r = varlink_object_new_from_json(&parameters, arguments->parameters);
        if (r < 0) {
                fprintf(stderr, "Unable to parse input parameters, must be valid JSON\n");
                return CLI_ERROR_INVALID_JSON;
        }

        r  = cli_split_address(arguments->method, &address, &method);
        if (r < 0) {
                fprintf(stderr, "Unable to parse address: %s\n", cli_error_string(-r));
                return -r;
        }

        if (!address) {
                _cleanup_(freep) char *interface = NULL;

                r = varlink_interface_parse_qualified_name(method, &interface, NULL);
                if (r < 0) {
                        fprintf(stderr, "Unable to parse address: %s\n", cli_error_string(-r));
                        return CLI_ERROR_INVALID_ARGUMENT;
                }

                r = cli_resolve(cli, interface, &address);
                if (r < 0) {
                        fprintf(stderr, "Unable to resolve interface: %s\n", cli_error_string(-r));
                        return -r;
                }
        }

        if (arguments->host) {
                r = connection_new_ssh(&connection, arguments);
                if (r < 0) {
                        fprintf(stderr, "Unable to connect with SSH: %s\n", cli_error_string(-r));
                        return -r;
                }

        } else {
                r = varlink_connection_new(&connection, address);
                if (r < 0) {
                        fprintf(stderr, "Unable to connect: %s\n", varlink_error_string(-r));
                        return CLI_ERROR_CANNOT_CONNECT;
                }
        }

        r = varlink_connection_call(connection,
                                    method,
                                    parameters,
                                    arguments->flags,
                                    reply_callback,
                                    &error);
        if (r < 0) {
                fprintf(stderr, "Unable to call: %s\n", varlink_error_string(-r));
                return CLI_ERROR_CALL_FAILED;
        }

        r = cli_process_all_events(cli, connection);
        if (r >= 0)
                return EXIT_SUCCESS;

        /* CTRL-C */
        if (r == -CLI_ERROR_CANCELED)
                return EXIT_SUCCESS;

        fprintf(stderr, "Unable to process events: %s\n", cli_error_string(-r));
        return -r;
}

static long call_complete(Cli *cli, int argc, char **argv, const char *current) {
        CallArguments arguments = {};
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

        if (current[0] == '/')
                return cli_complete_addresses(cli, current);

        if (current[0] == '-')
                return cli_complete_options(cli, options, current);

        if (!arguments.method)
                return cli_complete_methods(cli, current);

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
