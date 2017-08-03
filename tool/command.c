#include "command.h"
#include "util.h"

#include <assert.h>
#include <errno.h>
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

const char *error_string(long error) {
        if (error == 0 || error >= (long)ARRAY_SIZE(error_strings))
                return "<invalid>";

        if (!error_strings[error])
                return "<missing>";

        return error_strings[error];
}

long exit_error(long error) {
        fprintf(stderr, "Error: %s\n", error_string(error));

        return error;
}

struct VarlinkCli {
        char *address;
        VarlinkConnection *connection;

        int epoll_fd;
        int signal_fd;
};

long varlink_cli_new(VarlinkCli **mp) {
        _cleanup_(varlink_cli_freep) VarlinkCli *cli = NULL;
        sigset_t mask;

        cli = calloc(1, sizeof(VarlinkCli));

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

VarlinkCli *varlink_cli_free(VarlinkCli *cli) {
        if (cli->connection)
                varlink_cli_disconnect(cli);

        if (cli->epoll_fd > 0)
                close(cli->epoll_fd);

        if (cli->signal_fd > 0)
                close(cli->signal_fd);

        free(cli);

        return NULL;
}

void varlink_cli_freep(VarlinkCli **mp) {
        if (*mp)
                varlink_cli_free(*mp);
}

long varlink_cli_connect(VarlinkCli *cli, const char *address) {
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

long varlink_cli_resolve(VarlinkCli *cli, const char *interface, char **addressp) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *out = NULL;
        _cleanup_(freep) char *error = NULL;
        const char *address;
        long r;

        /* don't resolve the resolver */
        if (strcmp(interface, "org.varlink.resolver") == 0) {
                *addressp = strdup("unix:/run/org.varlink.resolver");
                return 0;
        }

        r = varlink_cli_connect(cli, "unix:/run/org.varlink.resolver");
        if (r < 0)
                return r;

        varlink_object_new(&parameters);
        varlink_object_set_string(parameters, "interface", interface);

        r = varlink_cli_call(cli, "org.varlink.resolver.Resolve", parameters, 0);
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

        r = varlink_object_get_string(out, "address", &address);
        if (r < 0)
                return -CLI_ERROR_CANNOT_RESOLVE;

        *addressp = strdup(address);

        return 0;
}

long varlink_cli_disconnect(VarlinkCli *cli) {
        assert(cli->connection);

        epoll_del(cli->epoll_fd, varlink_connection_get_fd(cli->connection));

        cli->connection = varlink_connection_close(cli->connection);
        return 0;
}

long varlink_cli_call(VarlinkCli *cli,
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

long varlink_cli_wait_reply(VarlinkCli *cli,
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
