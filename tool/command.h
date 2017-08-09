#pragma once

#include "cli.h"

typedef long (*CommandRunFunction)(Cli *cli, int argc, char **argv);
typedef long (*CommandCompleteFunction)(Cli *cli, int argc, char **argv, const char *current);

typedef struct {
        const char *name;
        const char *info;
        CommandRunFunction run;
        CommandCompleteFunction complete;
} CliCommand;

extern const CliCommand *cli_commands[];
