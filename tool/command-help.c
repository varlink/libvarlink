#include "command.h"
#include "interface.h"
#include "object.h"
#include "terminal-colors.h"
#include "util.h"

#include <errno.h>
#include <getopt.h>
#include <string.h>

static long print_service(Cli *cli) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *info = NULL;
        _cleanup_(freep) char *error = NULL;
        const char *name;
        VarlinkArray *interfaces;
        VarlinkObject *properties;
        unsigned long n_interfaces;
        long r;

        r = cli_call(cli, "org.varlink.service.GetInfo", NULL, 0);
        if (r < 0)
                return r;

        r = cli_wait_reply(cli, &info, &error, NULL);
        if (r < 0)
                return r;

        if (error) {
                printf("Error: %s\n", error);

                return 0;
        }

        if (varlink_object_get_string(info, "name", &name) < 0 ||
            varlink_object_get_array(info, "interfaces", &interfaces))
                return -CLI_ERROR_CALL_FAILED;

        if (varlink_object_get_object(info, "properties", &properties) < 0)
                properties = NULL;

        printf("%sService: %s%s%s\n",
               terminal_color(TERMINAL_BOLD),
               terminal_color(TERMINAL_GRAY_BOLD),
               name,
               terminal_color(TERMINAL_NORMAL));

        if (properties) {
                _cleanup_(freep) char *properties_json = NULL;

                printf("%sProperties: %s",
                       terminal_color(TERMINAL_BOLD),
                       terminal_color(TERMINAL_NORMAL));

                r = varlink_object_to_pretty_json(properties,
                                                  &properties_json,
                                                  0,
                                                  terminal_color(TERMINAL_CYAN),
                                                  terminal_color(TERMINAL_NORMAL),
                                                  terminal_color(TERMINAL_MAGENTA),
                                                  terminal_color(TERMINAL_NORMAL));
                if (r < 0)
                        return -CLI_ERROR_PANIC;

                printf("%s\n", properties_json);
        }

        printf("%sInterfaces:%s\n",
               terminal_color(TERMINAL_BOLD),
               terminal_color(TERMINAL_NORMAL));

        n_interfaces = varlink_array_get_n_elements(interfaces);
        for (unsigned long i = 0; i < n_interfaces; i += 1) {
                const char *interface = NULL;

                varlink_array_get_string(interfaces, i, &interface);
                printf("  %s\n", interface);
        }

        return 0;
}

static long help_interface(Cli *cli, const char *name) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *reply = NULL;
        _cleanup_(freep) char *error = NULL;
        _cleanup_(varlink_interface_freep) VarlinkInterface *interface = NULL;
        const char *interfacestring = NULL;
        _cleanup_(freep) char *string = NULL;
        long r;

        varlink_object_new(&parameters);
        varlink_object_set_string(parameters, "interface", name);
        r = cli_call(cli, "org.varlink.service.GetInterfaceDescription", parameters, 0);
        if (r < 0)
                return r;

        r = cli_wait_reply(cli, &reply, &error, NULL);
        if (r < 0)
                return r;

        if (error) {
                printf("Error: %s\n", error);

                return 0;
        }

        if (varlink_object_get_string(reply, "description", &interfacestring) < 0)
                return -CLI_ERROR_CALL_FAILED;

        r = varlink_interface_new(&interface, interfacestring, NULL);
        if (r < 0)
                return -CLI_ERROR_PANIC;

        r  = varlink_interface_write_interfacestring(interface,
                                                     &string,
                                                     0, 72 - 2,
                                                     terminal_color(TERMINAL_BLUE),
                                                     terminal_color(TERMINAL_NORMAL),
                                                     terminal_color(TERMINAL_MAGENTA),
                                                     terminal_color(TERMINAL_NORMAL),
                                                     terminal_color(TERMINAL_GREEN),
                                                     terminal_color(TERMINAL_NORMAL),
                                                     terminal_color(TERMINAL_CYAN),
                                                     terminal_color(TERMINAL_NORMAL));
        if (r < 0)
                return r;

        printf("%s\n", string);

        return 0;
}

static long help(Cli *cli) {
        static const struct option options[] = {
                { "address", required_argument, NULL, 'a' },
                { "help",    no_argument,       NULL, 'h' },
                {}
        };
        int c;
        const char *topic = NULL;
        _cleanup_(freep) char *address = NULL;
        const char *interface = NULL;
        long r;

        while ((c = getopt_long(cli->argc, cli->argv, "a:h", options, NULL)) >= 0) {
                switch (c) {
                        case 'a':
                                address = strdup(optarg);
                                break;

                        case 'h':
                                printf("Usage: %s help [ INTERFACE | ADDRESS ]\n", program_invocation_short_name);
                                printf("\n");
                                printf("Prints information about INTERFACE or the service at ADDRESS.\n");
                                printf("\n");
                                printf("  -a, --address=ADDRESS  connect to ADDRESS instead of resolving INTERFACE\n");
                                printf("  -h, --help             display this help text and exit\n");
                                return EXIT_SUCCESS;

                        default:
                                fprintf(stderr, "Try '%s --help' for more information\n",
                                        program_invocation_short_name);
                                return EXIT_FAILURE;
                }
        }

        topic = cli->argv[optind];
        if (!topic) {
                fprintf(stderr, "Usage: %s help [ INTERFACE | ADDRESS ]\n", program_invocation_short_name);
                return EXIT_FAILURE;
        }

        if (strchr(topic, ':') != NULL) {
                if (address) {
                        fprintf(stderr, "Error: cannot use --address when the first argument is an\n");
                        fprintf(stderr, "       address instead of an interface.\n");
                        return EXIT_FAILURE;
                }

                address = strdup(topic);
        } else {
                interface = topic;

                if (!address) {
                        r = cli_resolve(cli, interface, &address);
                        if (r < 0) {
                                fprintf(stderr, "Error resolving interface %s: %s\n", interface, strerror(-r));
                                return EXIT_FAILURE;
                        }
                }
        }

        r = cli_connect(cli, address);
        if (r < 0) {
                fprintf(stderr, "Error connecting to %s: %s\n", interface, strerror(-r));
                return EXIT_FAILURE;
        }

        if (interface) {
                r = help_interface(cli, interface);
                if (r < 0)
                        return EXIT_FAILURE;
        } else
                print_service(cli);

        return EXIT_SUCCESS;
}

const CliCommand command_help = {
        .name = "help",
        .info = "Documentation for interfaces and types",
        .function = help
};
