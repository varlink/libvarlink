#pragma once

#include "cli.h"

typedef long (*CommandFunction)(Cli *cli);

typedef struct Command {
        const char *name;
        const char *info;
        CommandFunction function;
} CliCommand;

extern const CliCommand command_call;
extern const CliCommand command_error;
extern const CliCommand command_format;
extern const CliCommand command_help;
extern const CliCommand command_list;
extern const CliCommand command_resolve;
