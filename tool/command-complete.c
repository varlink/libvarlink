#include "command.h"
#include "interface.h"
#include "object.h"
#include "util.h"

#include <errno.h>
#include <getopt.h>
#include <string.h>

static long complete_run(Cli *cli, int argc, char **argv) {
        char *endptr;
        int argindex;
        const char *current = NULL;

        if (argc < 3)
                return -CLI_ERROR_MISSING_ARGUMENT;

        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
                printf("Usage: %s complete INDEX -- ARGUMENTS\n", program_invocation_short_name);
                printf("\n");
                printf("  -h, --help             display this help text and exit\n");
                printf("\n");
                return 0;
        }

        argindex = strtoul(argv[1], &endptr, 0);
        if (endptr == argv[1] || endptr[0] != '\0')
                return -CLI_ERROR_INVALID_ARGUMENT;

        if (strcmp(argv[2], "--") != 0)
                return -CLI_ERROR_INVALID_ARGUMENT;

        argc -= 3;
        argv += 3;

        if (argindex == 0 || argindex > argc)
                return -CLI_ERROR_INVALID_ARGUMENT;

        /* don't care about arguments after the one that shall be completed */
        current = argv[argindex] ?: "",
        argc = argindex;
        argv[argc] = NULL;

        cli_complete(cli, argc, argv, current);

        return 0;
}

const CliCommand command_complete = {
        .name = "complete",
        .info = "Provide suggestions for command line completion",
        .run = complete_run
};
