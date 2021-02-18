// SPDX-License-Identifier: Apache-2.0

#include "command.h"

extern const CliCommand command_bridge;
extern const CliCommand command_call;
extern const CliCommand command_complete;
extern const CliCommand command_format;
extern const CliCommand command_help;
extern const CliCommand command_info;
extern const CliCommand command_resolve;

const CliCommand *cli_commands[] = {
        &command_bridge,
        &command_call,
        &command_complete,
        &command_format,
        &command_help,
        &command_info,
        &command_resolve,
        NULL
};
