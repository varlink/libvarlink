#include "command.h"
#include "interface.h"
#include "object.h"
#include "terminal-colors.h"
#include "util.h"

#include <errno.h>
#include <getopt.h>
#include <string.h>

static long print_registry(Cli *cli, const char *field) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *out = NULL;
        _cleanup_(freep) char *error = NULL;
        VarlinkArray *interfaces;
        long n_interfaces;
        long r;

        r = cli_connect(cli, cli->resolver);
        if (r < 0)
                return r;

        r = cli_call(cli, "org.varlink.resolver.GetInterfaces", NULL, 0);
        if (r < 0)
                return r;

        r = cli_wait_reply(cli, &out, &error, NULL);
        if (r < 0)
                return r;

        if (error)
                return -CLI_ERROR_CANNOT_RESOLVE;

        r = cli_disconnect(cli);
        if (r < 0)
                return r;

        r = varlink_object_get_array(out, "interfaces", &interfaces);
        if (r < 0)
                return -CLI_ERROR_CANNOT_RESOLVE;

        n_interfaces = varlink_array_get_n_elements(interfaces);
        for (long i = 0; i < n_interfaces; i += 1) {
                VarlinkObject *interface;
                const char *s;

                varlink_array_get_object(interfaces, i, &interface);
                varlink_object_get_string(interface, field, &s);
                printf("%s\n", s);
        }

        return 0;
}

static long print_methods(Cli *cli, const char *interface_name) {
        _cleanup_(freep) char *address = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *reply = NULL;
        _cleanup_(freep) char *error = NULL;
        _cleanup_(varlink_interface_freep) VarlinkInterface *interface = NULL;
        const char *interfacestring = NULL;
        _cleanup_(freep) char *string = NULL;
        long r;

        r = cli_resolve(cli, interface_name, &address);
        if (r < 0)
                return r;

        r = cli_connect(cli, address);
        if (r < 0)
                return r;

        varlink_object_new(&parameters);
        varlink_object_set_string(parameters, "interface", interface_name);
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

        for (unsigned long i = 0; i < interface->n_members; i += 1) {
                const VarlinkInterfaceMember *member = &interface->members[i];

                if (member->type != VARLINK_MEMBER_METHOD)
                        continue;

                printf("%s\n", member->name);
        }

        return 0;
}

static long list(Cli *cli, int argc, char **argv) {
        static const struct option options[] = {
                { "help",             no_argument, NULL, 'h' },
                {}
        };
        const char *type = NULL;
        const char *argument = NULL;
        int c;

        while ((c = getopt_long(argc, argv, "h", options, NULL)) >= 0) {
                switch (c) {
                        case 'h':
                                printf("Usage: %s COMMAND [OPTIONS]\n", program_invocation_short_name);
                                printf("\n");
                                printf("  -h, --help             display this help text and exit\n");
                                printf("\n");
                                printf("Commands:\n");
                                printf(" addresses                List all registered addresses\n");
                                printf(" interfaces               List all registered interfaces\n");
                                printf(" methods INTERFACE        List methods of specified interface\n");
                                printf("\n");
                                return EXIT_SUCCESS;

                        default:
                                return cli_exit_error(CLI_ERROR_PANIC);
                }
        }

        type = argv[optind];
        if (!type) {
                fprintf(stderr, "Error: expecting command\n");

                return CLI_ERROR_MISSING_ARGUMENT;
        }

        argument = argv[optind + 1];

        if (strcmp(type, "interfaces") == 0)
                return print_registry(cli, "interface");
        else if (strcmp(type, "addresses") == 0)
                return print_registry(cli, "address");
        else if (strcmp(type, "methods") == 0)
                return print_methods(cli, argument);

        return cli_exit_error(CLI_ERROR_INVALID_ARGUMENT);
}

const CliCommand command_list = {
        .name = "list",
        .info = "List interfaces, addresses",
        .run = list
};
