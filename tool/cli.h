#pragma once

#include <getopt.h>
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
        CLI_ERROR_INVALID_MESSAGE,
        CLI_ERROR_MAX
};

typedef struct {
        const char *resolver;

        int epoll_fd;
        int signal_fd;
} Cli;

const char *cli_error_string(long error);
long cli_exit_error(long error);

long cli_new(Cli **clip);
Cli *cli_free(Cli *cli);
void cli_freep(Cli **clip);
long cli_resolve(Cli *cli, const char *interface, char **addressp);
long cli_call(Cli *cli, const char *method, VarlinkObject *parameters, char **errorp, VarlinkObject **outp);
long cli_process_all_events(Cli *cli, VarlinkConnection *connection);
long cli_run(Cli *cli, int argc, char **argv);
long cli_complete(Cli *cli, int argc, char **argv, const char *current);

long cli_complete_options(Cli *cli, const struct option *options, const char *current);
long cli_complete_interfaces(Cli *cli, const char *current, bool end_with_dot);
long cli_complete_addresses(Cli *cli, const char *current);
long cli_complete_qualified_methods(Cli *cli, const char *current);

__attribute__ ((format (printf, 2, 3)))
void cli_print_completion(const char *current, const char *format, ...);

long cli_split_address(const char *identifier, char **addressp, const char **methodp);
