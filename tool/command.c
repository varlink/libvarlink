#include "command.h"

extern const CliCommand command_call;
extern const CliCommand command_error;
extern const CliCommand command_format;
extern const CliCommand command_help;
extern const CliCommand command_list;
extern const CliCommand command_resolve;

const CliCommand *cli_commands[] = {
        &command_call,
        &command_error,
        &command_format,
        &command_help,
        &command_list,
        &command_resolve,
        NULL
};
