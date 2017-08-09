#include "command.h"

extern const CliCommand command_call;
extern const CliCommand command_complete;
extern const CliCommand command_error;
extern const CliCommand command_format;
extern const CliCommand command_help;
extern const CliCommand command_resolve;

const CliCommand *cli_commands[] = {
        &command_call,
        &command_complete,
        &command_error,
        &command_format,
        &command_help,
        &command_resolve,
        NULL
};
