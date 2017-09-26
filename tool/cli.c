#include "cli.h"

#include "command.h"
#include "connection.h"
#include "interface.h"
#include "util.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>

static const char *error_strings[] = {
        [CLI_ERROR_PANIC] = "Panic",
        [CLI_ERROR_CANNOT_RESOLVE] = "CannotResolve",
        [CLI_ERROR_MISSING_COMMAND] = "MissingCommand",
        [CLI_ERROR_COMMAND_NOT_FOUND] = "CommandNotFound",
        [CLI_ERROR_MISSING_ARGUMENT] = "MissingArgument",
        [CLI_ERROR_INVALID_ARGUMENT] = "InvalidArgument",
        [CLI_ERROR_INVALID_JSON] = "InvalidJson",
        [CLI_ERROR_CANNOT_CONNECT] = "CannotConnect",
        [CLI_ERROR_TIMEOUT] = "Timeout",
        [CLI_ERROR_CANCELED] = "Canceled",
        [CLI_ERROR_REMOTE_ERROR] = "RemoteError",
        [CLI_ERROR_CALL_FAILED] = "CallFailed",
        [CLI_ERROR_INVALID_MESSAGE] = "InvalidMessage",
        [CLI_ERROR_CONNECTION_CLOSED] = "ConnectionClosed",
};

static const struct option cli_options[] = {
        { "help",     no_argument,       NULL, 'h' },
        { "resolver", required_argument, NULL, 'R' },
        { "timeout",  required_argument, NULL, 't' },
        { "version",  no_argument,       NULL, 'V' },
        {}
};

typedef struct {
        bool help;
        bool version;
        const char *resolver;
        long timeout;

        const char *command;
        int remaining_argc;
        char **remaining_argv;
} CliArguments;

const char *cli_error_string(long error) {
        if (error == 0 || error >= (long)ARRAY_SIZE(error_strings))
                return "<invalid>";

        if (!error_strings[error])
                return "<missing>";

        return error_strings[error];
}

long cli_new(Cli **mp) {
        _cleanup_(cli_freep) Cli *cli = NULL;
        sigset_t mask;

        cli = calloc(1, sizeof(Cli));
        cli->resolver = "/run/org.varlink.resolver";
        cli->timeout = -1;

        cli->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
        if (cli->epoll_fd < 0)
                return -CLI_ERROR_PANIC;

        sigemptyset(&mask);
        sigaddset(&mask, SIGTERM);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGPIPE);
        sigprocmask(SIG_BLOCK, &mask, NULL);

        cli->signal_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
        if (cli->signal_fd < 0)
                return -CLI_ERROR_PANIC;

        if (epoll_add(cli->epoll_fd, cli->signal_fd, EPOLLIN, NULL) < 0)
                return -CLI_ERROR_PANIC;

        *mp = cli;
        cli = NULL;

        return 0;
}

Cli *cli_free(Cli *cli) {
        if (cli->epoll_fd > 0)
                close(cli->epoll_fd);

        if (cli->signal_fd > 0)
                close(cli->signal_fd);

        free(cli);

        return NULL;
}

void cli_freep(Cli **mp) {
        if (*mp)
                cli_free(*mp);
}

static long cli_call(Cli *cli,
                     const char *method_identifier,
                     VarlinkObject *parameters,
                     char **errorp,
                     VarlinkObject **outp) {
        _cleanup_(freep) char *address = NULL;
        _cleanup_(freep) char *method = NULL;
        long r;

        r = cli_parse_url(method_identifier,
                          NULL,
                          &address,
                          NULL,
                          &method);
        if (r < 0)
                return r;

        if (!address) {
                _cleanup_(freep) char *interface = NULL;

                r = varlink_interface_parse_qualified_name(method, &interface, NULL);
                if (r < 0)
                        return -CLI_ERROR_INVALID_ARGUMENT;

                r = cli_resolve(cli, interface, &address);
                if (r < 0)
                        return -CLI_ERROR_CANNOT_RESOLVE;
        }

        return cli_call_on_address(cli, address, method, parameters, errorp, outp);
}

long cli_resolve(Cli *cli, const char *interface, char **addressp) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *out = NULL;
        _cleanup_(freep) char *error = NULL;
        _cleanup_(freep) char *json = NULL;
        const char *address;
        long r;

        /* Don't resolve the resolver */
        if (strcmp(interface, "org.varlink.resolver") == 0) {
                *addressp = strdup(cli->resolver);
                return 0;
        }

        varlink_object_new(&parameters);
        varlink_object_set_string(parameters, "interface", interface);

        r = cli_call(cli, "org.varlink.resolver.Resolve", parameters, &error, &out);
        if (r < 0)
                return r;

        if (error)
                return -CLI_ERROR_CANNOT_RESOLVE;

        r = varlink_object_get_string(out, "address", &address);
        if (r < 0)
                return -CLI_ERROR_CANNOT_RESOLVE;

        *addressp = strdup(address);

        return 0;
}

struct Reply {
        char *error;
        VarlinkObject *parameters;
};

static void reply_callback(VarlinkConnection *connection,
                           const char *error,
                           VarlinkObject *parameters,
                           uint64_t flags,
                           void *userdata) {
        struct Reply *reply = userdata;

        if (error)
                reply->error = strdup(error);

        reply->parameters = varlink_object_ref(parameters);

        varlink_connection_close(connection);
}

static long cli_connection_new_ssh(VarlinkConnection **connectionp, const char *host, unsigned int port) {
        int sp[2];
        pid_t pid;
        long r;

        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0)
                return -CLI_ERROR_PANIC;

        pid = fork();
        if (pid < 0) {
                close(sp[0]);
                close(sp[1]);
                return -CLI_ERROR_PANIC;
        }

        if (pid == 0) {
                const char *arg[11];
                long i = 0;

                arg[i++] = "ssh";

                /* Disable X11 and pseudo-terminal */
                arg[i++] = "-xT";

                /* Disable passphrase/password querying */
                arg[i++] = "-o";
                arg[i++] = "BatchMode=yes";

                /* Add custom port number */
                if (port > 0) {
                        char p[8];

                        sprintf(p, "%d", port);
                        arg[i++] = "-p";
                        arg[i++] = p;
                }

                arg[i++] = "--";
                arg[i++] = host;
                arg[i++] = "varlink";
                arg[i++] = "bridge";
                arg[i] = NULL;

                close(sp[0]);

                if (dup2(sp[1], STDIN_FILENO) != STDIN_FILENO ||
                    dup2(sp[1], STDOUT_FILENO) != STDOUT_FILENO)
                        return -CLI_ERROR_PANIC;

                if (sp[1] != STDIN_FILENO && sp[1] != STDOUT_FILENO)
                        close(sp[1]);

                execvp(arg[0], (char **) arg);
                _exit(EXIT_FAILURE);
        }

        close(sp[1]);

        r = varlink_connection_new_from_socket(connectionp, sp[0]);
        if (r < 0)
                return -CLI_ERROR_CANNOT_CONNECT;

        return 0;
}

long cli_connect(Cli *cli,
                 VarlinkConnection **connectionp,
                 bool ssh,
                 const char *address,
                 unsigned int port,
                 const char *method) {
        _cleanup_(freep) char *addr = NULL;
        _cleanup_(freep) char *interface = NULL;
        long r;

        if (ssh)
                return cli_connection_new_ssh(connectionp, address, port);

        if (address)
                return varlink_connection_new(connectionp, address);

        r = varlink_interface_parse_qualified_name(method, &interface, NULL);
        if (r < 0)
                return r;

        r = cli_resolve(cli, interface, &addr);
        if (r < 0)
                return r;

        return varlink_connection_new(connectionp, addr);
}

long cli_call_on_address(Cli *cli,
                         const char *address,
                         const char *method,
                         VarlinkObject *parameters,
                         char **errorp,
                         VarlinkObject **outp) {
        _cleanup_(varlink_connection_freep) VarlinkConnection *connection = NULL;
        struct Reply reply = {};
        long r;

        r = varlink_connection_new(&connection, address);
        if (r < 0)
                return -CLI_ERROR_PANIC;

        r = varlink_connection_call(connection, method, parameters, 0, reply_callback, &reply);
        if (r < 0)
                return r;

        r = cli_process_all_events(cli, connection);
        if (r < 0)
                return r;

        if (errorp)
                *errorp = reply.error;
        else
                free(reply.error);

        if (outp)
                *outp = reply.parameters;
        else
                varlink_object_unref(reply.parameters);

        return 0;
}

long cli_process_all_events(Cli *cli, VarlinkConnection *connection) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
        _cleanup_(freep) char *error = NULL;
        long r;

        r = epoll_add(cli->epoll_fd,
                      varlink_connection_get_fd(connection),
                      varlink_connection_get_events(connection),
                      connection);
        if (r < 0)
                return -CLI_ERROR_PANIC;

        for (;;) {
                struct epoll_event ev;

                r = epoll_wait(cli->epoll_fd, &ev, 1, cli->timeout);
                if (r < 0) {
                        if (errno == EINTR)
                                continue;

                        return -CLI_ERROR_PANIC;
                }

                if (r == 0)
                        return -CLI_ERROR_TIMEOUT;

                if (ev.data.ptr == NULL) {
                        struct signalfd_siginfo fdsi;
                        long size;

                        size = read(cli->signal_fd, &fdsi, sizeof(struct signalfd_siginfo));
                        if (size != sizeof(struct signalfd_siginfo))
                                continue;

                        switch (fdsi.ssi_signo) {
                                case SIGTERM:
                                case SIGINT:
                                        return -CLI_ERROR_CANCELED;

                                case SIGPIPE:
                                        return -CLI_ERROR_CALL_FAILED;

                                default:
                                        return -CLI_ERROR_PANIC;
                        }

                } else if (ev.data.ptr == connection) {
                        r = varlink_connection_process_events(connection, ev.events);
                        switch (r) {
                                case 0:
                                        break;

                                case -VARLINK_ERROR_CONNECTION_CLOSED:
                                        return -CLI_ERROR_CONNECTION_CLOSED;

                                default:
                                        return -CLI_ERROR_PANIC;
                        }

                        if (varlink_connection_is_closed(connection))
                                break;

                        r = epoll_mod(cli->epoll_fd,
                                      varlink_connection_get_fd(connection),
                                      varlink_connection_get_events(connection),
                                      connection);
                        if (r < 0)
                                return -CLI_ERROR_PANIC;

                } else
                        return -CLI_ERROR_PANIC;
        }

        return 0;
}

static const CliCommand *cli_get_command(Cli *cli, const char *name) {
        for (unsigned long i = 0; cli_commands[i]; i += 1) {
                if (strcmp(name, cli_commands[i]->name) == 0)
                        return cli_commands[i];
        }

        return NULL;
}

static long cli_parse_arguments(int argc, char **argv, CliArguments *arguments) {
        int c;

        opterr = 0;

        while ((c = getopt_long(argc, argv, "+:ht:R:V", cli_options, NULL)) >= 0) {
                switch (c) {
                        case 'h':
                                arguments->help = true;
                                break;

                        case 't':
                                arguments->timeout = strtoul(optarg, NULL, 0) * 1000;
                                break;

                        case 'R':
                                arguments->resolver = optarg;
                                break;

                        case 'V':
                                arguments->version = true;
                                break;

                        case '?':
                                return -CLI_ERROR_INVALID_ARGUMENT;

                        case ':':
                                return -CLI_ERROR_MISSING_ARGUMENT;

                        default:
                                return -CLI_ERROR_PANIC;
                }
        }

        arguments->command = argv[optind];
        arguments->remaining_argc = argc - optind;
        arguments->remaining_argv = argv + optind;
        optind = 0;

        return 0;
}

long cli_run(Cli *cli, int argc, char **argv) {
        CliArguments arguments = {
                .timeout = -1
        };
        const CliCommand *command;
        long r;

        r = cli_parse_arguments(argc, argv, &arguments);
        if (r < 0)
                return r;

        if (arguments.resolver)
                cli->resolver = arguments.resolver;

        if (arguments.timeout != -1)
                cli->timeout = arguments.timeout;

        if (arguments.help) {
                printf("Usage: %s COMMAND [OPTIONS]...\n", program_invocation_short_name);
                printf("\n");
                printf("  -h, --help             display this help text and exit\n");
                printf("  -t, --timeout=SECONDS  time in seconds to wait for a reply\n");
                printf("  -R, --resolver=ADDRESS address of the resolver\n");
                printf("  -V, --version          output version information and exit\n");
                printf("\n");
                printf("Commands:\n");
                for (unsigned long i = 0; cli_commands[i]; i += 1)
                        printf("  %-16.16s %s\n", cli_commands[i]->name, cli_commands[i]->info);
                printf("\n");
                printf("Errors:\n");
                for (long i = 1 ; i < CLI_ERROR_MAX; i += 1)
                        printf(" %3li %s\n", i, cli_error_string(i));
                printf("\n");
                return 0;
        }

        if (arguments.version) {
                printf(VERSION "\n");
                return 0;
        }

        if (arguments.command == NULL) {
                fprintf(stderr, "Usage: %s COMMAND [OPTIONS]\n", program_invocation_short_name);
                fprintf(stderr, "Try '%s --help' for more information\n", program_invocation_short_name);
                return -CLI_ERROR_COMMAND_NOT_FOUND;
        }

        command = cli_get_command(cli, arguments.command);
        if (!command) {
                fprintf(stderr, "%s: '%s' is not a valid command.\n", program_invocation_short_name, arguments.command);
                fprintf(stderr, "Try '%s --help' for more information\n", program_invocation_short_name);
                return -CLI_ERROR_COMMAND_NOT_FOUND;
        }

        return command->run(cli, arguments.remaining_argc, arguments.remaining_argv);
}

void cli_print_completion(const char *current, const char *format, ...) {
        va_list args;
        _cleanup_(freep) char *word = NULL;

        va_start(args, format);
        vasprintf(&word, format, args);
        va_end(args);

        if (strncmp(word, current, strlen(current)) != 0)
                return;

        printf("%s\n", word);
}

long cli_complete(Cli *cli, int argc, char **argv, const char *current) {
        CliArguments arguments = {
                .timeout = -1
        };
        int r;

        r = cli_parse_arguments(argc, argv, &arguments);
        if (r < 0) {
                if (r == -CLI_ERROR_INVALID_ARGUMENT)
                        return 0;

                return -CLI_ERROR_PANIC;
        }

        if (arguments.resolver)
                cli->resolver = arguments.resolver;

        if (arguments.timeout != -1)
                cli->timeout = arguments.timeout;

        if (arguments.command) {
                const CliCommand *command;

                command = cli_get_command(cli, arguments.command);
                if (command) {
                        if (!command->complete)
                                return 0;

                        return command->complete(cli, arguments.remaining_argc, arguments.remaining_argv, current);
                }
        }

        if (current[0] == '-') {
                if (current[strlen(current) - 1] == '=')
                        return 0;

                for (const struct option *option = cli_options; option->name; option += 1)
                        cli_print_completion(current, "--%s%s", option->name, option->has_arg ? "=" : "");

        } else {
                for (unsigned long i = 0; cli_commands[i]; i += 1)
                        cli_print_completion(current, "%s", cli_commands[i]->name);
        }

        return 0;
}

long cli_complete_options(Cli *cli, const struct option *options, const char *current) {
        for (const struct option *option = options; option->name; option += 1)
                cli_print_completion(current, "--%s%s", option->name, option->has_arg ? "=" : "");

        return true;
}

long cli_complete_interfaces(Cli *cli, const char *current, bool end_with_dot) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *out = NULL;
        _cleanup_(freep) char *error = NULL;
        VarlinkArray *interfaces;
        long n_interfaces;
        long r;

        r = cli_call(cli, "org.varlink.resolver.GetInfo", NULL, &error, &out);
        if (r < 0)
                return -r;

        if (error)
                return -CLI_ERROR_CALL_FAILED;

        r = varlink_object_get_array(out, "interfaces", &interfaces);
        if (r < 0)
                return -CLI_ERROR_INVALID_MESSAGE;

        n_interfaces = varlink_array_get_n_elements(interfaces);
        for (long i = 0; i < n_interfaces; i += 1) {
                VarlinkObject *entry;
                const char *interface;

                varlink_array_get_object(interfaces, i, &entry);
                varlink_object_get_string(entry, "interface", &interface);

                cli_print_completion(current, "%s%s", interface, end_with_dot ? "." : "");
        }

        return 0;
}

long cli_complete_addresses(Cli *cli, const char *current) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *out = NULL;
        _cleanup_(freep) char *error = NULL;
        VarlinkArray *interfaces;
        long n_interfaces;
        long r;

        r = cli_call(cli, "org.varlink.resolver.GetInfo", NULL, &error, &out);
        if (r < 0)
                return -r;

        if (error)
                return -CLI_ERROR_CALL_FAILED;

        r = varlink_object_get_array(out, "interfaces", &interfaces);
        if (r < 0)
                return -CLI_ERROR_INVALID_MESSAGE;

        n_interfaces = varlink_array_get_n_elements(interfaces);
        for (long i = 0; i < n_interfaces; i += 1) {
                VarlinkObject *entry;
                const char *address;

                varlink_array_get_object(interfaces, i, &entry);
                varlink_object_get_string(entry, "address", &address);

                cli_print_completion(current, "%s", address);
        }

        return 0;
}

long cli_complete_methods(Cli *cli, const char *current) {
        _cleanup_(freep) char *address = NULL;
        _cleanup_(freep) char *method = NULL;
        _cleanup_(freep) char *interface_name = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *out = NULL;
        _cleanup_(varlink_interface_freep) VarlinkInterface *interface = NULL;
        _cleanup_(freep) char *error = NULL;
        const char *description = NULL;
        long r;

        r = cli_parse_url(current,
                          NULL,
                          &address,
                          NULL,
                          &method);
        if (r < 0)
                return -r;

        r = varlink_interface_parse_qualified_name(method, &interface_name, NULL);
        if (r < 0)
                return cli_complete_interfaces(cli, current, true);

        if (!address) {
                r = cli_resolve(cli, interface_name, &address);
                switch (r) {
                        case 0:
                                break;
                        case -CLI_ERROR_CANNOT_RESOLVE:
                                return cli_complete_interfaces(cli, current, true);
                        default:
                                return -r;
                }
        }

        varlink_object_new(&parameters);
        varlink_object_set_string(parameters, "interface", interface_name);

        r = cli_call_on_address(cli, address, "org.varlink.service.GetInterfaceDescription", parameters, &error, &out);
        if (r < 0)
                return r;

        if (error)
                return -CLI_ERROR_REMOTE_ERROR;

        if (varlink_object_get_string(out, "description", &description) < 0)
                return -CLI_ERROR_CALL_FAILED;

        r = varlink_interface_new(&interface, description, NULL);
        if (r < 0)
                return -CLI_ERROR_PANIC;

        for (unsigned long i = 0; i < interface->n_members; i += 1) {
                const VarlinkInterfaceMember *member = &interface->members[i];

                if (member->type != VARLINK_MEMBER_METHOD)
                        continue;

                cli_print_completion(current, "%s.%s", interface_name, member->name);
        }

        return 0;
}

static long url_decode(char **outp, const char *in, unsigned long len) {
        _cleanup_(freep) char *out = NULL;
        unsigned long j = 0;

        out = malloc(len);

        for (unsigned long i = 0; in[i] != '\0' && i < len; i += 1) {
                if (in[i] == '%') {
                        unsigned int hex;

                        if (i + 3 > len)
                                return -CLI_ERROR_INVALID_ARGUMENT;

                        if (sscanf(in + i + 1, "%02x", &hex) != 1)
                                return -CLI_ERROR_INVALID_ARGUMENT;

                        out[j] = hex;
                        j += 1;
                        i += 2;

                        continue;
                }

                out[j] = in[i];
                j += 1;
        }

        out[j] = '\0';
        *outp = out;
        out = NULL;

        return j;
}

long cli_parse_url(const char *url,
                   bool *sshp,
                   char **addressp,
                   unsigned int *portp,
                   char **methodp) {
        bool ssh = false;
        _cleanup_(freep) char *address = NULL;
        unsigned int port = 0;
        _cleanup_(freep) char *method = NULL;
        long r;

        if (strncmp(url, "varlink://", 10) == 0) {
                char *s;

                /* varlink://[ADDRESS][/INTERFACE.METHOD] */
                s = strchrnul(url + 10, '/');
                if (s - (url + 10) > 0) {
                        r = url_decode(&address, url + 10, s - (url + 10));
                        if (r < 0)
                                return r;
                }

                if (s[1] != '\0')
                        method = strdup(s + 1);

        } else if (strncmp(url, "ssh://", 6) == 0) {
                char *s;
                char *h;
                unsigned int p;

                /* ssh://HOST[:PORT][/INTERFACE.METHOD] */
                ssh = true;

                s = strchrnul(url + 6, '/');
                address = strndup(url + 6, s - (url + 6));

                if (s[1] != '\0')
                        method = strdup(s + 1);

                /* Extract optional port number */
                if (sscanf(address, "%m[^:]:%d", &h, &p) == 2) {
                        free(address);
                        address = h;
                        port = p;
                }

        } else if (strchr(url, '/')) {
                char *s;

                /* ADDRESS[/INTERFACE.METHOD] */
                s = strrchr(url, '/');
                if (s - url > 0)
                        address = strndup(url, s - url);

                if (s[1] != '\0')
                        method = strdup(s + 1);

        } else {
                /* INTERFACE.METHOD */
                method = strdup(url);
        }

        if (sshp)
                *sshp = ssh;

        if (addressp) {
                *addressp = address;
                address = NULL;
        }

        if (portp)
                *portp = port;

        if (methodp) {
                *methodp = method;
                method = NULL;
        }

        return 0;
}
