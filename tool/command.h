#pragma once

#include "cli.h"

typedef long (*CommandRunFunction)(Cli *cli, int argc, char **argv);

typedef struct {
        const char *name;
        const char *info;
        CommandRunFunction run;
} CliCommand;

extern const CliCommand *cli_commands[];
