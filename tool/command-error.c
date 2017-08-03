#include "command.h"
#include "error.h"
#include "util.h"

#include <errno.h>
#include <getopt.h>
#include <string.h>

static long error(VarlinkCli *cli) {
        static const struct option options[] = {
                { "help",    no_argument,       NULL, 'h' },
                {}
        };
        int c;
        const char *arg;

        while ((c = getopt_long(cli->argc, cli->argv, "h", options, NULL)) >= 0) {
                switch (c) {
                        case 'h':
                                printf("Usage: %s error [NUMBER/STRING]\n", program_invocation_short_name);
                                printf("\n");
                                printf("Display and resolve libvarlink error code and string.\n");
                                printf("\n");
                                printf("  -h, --help             display this help text and exit\n");
                                return EXIT_SUCCESS;

                        default:
                                return exit_error(CLI_ERROR_PANIC);
                }
        }

        arg = cli->argv[optind];
        if (arg) {
                long n;
                char *endptr;

                n = strtol(arg, &endptr, 0);
                if (endptr[0] == '\0') {
                        printf("%s\n", varlink_error_string(n));
                        return EXIT_SUCCESS;
                }

                for (long i = 1 ; i < CLI_ERROR_MAX; i += 1) {
                        if (strcmp(arg, varlink_error_string(i)) != 0)
                                continue;

                        printf("%li\n", i);
                        return EXIT_SUCCESS;
                }

                return exit_error(CLI_ERROR_INVALID_ARGUMENT);

        }

        for (long i = 1 ; i < VARLINK_ERROR_MAX; i += 1)
                printf(" %3li %s\n", i, varlink_error_string(i));

        return EXIT_SUCCESS;
}

const Command command_error = {
        .name = "error",
        .info = "Print the error codes and strings of libvarlink",
        .function = error
};
