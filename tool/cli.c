#include "cli.h"

#include "command.h"
#include "util.h"

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>

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
        [CLI_ERROR_CALL_FAILED] = "CallFailed"
};

const char *cli_error_string(long error) {
        if (error == 0 || error >= (long)ARRAY_SIZE(error_strings))
                return "<invalid>";

        if (!error_strings[error])
                return "<missing>";

        return error_strings[error];
}

long cli_exit_error(long error) {
        fprintf(stderr, "Error: %s\n", cli_error_string(error));

        return error;
}

long cli_new(Cli **mp) {
        _cleanup_(cli_freep) Cli *cli = NULL;
        sigset_t mask;

        cli = calloc(1, sizeof(Cli));

        cli->resolver = "unix:/run/org.varlink.resolver";

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
        if (cli->connection)
                cli_disconnect(cli);

        if (cli->epoll_fd > 0)
                close(cli->epoll_fd);

        if (cli->signal_fd > 0)
                close(cli->signal_fd);

        free(cli);

        return NULL;
}

void cli_freep(Cli **mp) {
        if (*mp)
                cli_free(*mp);
}

long cli_connect(Cli *cli, const char *address) {
        long r;

        r = varlink_connection_new(&cli->connection, address);
        if (r < 0)
                return -CLI_ERROR_PANIC;

        if (epoll_add(cli->epoll_fd,
                      varlink_connection_get_fd(cli->connection),
                      varlink_connection_get_events(cli->connection),
                      cli->connection) < 0)
                return -CLI_ERROR_PANIC;

        return 0;
}

long cli_resolve(Cli *cli, const char *interface, char **addressp) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *out = NULL;
        _cleanup_(freep) char *error = NULL;
        const char *address;
        long r;

        /* don't resolve the resolver */
        if (strcmp(interface, "org.varlink.resolver") == 0) {
                *addressp = strdup(cli->resolver);
                return 0;
        }

        r = cli_connect(cli, cli->resolver);
        if (r < 0)
                return r;

        varlink_object_new(&parameters);
        varlink_object_set_string(parameters, "interface", interface);

        r = cli_call(cli, "org.varlink.resolver.Resolve", parameters, 0);
        if (r < 0)
                return r;

        r = cli_wait_reply(cli, &out, &error, NULL);
        if (r < 0)
                return r;

        if (error)
                return -CLI_ERROR_CANNOT_RESOLVE;

        r = cli_disconnect(cli);
        if (r < 0)
                return r;

        r = varlink_object_get_string(out, "address", &address);
        if (r < 0)
                return -CLI_ERROR_CANNOT_RESOLVE;

        *addressp = strdup(address);

        return 0;
}

long cli_disconnect(Cli *cli) {
        assert(cli->connection);

        epoll_del(cli->epoll_fd, varlink_connection_get_fd(cli->connection));

        cli->connection = varlink_connection_close(cli->connection);
        return 0;
}

long cli_call(Cli *cli,
              const char *qualified_method,
              VarlinkObject *parameters,
              long flags) {
        long r;

        r = varlink_connection_call(cli->connection, qualified_method, parameters, flags);
        switch (r) {
                default:
                        return CLI_ERROR_PANIC;
        }

        return -CLI_ERROR_PANIC;
}

long cli_wait_reply(Cli *cli,
                    VarlinkObject **parametersp,
                    char **errorp,
                    long *flagsp) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
        _cleanup_(freep) char *error = NULL;
        long r;

        for (;;) {
                struct epoll_event ev;

                r = epoll_mod(cli->epoll_fd,
                              varlink_connection_get_fd(cli->connection),
                              varlink_connection_get_events(cli->connection),
                              cli->connection);
                if (r < 0)
                        return -CLI_ERROR_PANIC;


                r = epoll_wait(cli->epoll_fd, &ev, 1, -1);
                if (r < 0) {
                        if (errno == EINTR)
                                continue;

                        return -CLI_ERROR_PANIC;
                }

                if (r == 0)
                        return -CLI_ERROR_TIMEOUT;

                if (ev.data.ptr == NULL) {
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
                } else if (ev.data.ptr == cli->connection) {
                        r = varlink_connection_process_events(cli->connection, ev.events);
                        if (r < 0)
                                return -CLI_ERROR_PANIC;

                        r = varlink_connection_receive_reply(cli->connection, &parameters, &error, flagsp);
                        if (r < 0)
                                return -CLI_ERROR_PANIC;

                        if (!parameters)
                                continue;
                } else
                        return -CLI_ERROR_PANIC;

                break;
        }

        *parametersp = parameters;
        parameters = NULL;

        if (errorp) {
                *errorp = error;
                error = NULL;
        }

        return 0;
}

static long cli_parse_arguments(Cli *cli, const char **cmdp, CommandFunction *commandp) {
        const char *cmd;
        static const struct option options[] = {
                { "help",    no_argument, NULL, 'h' },
                { "resolver", required_argument, NULL, 'R' },
                { "version", no_argument, NULL, 'V' },
                {}
        };
        int c;

        while ((c = getopt_long(cli->argc, cli->argv, "+hR:V", options, NULL)) >= 0) {
                switch (c) {
                        case 'h':
                                return 'h';

                        case 'R':
                                cli->resolver = optarg;
                                break;

                        case 'V':
                                return 'V';

                        default:
                                return -CLI_ERROR_PANIC;
                }
        }

        if (!cli->argv[optind])
                return -CLI_ERROR_MISSING_COMMAND;

        cmd = cli->argv[optind];
        *cmdp = cmd;

        cli->argc -= optind;
        cli->argv += optind;
        optind = 0;

        for (unsigned long i = 0; cli_commands[i]; i += 1) {
                if (strcmp(cmd, cli_commands[i]->name) == 0) {
                        *commandp = cli_commands[i]->function;

                        return 0;
                }
        }

        return -CLI_ERROR_COMMAND_NOT_FOUND;
}


long cli_run(Cli *cli, int argc, char **argv) {
        const char *cmd;
        CommandFunction command;
        long c;

        cli->argv = argv;
        cli->argc = argc;

        c = cli_parse_arguments(cli, &cmd, &command);
        switch (c) {
                case 0:
                        break;

                case 'h':
                        printf("Usage: %s COMMAND [OPTIONS]...\n", program_invocation_short_name);
                        printf("\n");
                        printf("  -h, --help             display this help text and exit\n");
                        printf("  -R, --resolver=ADDRESS address of the resolver\n");
                        printf("  -V, --version          output version information and exit\n");
                        printf("\n");
                        printf("Commands:\n");
                        for (unsigned long i = 0; cli_commands[i]; i += 1)
                                printf("  %-16.16s %s\n", cli_commands[i]->name, cli_commands[i]->info);
                        printf("\n");
                        printf("Errors:\n");
                        for (long i = 1 ; i < CLI_ERROR_MAX; i += 1)
                                printf(" %3li %s\n", i, cli_error_string(i));
                        printf("\n");
                        return EXIT_SUCCESS;

                case 'V':
                        printf(VERSION "\n");
                        return EXIT_SUCCESS;

                case -CLI_ERROR_MISSING_COMMAND:
                        fprintf(stderr, "Usage: %s COMMAND [OPTIONS]\n", program_invocation_short_name);
                        fprintf(stderr, "Try '%s --help' for more information\n", program_invocation_short_name);
                        return CLI_ERROR_COMMAND_NOT_FOUND;

                case -CLI_ERROR_COMMAND_NOT_FOUND:
                        fprintf(stderr, "%s: '%s' is not a valid command.\n", program_invocation_short_name, cmd);
                        fprintf(stderr, "Try '%s --help' for more information\n", program_invocation_short_name);
                        return CLI_ERROR_COMMAND_NOT_FOUND;

                default:
                        return cli_exit_error(CLI_ERROR_PANIC);
        }

        return command(cli);
}
