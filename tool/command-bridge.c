#include "command.h"
#include "interface.h"
#include "message.h"
#include "stream.h"
#include "util.h"
#include "uri.h"
#include "varlink.h"
#include "connection.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <string.h>
#include <sys/epoll.h>

#define STREAM_BUFFER_SIZE (8 * 1024 * 1204)

typedef struct {
        Cli *cli;
        int epoll_fd;
        long status;
        AVLTree *services;
        VarlinkObject *info;
        VarlinkStream *in;
} Bridge;

static void bridge_free(Bridge *bridge) {
        if (bridge->services)
                avl_tree_free(bridge->services);

        if (bridge->info)
                varlink_object_unref(bridge->info);

        varlink_stream_free(bridge->in);

        if (bridge->epoll_fd >= 0)
                close(bridge->epoll_fd);

        free(bridge);
}

static void bridge_freep(Bridge **bridgep) {
        if (*bridgep)
                bridge_free(*bridgep);
}

static long fd_nonblock(int fd) {
        int flags;

        flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0)
                return -errno;

        if (flags & O_NONBLOCK)
                return 0;

        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
                return -errno;

        return 0;
}

static long bridge_new(Bridge **bridgep, Cli *cli) {
        _cleanup_(bridge_freep) Bridge *bridge = NULL;
        long r;

        bridge = calloc(1, sizeof(Bridge));
        bridge->cli = cli;

        if (fd_nonblock(STDIN_FILENO) < 0)
                return -CLI_ERROR_PANIC;

        r = varlink_stream_new(&bridge->in, STDIN_FILENO);
        if (r < 0)
                return r;

        bridge->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
        if (bridge->epoll_fd < 0)
                return -CLI_ERROR_PANIC;

        if (epoll_add(bridge->epoll_fd, cli->signal_fd, EPOLLIN, bridge) < 0 ||
            epoll_add(bridge->epoll_fd, bridge->in->fd, EPOLLIN, NULL) < 0)
                return -CLI_ERROR_PANIC;

        *bridgep = bridge;
        bridge = NULL;

        return 0;
}

static long bridge_reply(Bridge *bridge,
                         const char *error,
                         VarlinkObject *parameters,
                         uint64_t flags) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *message = NULL;
        _cleanup_(freep) char *json = NULL;
        long r;

        r = varlink_message_pack_reply(error, parameters, flags, &message);
        if (r < 0)
                return -CLI_ERROR_PANIC;

        r = varlink_object_to_json(message, &json);
        if (r < 0)
                return -CLI_ERROR_PANIC;

        printf("%s%c", json, '\0');
        fflush(stdout);

        return 0;
}

static long reply_callback(VarlinkConnection *connection,
                           const char *error,
                           VarlinkObject *parameters,
                           uint64_t flags,
                           void *userdata) {
        Bridge *bridge = userdata;
        long r;

        r = bridge_reply(bridge, error, parameters, flags);
        if (r < 0)
                bridge->status = r;

        if (!(flags & VARLINK_REPLY_CONTINUES))
                varlink_connection_close(connection);

        return 0;
}

static const struct option options[] = {
        { "connect", required_argument, NULL, 'c' },
        { "help",    no_argument,       NULL, 'h' },
        {}
};

static long handleBridge(Cli *cli, Bridge *bridge) {
        while (bridge->status >= 0) {
                _cleanup_(varlink_object_unrefp) VarlinkObject *call = NULL;
                _cleanup_(freep) char *method = NULL;
                _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
                uint64_t flags;
                _cleanup_(varlink_connection_freep) VarlinkConnection *connection = NULL;
                long r;

                /* Read one message. */
                r = varlink_stream_read(bridge->in, &call);
                switch (r) {
                        case 0:
                                if (bridge->in->hup)
                                        return 0;

                                /* Wait for message from client. */
                                for (;;) {
                                        struct epoll_event ev;

                                        r = epoll_wait(bridge->epoll_fd, &ev, 1, -1);
                                        if (r < 0) {
                                                if (errno == EINTR)
                                                        continue;

                                                return -CLI_ERROR_PANIC;
                                        }

                                        if (ev.data.ptr == bridge)
                                                return -CLI_ERROR_CANCELED;

                                        break;
                                }
                                continue;

                        case 1:
                                break;

                        case -VARLINK_ERROR_INVALID_MESSAGE:
                                return -CLI_ERROR_INVALID_MESSAGE;

                        default:
                                return -CLI_ERROR_PANIC;
                }

                r = varlink_message_unpack_call(call, &method, &parameters, &flags);
                if (r < 0)
                        return -CLI_ERROR_INVALID_MESSAGE;

                if (strcmp(method, "org.varlink.service.GetInfo") == 0) {
                        r = varlink_connection_new(&connection, cli->resolver);
                        if (r < 0)
                                return -CLI_ERROR_PANIC;

                        /* Forward org.varlink.service.GetInfo to org.varlink.resolver.GetInfo */
                        r = varlink_connection_call(connection,
                                                    "org.varlink.resolver.GetInfo",
                                                    parameters,
                                                    flags,
                                                    reply_callback,
                                                    bridge);
                        if (r < 0)
                                return -CLI_ERROR_PANIC;

                } else if (strcmp(method, "org.varlink.service.GetInterfaceDescription") == 0) {
                        _cleanup_(freep) char *address = NULL;
                        const char *interf;

                        r = varlink_object_get_string(parameters, "interface", &interf);
                        if (r < 0)
                                return -CLI_ERROR_MISSING_ARGUMENT;

                        r = cli_resolve(cli, interf, &address);
                        if (r < 0) {
                                bridge_reply(bridge, "org.varlink.service.InterfaceNotFound", NULL, 0);
                                return -CLI_ERROR_PANIC;
                        }

                        r = varlink_connection_new(&connection, address);
                        if (r < 0)
                                return -CLI_ERROR_PANIC;

                        r = varlink_connection_call(connection,
                                                    "org.varlink.service.GetInterfaceDescription",
                                                    parameters,
                                                    flags,
                                                    reply_callback,
                                                    bridge);
                        if (r < 0)
                                return -CLI_ERROR_PANIC;

                } else {
                        _cleanup_(varlink_uri_freep) VarlinkURI *uri = NULL;
                        _cleanup_(freep) char *address = NULL;

                        r = varlink_uri_new(&uri, method, true);
                        if (r < 0) {
                                bridge_reply(bridge, "org.varlink.service.InvalidParameter", NULL, 0);
                                return -CLI_ERROR_INVALID_MESSAGE;
                        }

                        r = cli_resolve(cli, uri->interface, &address);
                        if (r < 0) {
                                bridge_reply(bridge, "org.varlink.service.InterfaceNotFound", NULL, 0);
                                return -CLI_ERROR_PANIC;
                        }

                        if (connection != NULL) {
                                varlink_connection_free(connection);
                        }

                        r = varlink_connection_new(&connection, address);
                        if (r < 0)
                                return -CLI_ERROR_PANIC;

                        r = varlink_connection_call(connection,
                                                    method,
                                                    parameters,
                                                    flags,
                                                    reply_callback,
                                                    bridge);
                        if (r < 0)
                                return -CLI_ERROR_PANIC;

                }

                r = cli_process_all_events(cli, connection);
                if (r < 0)
                        return r;
        }

        return bridge->status;
}

static long handleDirectBridge(Cli *cli, Bridge *bridge, VarlinkURI *bridge_uri) {
        _cleanup_(varlink_connection_freep) VarlinkConnection *connection = NULL;
        VarlinkStream *out_stream = NULL;
        long r;

        r = cli_connect(cli, &connection, bridge_uri);

        if (r < 0) {
                fprintf(stderr, "Unable to connect: %s\n", varlink_error_string(-r));
                return r;
        }

        r = varlink_stream_new(&out_stream, STDOUT_FILENO);
        if (r < 0)
                return r;

        varlink_connection_bridge(cli->signal_fd, bridge->in, out_stream, connection);

        varlink_stream_free(out_stream);

        return bridge->status;
}

static long bridge_run(Cli *cli, int argc, char **argv) {
        int c;
        const char *connect = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *info = NULL;
        _cleanup_(freep) char *error = NULL;
        _cleanup_(bridge_freep) Bridge *bridge = NULL;
        _cleanup_(varlink_connection_freep) VarlinkConnection *bridge_connection = NULL;
        _cleanup_(varlink_uri_freep) VarlinkURI *bridge_uri = NULL;
        long r;

        while ((c = getopt_long(argc, argv, "h", options, NULL)) >= 0) {
                switch (c) {
                        case 'h':
                                printf("Usage: %s bridge\n", program_invocation_short_name);
                                printf("\n");
                                printf("Bridge varlink messages on standard in and out to varlink services\n"
                                       "on this machine.\n");
                                printf("\n");
                                printf("  -h, --help             display this help text and exit\n");
                                return 0;

                        case 'c':
                                connect = optarg;
                                break;

                        default:
                                fprintf(stderr, "Try '%s --help' for more information\n",
                                        program_invocation_short_name);
                                return -CLI_ERROR_INVALID_ARGUMENT;
                }
        }

        r = bridge_new(&bridge, cli);
        if (r < 0)
                return r;


        if (connect) {
                r = varlink_uri_new(&bridge_uri, connect, false);
                if (r < 0) {
                        fprintf(stderr, "Unable to parse --connect ADDRESS\n");
                        return -CLI_ERROR_INVALID_ARGUMENT;
                }
        }

        if (cli->activate || cli->bridge || bridge_uri)
                return handleDirectBridge(cli, bridge, bridge_uri);
        else
                return handleBridge(cli, bridge);

}

static long bridge_complete(Cli *cli, int argc, char **argv, const char *current) {
        if (current[0] == '-')
                return cli_complete_options(cli, options, current);

        return 0;
}

const CliCommand command_bridge = {
        .name = "bridge",
        .info = "Bridge varlink messages to services on this machine",
        .run = bridge_run,
        .complete = bridge_complete
};
