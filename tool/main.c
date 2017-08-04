#include "command.h"
#include "util.h"

#include <errno.h>
#include <getopt.h>
#include <string.h>

const Command *commands[] = {
        &command_call,
        &command_error,
        &command_format,
        &command_help,
        &command_list,
        &command_resolve
};

static long lookup_command(VarlinkCli *cli, const char **cmdp, CommandFunction *commandp) {
        const char *cmd;
        static const struct option options[] = {
                { "help",    no_argument, NULL, 'h' },
                { "resolver", required_argument, NULL, 'R' },
                { "version", no_argument, NULL, 'V' },
                {}
        };
        int c;

        while ((c = getopt_long(cli->argc, cli->argv, "+hR:V", options, NULL)) >= 0) {
                switch (c) {
                        case 'h':
                                return 'h';

                        case 'R':
                                cli->resolver = optarg;
                                break;

                        case 'V':
                                return 'V';

                        default:
                                return -CLI_ERROR_PANIC;
                }
        }

        if (!cli->argv[optind])
                return -CLI_ERROR_MISSING_COMMAND;

        cmd = cli->argv[optind];
        *cmdp = cmd;

        cli->argc -= optind;
        cli->argv += optind;
        optind = 0;

        for (unsigned long i = 0; i < ARRAY_SIZE(commands); i += 1) {
                if (strcmp(cmd, commands[i]->name) == 0) {
                        *commandp = commands[i]->function;

                        return 0;
                }
        }

        return -CLI_ERROR_COMMAND_NOT_FOUND;
}

int main(int argc, char **argv) {
        _cleanup_(varlink_cli_freep) VarlinkCli *cli = NULL;
        const char *cmd;
        CommandFunction command;
        long r;

        r = varlink_cli_new(&cli);
        if (r < 0)
                return exit_error(-r);

        cli->argv = argv;
        cli->argc = argc;

        r = lookup_command(cli, &cmd, &command);
        switch (r) {
                case 0:
                        break;

                case 'h':
                        printf("Usage: %s COMMAND [OPTIONS]...\n", program_invocation_short_name);
                        printf("\n");
                        printf("  -h, --help             display this help text and exit\n");
                        printf("  -R, --resolver=ADDRESS address of the resolver\n");
                        printf("  -V, --version          output version information and exit\n");
                        printf("\n");
                        printf("Commands:\n");
                        for (unsigned long i = 0; i < ARRAY_SIZE(commands); i += 1)
                                printf("  %-16.16s %s\n", commands[i]->name, commands[i]->info);
                        printf("\n");
                        printf("Errors:\n");
                        for (long i = 1 ; i < CLI_ERROR_MAX; i += 1)
                                printf(" %3li %s\n", i, error_string(i));
                        printf("\n");
                        return EXIT_SUCCESS;

                case 'V':
                        printf(VERSION "\n");
                        return EXIT_SUCCESS;

                case -CLI_ERROR_MISSING_COMMAND:
                        fprintf(stderr, "Usage: %s COMMAND [OPTIONS]\n", program_invocation_short_name);
                        fprintf(stderr, "Try '%s --help' for more information\n", program_invocation_short_name);
                        return CLI_ERROR_COMMAND_NOT_FOUND;

                case -CLI_ERROR_COMMAND_NOT_FOUND:
                        fprintf(stderr, "%s: '%s' is not a valid command.\n", program_invocation_short_name, cmd);
                        fprintf(stderr, "Try '%s --help' for more information\n", program_invocation_short_name);
                        return CLI_ERROR_COMMAND_NOT_FOUND;

                default:
                        return exit_error(CLI_ERROR_PANIC);
        }

        return command(cli);
}
