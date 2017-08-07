#pragma once

#include <varlink.h>

enum {
        CLI_ERROR_PANIC = 1,
        CLI_ERROR_CANNOT_RESOLVE,
        CLI_ERROR_MISSING_COMMAND,
        CLI_ERROR_COMMAND_NOT_FOUND,
        CLI_ERROR_MISSING_ARGUMENT,
        CLI_ERROR_INVALID_ARGUMENT,
        CLI_ERROR_INVALID_JSON,
        CLI_ERROR_CANNOT_CONNECT,
        CLI_ERROR_TIMEOUT,
        CLI_ERROR_CANCELED,
        CLI_ERROR_CALL_FAILED,
        CLI_ERROR_REMOTE_ERROR,
        CLI_ERROR_MAX
};

typedef struct {
        int argc;
        char **argv;
        const char *resolver;

        VarlinkConnection *connection;
        int epoll_fd;
        int signal_fd;
} Cli;

const char *cli_error_string(long error);
long cli_exit_error(long error);

long cli_new(Cli **clip);
Cli *cli_free(Cli *cli);
void cli_freep(Cli **clip);
long cli_connect(Cli *cli, const char *address);
long cli_resolve(Cli *cli, const char *interface, char **addressp);
long cli_disconnect(Cli *cli);
long cli_call(Cli *cli, const char *qualified_method, VarlinkObject *parameters, long flags);
long cli_wait_reply(Cli *cli, VarlinkObject **replyp, char **errorp, long *flagsp);
