#include "command.h"
#include "interface.h"
#include "protocol.h"
#include "util.h"
#include "varlink.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <string.h>
#include <sys/epoll.h>

#define STREAM_BUFFER_SIZE (8 * 1024 * 1204)

typedef struct {
        int fd;
        uint8_t *buffer;
        unsigned long start, end;
        bool closed;
} VarlinkStream;

typedef struct {
        Cli *cli;
        long status;
        AVLTree *services;
        VarlinkObject *info;
        VarlinkStream in;
} Bridge;

static void varlink_stream_init(VarlinkStream *stream, int fd) {
        stream->fd = fd;
        stream->buffer = malloc(STREAM_BUFFER_SIZE);
        stream->start = 0;
        stream->end = 0;
        stream->closed = false;
}

static void varlink_stream_deinit(VarlinkStream *stream) {
        stream->fd = -1;
        free(stream->buffer);
        stream->start = 0;
        stream->end = 0;
        stream->closed = false;
}

static long varlink_stream_read(VarlinkStream *s, VarlinkObject **messagep) {
        long r;

        for (;;) {
                uint8_t *nul;
                long n;

                nul = memchr(&s->buffer[s->start], 0, s->end - s->start);
                if (nul) {
                        r = varlink_object_new_from_json(messagep, (const char *)&s->buffer[s->start]);
                        if (r < 0)
                                return r;

                        s->start = (nul + 1) - s->buffer;
                        return 0;
                }

                n = s->end - s->start;
                if (n > 0)
                        s->buffer = memmove(s->buffer, s->buffer + s->start, n);

                s->start = 0;
                s->end = n;

                if (s->end == STREAM_BUFFER_SIZE)
                        return -VARLINK_ERROR_INVALID_MESSAGE;

                n = read(s->fd, &s->buffer[s->end], STREAM_BUFFER_SIZE - s->end);
                if (n < 0) {
                        s->closed = true;
                        return -VARLINK_ERROR_RECEIVING_MESSAGE;
                }

                if (n == 0) {
                        s->closed = true;
                        if (s->end != s->start)
                                return -VARLINK_ERROR_INVALID_MESSAGE;
                        break;
                }

                s->end += n;
        }

        *messagep = NULL;
        return 0;
}

static void bridge_free(Bridge *bridge) {
        if (bridge->services)
                avl_tree_free(bridge->services);

        if (bridge->info)
                varlink_object_unref(bridge->info);

        varlink_stream_deinit(&bridge->in);

        free(bridge);
}

static void bridge_freep(Bridge **bridgep) {
        if (*bridgep)
                bridge_free(*bridgep);
}

static long bridge_new(Bridge **bridgep, Cli *cli) {
        _cleanup_(bridge_freep) Bridge *bridge = NULL;
        _cleanup_(freep) char **error = NULL;

        bridge = calloc(1, sizeof(Bridge));
        bridge->cli = cli;

        varlink_stream_init(&bridge->in, STDIN_FILENO);

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

        r = varlink_object_new(&message);
        if (r < 0)
                return -CLI_ERROR_PANIC;

        if (error)
                varlink_object_set_string(message, "error", error);

        if (parameters)
                varlink_object_set_object(message, "parameters", parameters);

        if (flags & VARLINK_REPLY_CONTINUES)
                varlink_object_set_bool(message, "continues", true);

        r = varlink_object_to_json(message, &json);
        if (r < 0)
                return -CLI_ERROR_PANIC;

        printf("%s%c", json, '\0');
        fflush(stdout);

        return 0;
}

static void reply_callback(VarlinkConnection *connection,
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
}

static long bridge_run(Cli *cli, int argc, char **argv) {
        static const struct option options[] = {
                { "help",    no_argument,       NULL, 'h' },
                {}
        };
        int c;
        _cleanup_(varlink_object_unrefp) VarlinkObject *info = NULL;
        _cleanup_(freep) char *error = NULL;
        _cleanup_(bridge_freep) Bridge *bridge = NULL;
        long r;

        while ((c = getopt_long(argc, argv, "a:h", options, NULL)) >= 0) {
                switch (c) {
                        case 'h':
                                printf("Usage: %s bridge\n", program_invocation_short_name);
                                printf("\n");
                                printf("Bridge varlink messages on standard in and out to varlink services\n"
                                       "on this machine.\n");
                                printf("\n");
                                printf("  -h, --help             display this help text and exit\n");
                                return 0;

                        default:
                                fprintf(stderr, "Try '%s --help' for more information\n",
                                        program_invocation_short_name);
                                return -CLI_ERROR_INVALID_ARGUMENT;
                }
        }

        bridge_new(&bridge, cli);

        while (bridge->status == 0) {
                _cleanup_(varlink_object_unrefp) VarlinkObject *call = NULL;
                _cleanup_(freep) char *method = NULL;
                uint64_t flags;
                const char *dot;
                _cleanup_(freep) char *interface = NULL;
                _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
                _cleanup_(varlink_connection_freep) VarlinkConnection *connection = NULL;

                r = varlink_stream_read(&bridge->in, &call);
                switch (r) {
                        case 0:
                                break;
                        case -VARLINK_ERROR_INVALID_MESSAGE:
                                return -CLI_ERROR_INVALID_MESSAGE;
                        default:
                                return -CLI_ERROR_PANIC;
                }

                if (!call)
                        break;

                r = varlink_protocol_unpack_call(call, &method, &parameters, &flags);
                if (r < 0)
                        return -CLI_ERROR_INVALID_MESSAGE;

                dot = strrchr(method, '.');
                if (!dot)
                        return -CLI_ERROR_INVALID_MESSAGE;

                interface = strndup(method, dot - method);

                /* Forward org.varlink.service.GetInfo to org.varlink.resolver.GetInfo */
                if (strcmp(method, "org.varlink.service.GetInfo") == 0) {
                        r = varlink_connection_new(&connection, cli->resolver);
                        if (r < 0)
                                return -CLI_ERROR_PANIC;

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
                        if (r < 0)
                                return -CLI_ERROR_PANIC;

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
                        _cleanup_(freep) char *address = NULL;

                        r = cli_resolve(cli, interface, &address);
                        if (r < 0)
                                return -CLI_ERROR_PANIC;

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

const CliCommand command_bridge = {
        .name = "bridge",
        .info = "Bridge varlink messages to services on this machine",
        .run = bridge_run
};
