#include "command.h"
#include "error.h"
#include "terminal-colors.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>

static long print_service(Cli *cli, const char *address) {
        _cleanup_(varlink_connection_freep) VarlinkConnection *connection = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *info = NULL;
        _cleanup_(freep) char *error = NULL;
        const char *str;
        VarlinkArray *interfaces;
        unsigned long n_interfaces;
        long r;

        r = varlink_connection_new(&connection, address);
        if (r < 0) {
                fprintf(stderr, "Unable to connect: %s\n", varlink_error_string(-r));
                return r;
        }

        r = cli_call(cli,
                     connection,
                     "org.varlink.service.GetInfo",
                     NULL,
                     0,
                     &error,
                     &info);
        if (r < 0) {
                fprintf(stderr, "Unable to call method: %s\n", cli_error_string(-r));
                return r;
        }

        if (error) {
                fprintf(stderr, "Call failed with error: %s\n", error);
                return -CLI_ERROR_REMOTE_ERROR;
        }

        if (varlink_object_get_string(info, "vendor", &str) >= 0)
                printf("%sVendor:%s %s\n",
                       TERMINAL_BOLD,
                       TERMINAL_NORMAL,
                       str);

        if (varlink_object_get_string(info, "product", &str) >= 0)
                printf("%sProduct:%s %s\n",
                       TERMINAL_BOLD,
                       TERMINAL_NORMAL,
                       str);

        if (varlink_object_get_string(info, "version", &str) >= 0)
                printf("%sVersion:%s %s\n",
                       TERMINAL_BOLD,
                       TERMINAL_NORMAL,
                       str);

        if (varlink_object_get_string(info, "url", &str) >= 0)
                printf("%sURL:%s %s\n",
                       TERMINAL_BOLD,
                       TERMINAL_NORMAL,
                       str);

        if (varlink_object_get_array(info, "interfaces", &interfaces) < 0) {
                fprintf(stderr, "Unable to parse reply\n");
                return -CLI_ERROR_INVALID_MESSAGE;
        }

        printf("%sInterfaces:%s\n",
               terminal_color(TERMINAL_BOLD),
               terminal_color(TERMINAL_NORMAL));

        n_interfaces = varlink_array_get_n_elements(interfaces);
        for (unsigned long i = 0; i < n_interfaces; i += 1) {
                const char *interface;

                if (varlink_array_get_string(interfaces, i, &interface) < 0) {
                        fprintf(stderr, "Unable to parse reply\n");
                        return -CLI_ERROR_INVALID_MESSAGE;
                }

                printf("  %s\n", interface);
        }

        printf("\n");

        return 0;
}

static long info_run(Cli *cli, int argc, char **argv) {
        static const struct option options[] = {
                { "help",    no_argument,       NULL, 'h' },
                {}
        };
        const char *address = NULL;
        int c;
        long r;

        while ((c = getopt_long(argc, argv, "h", options, NULL)) >= 0) {
                switch (c) {
                        case 'h':
                                printf("Usage: %s info ADDRESS\n", program_invocation_short_name);
                                printf("\n");
                                printf("Prints information about the service running at ADDRESS.\n");
                                printf("\n");
                                printf("  -h, --help             display this help text and exit\n");
                                return 0;

                        default:
                                fprintf(stderr, "Try '%s --help' for more information\n",
                                        program_invocation_short_name);
                                return -CLI_ERROR_INVALID_ARGUMENT;
                }
        }

        address = argv[optind];
        if (!address) {
                fprintf(stderr, "Usage: %s info ADDRESS\n", program_invocation_short_name);
                return -CLI_ERROR_MISSING_ARGUMENT;
        }

        r = print_service(cli, address);
        if (r < 0)
                return r;

        return 0;
}

static long info_complete(Cli *cli, int argc, char **argv, const char *current) {
        _cleanup_(freep) char *prefix = NULL;
        DIR *dir;
        char *p;

        if (argc != 1)
                return 0;

        p = strrchr(current, '/');
        if (p) {
                if (strncmp(current, "unix:", 5) != 0)
                        return 0;

                prefix = strndup(current + 5, p - current + 1 - 5);
                if (!prefix)
                        return -CLI_ERROR_PANIC;

                dir = opendir(prefix);
        } else
                dir = opendir(".");
        if (!dir)
                return 0;

        for (struct dirent *d = readdir(dir); d; d = readdir(dir)) {
                if (d->d_name[0] == '.')
                        continue;

                switch (d->d_type) {
                        case DT_DIR:
                                cli_print_completion(current, "unix:%s%s/", prefix ?: "", d->d_name);
                                break;

                        case DT_SOCK:
                                cli_print_completion(current, "unix:%s%s", prefix ?: "", d->d_name);
                                break;
                }
        }

        closedir(dir);

        /* Always add the root directory, the current one might be empty. */
        cli_print_completion(current, "unix:/");

        return 0;
}

const CliCommand command_info = {
        .name = "info",
        .info = "Print information about a service",
        .run = info_run,
        .complete = info_complete
};
