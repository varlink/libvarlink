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

const char *error_string(long error);
long exit_error(long error);

typedef struct {
        int argc;
        char **argv;
        const char *resolver;

        VarlinkConnection *connection;
        int epoll_fd;
        int signal_fd;
} VarlinkCli;

typedef struct Command Command;
typedef long (*CommandFunction)(VarlinkCli *cli);

struct Command {
        const char *name;
        const char *info;
        CommandFunction function;
};

long varlink_cli_new(VarlinkCli **clip);
VarlinkCli *varlink_cli_free(VarlinkCli *cli);
void varlink_cli_freep(VarlinkCli **clip);
long varlink_cli_connect(VarlinkCli *cli, const char *address);
long varlink_cli_resolve(VarlinkCli *cli, const char *interface, char **addressp);
long varlink_cli_disconnect(VarlinkCli *cli);
long varlink_cli_call(VarlinkCli *cli, const char *qualified_method, VarlinkObject *parameters, long flags);
long varlink_cli_wait_reply(VarlinkCli *cli, VarlinkObject **replyp, char **errorp, long *flagsp);

extern const Command command_call;
extern const Command command_error;
extern const Command command_format;
extern const Command command_help;
extern const Command command_list;
extern const Command command_resolve;
