#pragma once

#include "cli.h"

typedef long (*CommandFunction)(Cli *cli);

typedef struct {
        const char *name;
        const char *info;
        CommandFunction function;
} CliCommand;

extern const CliCommand *cli_commands[];
