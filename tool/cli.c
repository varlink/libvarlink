#include "cli.h"

#include "connection.h"
#include "command.h"
#include "interface.h"
#include "uri.h"
#include "util.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/wait.h>

static const char *error_strings[] = {
        [CLI_ERROR_PANIC] = "Panic",
        [CLI_ERROR_CANNOT_RESOLVE] = "CannotResolve",
        [CLI_ERROR_MISSING_COMMAND] = "MissingCommand",
        [CLI_ERROR_COMMAND_NOT_FOUND] = "CommandNotFound",
        [CLI_ERROR_MISSING_ARGUMENT] = "MissingArgument",
        [CLI_ERROR_INVALID_ARGUMENT] = "InvalidArgument",
        [CLI_ERROR_INVALID_JSON] = "InvalidJson",
        [CLI_ERROR_CANNOT_CONNECT] = "CannotConnect",
        [CLI_ERROR_TIMEOUT] = "Timeout",
        [CLI_ERROR_CANCELED] = "Canceled",
        [CLI_ERROR_REMOTE_ERROR] = "RemoteError",
        [CLI_ERROR_CALL_FAILED] = "CallFailed",
        [CLI_ERROR_INVALID_MESSAGE] = "InvalidMessage",
        [CLI_ERROR_CONNECTION_CLOSED] = "ConnectionClosed",
};

static const struct option cli_options[] = {
        { "activate", required_argument, NULL, 'A' },
        { "bridge",   required_argument, NULL, 'b' },
        { "help",     no_argument,       NULL, 'h' },
        { "resolver", required_argument, NULL, 'R' },
        { "timeout",  required_argument, NULL, 't' },
        { "version",  no_argument,       NULL, 'V' },
        {}
};

typedef struct {
        bool help;
        bool version;
        const char *activate;
        const char *bridge;
        const char *resolver;
        long timeout;

        const char *command;
        int remaining_argc;
        char **remaining_argv;
} CliArguments;

const char *cli_error_string(long error) {
        if (error == 0 || error >= (long)ARRAY_SIZE(error_strings))
                return "<invalid>";

        if (!error_strings[error])
                return "<missing>";

        return error_strings[error];
}

long cli_new(Cli **mp) {
        _cleanup_(cli_freep) Cli *cli = NULL;
        sigset_t mask;

        cli = calloc(1, sizeof(Cli));
        cli->resolver = "unix:/run/org.varlink.resolver";
        cli->timeout = -1;

        cli->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
        if (cli->epoll_fd < 0)
                return -CLI_ERROR_PANIC;

        sigemptyset(&mask);
        sigaddset(&mask, SIGTERM);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGPIPE);
        sigprocmask(SIG_BLOCK, &mask, NULL);

        cli->signal_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
        if (cli->signal_fd < 0)
                return -CLI_ERROR_PANIC;

        if (epoll_add(cli->epoll_fd, cli->signal_fd, EPOLLIN, NULL) < 0)
                return -CLI_ERROR_PANIC;

        *mp = cli;
        cli = NULL;

        return 0;
}

Cli *cli_free(Cli *cli) {
        if (cli->epoll_fd > 0)
                close(cli->epoll_fd);

        if (cli->signal_fd > 0)
                close(cli->signal_fd);

        if (cli->pid > 0 && kill(cli->pid, SIGTERM) >= 0)
                waitpid(cli->pid, NULL, 0);

        if (cli->path) {
                char *s;

                unlink(cli->path);
                s = strrchr(cli->path, '/');
                if (s) {
                        *s = '\0';
                        rmdir(cli->path);
                }
                free(cli->path);
        }

        free(cli);
        return NULL;
}

void cli_freep(Cli **mp) {
        if (*mp)
                cli_free(*mp);
}

struct Reply {
        char *error;
        VarlinkObject *parameters;
};

static long reply_callback(VarlinkConnection *connection,
                           const char *error,
                           VarlinkObject *parameters,
                           uint64_t UNUSED(flags),
                           void *userdata) {
        struct Reply *reply = userdata;

        if (error) {
                reply->error = strdup(error);
                if (!reply->error)
                        return -CLI_ERROR_PANIC;
        }

        reply->parameters = varlink_object_ref(parameters);

        varlink_connection_close(connection);

        return 0;
}

long cli_call(Cli *cli,
              VarlinkConnection *connection,
              const char *method,
              VarlinkObject *parameters,
              uint64_t flags,
              char **errorp,
              VarlinkObject **outp) {
        struct Reply reply = {};
        long r;

        r = varlink_connection_call(connection,
                                    method,
                                    parameters,
                                    flags,
                                    reply_callback, &reply);
        if (r < 0)
                return -CLI_ERROR_CALL_FAILED;

        r = cli_process_all_events(cli, connection);
        if (r < 0)
                return r;

        if (errorp)
                *errorp = reply.error;
        else
                free(reply.error);

        if (outp)
                *outp = reply.parameters;
        else
                varlink_object_unref(reply.parameters);

        return 0;
}

long cli_resolve(Cli *cli,
                 const char *interface,
                 char **addressp) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
        _cleanup_(varlink_connection_freep) VarlinkConnection *connection = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *out = NULL;
        _cleanup_(freep) char *error = NULL;
        _cleanup_(freep) char *json = NULL;
        const char *address;
        char *a;
        long r;

        /* Don't resolve the resolver */
        if (strcmp(interface, "org.varlink.resolver") == 0) {
                a = strdup(cli->resolver);
                if (!a)
                        return -CLI_ERROR_PANIC;

                *addressp = a;
                return 0;
        }

        varlink_object_new(&parameters);
        varlink_object_set_string(parameters, "interface", interface);

        r = varlink_connection_new(&connection, cli->resolver);
        if (r < 0)
                return -CLI_ERROR_CANNOT_CONNECT;

        r = cli_call(cli,
                     connection,
                     "org.varlink.resolver.Resolve",
                     parameters,
                     0,
                     &error,
                     &out);
        if (r < 0)
                return r;

        if (error)
                return -CLI_ERROR_CANNOT_RESOLVE;

        r = varlink_object_get_string(out, "address", &address);
        if (r < 0)
                return -CLI_ERROR_CANNOT_RESOLVE;

        a = strdup(address);
        if (!a)
                return -CLI_ERROR_PANIC;

        *addressp = a;
        return 0;
}

long cli_connect(Cli *cli,
                 VarlinkConnection **connectionp,
                 VarlinkURI *uri) {
        _cleanup_(freep) char *address = NULL;
        long r;

        if ((cli->activate || cli->bridge) && (uri && uri->protocol != VARLINK_URI_PROTOCOL_NONE))
                return -CLI_ERROR_CANNOT_CONNECT;

        if (cli->activate) {
                long fd;

                fd = cli_activate(cli->activate, &cli->path, &cli->pid);
                if (fd < 0)
                        return fd;

                return varlink_connection_new_from_fd(connectionp, (int)fd);
        }

        if (cli->bridge) {
                long fd;

                fd = cli_bridge(cli->bridge, &cli->pid);
                if (fd < 0)
                        return fd;

                return varlink_connection_new_from_fd(connectionp, (int)fd);
        }

        if (!uri)
                return -CLI_ERROR_CANNOT_CONNECT;

        if (uri->protocol != VARLINK_URI_PROTOCOL_NONE)
                return varlink_connection_new_from_uri(connectionp, uri);

        r = cli_resolve(cli, uri->interface, &address);
        if (r < 0)
                return r;

        r = varlink_connection_new(connectionp, address);
        if (r < 0)
                return -CLI_ERROR_CANNOT_CONNECT;

        return 0;
}

long cli_process_all_events(Cli *cli, VarlinkConnection *connection) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
        _cleanup_(freep) char *error = NULL;
        long r;

        if (varlink_connection_get_events(connection) == 0)
                return 0;

        r = epoll_add(cli->epoll_fd,
                      varlink_connection_get_fd(connection),
                      varlink_connection_get_events(connection),
                      connection);
        if (r < 0)
                return -CLI_ERROR_PANIC;

        for (;;) {
                struct epoll_event ev;

                r = epoll_wait(cli->epoll_fd, &ev, 1, cli->timeout);
                if (r < 0) {
                        if (errno == EINTR)
                                continue;

                        return -CLI_ERROR_PANIC;
                }

                if (r == 0)
                        return -CLI_ERROR_TIMEOUT;

                if (!ev.data.ptr) {
                        struct signalfd_siginfo fdsi;
                        long size;

                        size = read(cli->signal_fd, &fdsi, sizeof(struct signalfd_siginfo));
                        if (size != sizeof(struct signalfd_siginfo))
                                continue;

                        switch (fdsi.ssi_signo) {
                                case SIGTERM:
                                case SIGINT:
                                        return -CLI_ERROR_CANCELED;

                                case SIGPIPE:
                                        return -CLI_ERROR_CALL_FAILED;

                                default:
                                        return -CLI_ERROR_PANIC;
                        }

                } else if (ev.data.ptr == connection) {
                        r = varlink_connection_process_events(connection, ev.events);
                        switch (r) {
                                case 0:
                                        break;

                                case -VARLINK_ERROR_CONNECTION_CLOSED:
                                        return -CLI_ERROR_CONNECTION_CLOSED;

                                case -VARLINK_ERROR_INVALID_MESSAGE:
                                        return -CLI_ERROR_INVALID_MESSAGE;

                                default:
                                        return -CLI_ERROR_PANIC;
                        }

                        if (varlink_connection_is_closed(connection))
                                break;

                        r = epoll_mod(cli->epoll_fd,
                                      varlink_connection_get_fd(connection),
                                      varlink_connection_get_events(connection),
                                      connection);
                        if (r < 0)
                                return -CLI_ERROR_PANIC;

                } else
                        return -CLI_ERROR_PANIC;
        }

        return 0;
}

static const CliCommand *cli_get_command(Cli *UNUSED(cli), const char *name) {
        for (unsigned long i = 0; cli_commands[i]; i += 1) {
                if (strcmp(name, cli_commands[i]->name) == 0)
                        return cli_commands[i];
        }

        return NULL;
}

static long cli_parse_arguments(int argc, char **argv, CliArguments *arguments) {
        int c;

        opterr = 0;

        while ((c = getopt_long(argc, argv, "+A:b:ht:R:V", cli_options, NULL)) >= 0) {
                switch (c) {
                        case 'A':
                                arguments->activate = optarg;
                                break;

                        case 'b':
                                arguments->bridge = optarg;
                                break;

                        case 'h':
                                arguments->help = true;
                                break;

                        case 't':
                                arguments->timeout = strtoul(optarg, NULL, 0) * 1000;
                                break;

                        case 'R':
                                arguments->resolver = optarg;
                                break;

                        case 'V':
                                arguments->version = true;
                                break;

                        case '?':
                                return -CLI_ERROR_INVALID_ARGUMENT;

                        case ':':
                                return -CLI_ERROR_MISSING_ARGUMENT;

                        default:
                                return -CLI_ERROR_PANIC;
                }
        }

        if (arguments->activate && arguments->bridge)
                return -CLI_ERROR_INVALID_ARGUMENT;

        arguments->command = argv[optind];
        arguments->remaining_argc = argc - optind;
        arguments->remaining_argv = argv + optind;
        optind = 0;

        return 0;
}

long cli_run(Cli *cli, int argc, char **argv) {
        CliArguments arguments = {
                .timeout = -1
        };
        const CliCommand *command;
        long r;

        r = cli_parse_arguments(argc, argv, &arguments);
        if (r < 0)
                return r;

        if (arguments.activate)
                cli->activate = arguments.activate;

        if (arguments.bridge)
                cli->bridge = arguments.bridge;

        if (arguments.resolver)
                cli->resolver = arguments.resolver;

        if (arguments.timeout != -1)
                cli->timeout = arguments.timeout;

        if (arguments.help) {
                printf("Usage: %s COMMAND [OPTIONS]...\n", program_invocation_short_name);
                printf("\n");
                printf("  -A, --activate=COMMAND Service to socket-activate and connect to\n");
                printf("                         The temporary UNIX socket address is\n");
                printf("                         exported as $VARLINK_ADDRESS.\n");
                printf("  -b, --bridge=COMMAND   Command to execute and connect to\n");
                printf("  -h, --help             Display this help text and exit\n");
                printf("  -t, --timeout=SECONDS  Time in seconds to wait for a reply\n");
                printf("  -R, --resolver=ADDRESS Address of the resolver\n");
                printf("  -V, --version          Output version information and exit\n");
                printf("\n");
                printf("Commands:\n");
                for (unsigned long i = 0; cli_commands[i]; i += 1)
                        printf("  %-16.16s %s\n", cli_commands[i]->name, cli_commands[i]->info);
                printf("\n");
                printf("Errors:\n");
                for (long i = 1 ; i < CLI_ERROR_MAX; i += 1)
                        printf(" %3li %s\n", i, cli_error_string(i));
                printf("\n");
                return 0;
        }

        if (arguments.version) {
                printf(VERSION "\n");
                return 0;
        }

        if (!arguments.command) {
                fprintf(stderr, "Usage: %s COMMAND [OPTIONS]\n", program_invocation_short_name);
                fprintf(stderr, "Try '%s --help' for more information\n", program_invocation_short_name);
                return -CLI_ERROR_COMMAND_NOT_FOUND;
        }

        command = cli_get_command(cli, arguments.command);
        if (!command) {
                fprintf(stderr, "%s: '%s' is not a valid command.\n", program_invocation_short_name, arguments.command);
                fprintf(stderr, "Try '%s --help' for more information\n", program_invocation_short_name);
                return -CLI_ERROR_COMMAND_NOT_FOUND;
        }

        return command->run(cli, arguments.remaining_argc, arguments.remaining_argv);
}

void cli_print_completion(const char *current, const char *format, ...) {
        va_list args;
        _cleanup_(freep) char *word = NULL;

        va_start(args, format);
        if (vasprintf(&word, format, args) < 0)
                abort();
        va_end(args);

        if (strncmp(word, current, strlen(current)) != 0)
                return;

        printf("%s\n", word);
}

long cli_complete(Cli *cli, int argc, char **argv, const char *current) {
        CliArguments arguments = {
                .timeout = -1
        };
        int r;

        r = cli_parse_arguments(argc, argv, &arguments);
        if (r < 0) {
                if (r == -CLI_ERROR_INVALID_ARGUMENT)
                        return 0;

                return -CLI_ERROR_PANIC;
        }

        if (arguments.resolver)
                cli->resolver = arguments.resolver;

        if (arguments.timeout != -1)
                cli->timeout = arguments.timeout;

        if (arguments.command) {
                const CliCommand *command;

                command = cli_get_command(cli, arguments.command);
                if (command) {
                        if (!command->complete)
                                return 0;

                        return command->complete(cli, arguments.remaining_argc, arguments.remaining_argv, current);
                }
        }

        if (current[0] == '-') {
                if (current[strlen(current) - 1] == '=')
                        return 0;

                for (const struct option *option = cli_options; option->name; option += 1)
                        cli_print_completion(current, "--%s%s", option->name, option->has_arg ? "=" : "");

        } else {
                for (unsigned long i = 0; cli_commands[i]; i += 1)
                        cli_print_completion(current, "%s", cli_commands[i]->name);
        }

        return 0;
}

long cli_complete_options(Cli *UNUSED(cli), const struct option *options, const char *current) {
        for (const struct option *option = options; option->name; option += 1)
                cli_print_completion(current, "--%s%s", option->name, option->has_arg ? "=" : "");

        return true;
}

long cli_complete_interfaces(Cli *cli, const char *current, bool end_with_dot) {
        _cleanup_(varlink_connection_freep) VarlinkConnection *connection = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *out = NULL;
        _cleanup_(freep) char *error = NULL;
        VarlinkArray *interfaces;
        long n_interfaces;
        long r;

        r = varlink_connection_new(&connection, cli->resolver);
        if (r < 0)
                return -CLI_ERROR_CANNOT_CONNECT;

        r = cli_call(cli,
                     connection,
                     "org.varlink.resolver.GetInfo",
                     NULL,
                     0,
                     &error,
                     &out);
        if (r < 0)
                return -r;

        if (error)
                return -CLI_ERROR_CALL_FAILED;

        r = varlink_object_get_array(out, "interfaces", &interfaces);
        if (r < 0)
                return -CLI_ERROR_INVALID_MESSAGE;

        n_interfaces = varlink_array_get_n_elements(interfaces);
        for (long i = 0; i < n_interfaces; i += 1) {
                const char *interface;

                varlink_array_get_string(interfaces, i, &interface);
                cli_print_completion(current, "%s%s", interface, end_with_dot ? "." : "");
        }

        return 0;
}

long cli_complete_methods(Cli *cli, const char *current) {
        _cleanup_(varlink_uri_freep) VarlinkURI *uri = NULL;
        _cleanup_(varlink_connection_freep) VarlinkConnection *connection = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *out = NULL;
        _cleanup_(varlink_interface_freep) VarlinkInterface *interface = NULL;
        _cleanup_(freep) char *error = NULL;
        const char *description = NULL;
        long r;

        r = varlink_uri_new(&uri, current, true);
        if (r < 0 || !uri->interface)
                return cli_complete_interfaces(cli, current, true);

        r = cli_connect(cli, &connection, uri);
        if (r < 0)
                return cli_complete_interfaces(cli, current, true);

        varlink_object_new(&parameters);
        varlink_object_set_string(parameters, "interface", uri->interface);

        r = cli_call(cli,
                     connection,
                     "org.varlink.service.GetInterfaceDescription",
                     parameters,
                     0,
                     &error,
                     &out);
        if (r < 0)
                return r;

        if (error)
                return -CLI_ERROR_REMOTE_ERROR;

        if (varlink_object_get_string(out, "description", &description) < 0)
                return -CLI_ERROR_CALL_FAILED;

        r = varlink_interface_new(&interface, description, NULL);
        if (r < 0)
                return -CLI_ERROR_PANIC;

        for (unsigned long i = 0; i < interface->n_members; i += 1) {
                const VarlinkInterfaceMember *member = interface->members[i];

                if (member->type != VARLINK_MEMBER_METHOD)
                        continue;

                cli_print_completion(current, "%s.%s", uri->interface, member->name);
        }

        return 0;
}
