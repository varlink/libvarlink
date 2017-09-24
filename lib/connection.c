#include "connection.h"
#include "object.h"
#include "protocol.h"
#include "socket.h"
#include "util.h"

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <unistd.h>

typedef struct ReplyCallback ReplyCallback;

struct ReplyCallback {
        uint64_t call_flags;
        VarlinkReplyFunc func;
        void *userdata;

        STAILQ_ENTRY(ReplyCallback) entry;
};

struct VarlinkConnection {
        char *address;

        VarlinkSocket socket;

        STAILQ_HEAD(pending, ReplyCallback) pending;

        VarlinkConnectionClosedFunc closed_callback;
        void *closed_userdata;
};

static long connection_new(VarlinkConnection **connectionp) {
        VarlinkConnection *connection;

        connection = calloc(1, sizeof(VarlinkConnection));
        varlink_socket_init(&connection->socket);
        STAILQ_INIT(&connection->pending);

        *connectionp = connection;
        return 0;
}

long varlink_connection_new_from_socket(VarlinkConnection **connectionp, int socket) {
        _cleanup_(varlink_connection_freep) VarlinkConnection *connection = NULL;

        connection_new(&connection);
        connection->socket.fd = socket;

        *connectionp = connection;
        connection = NULL;

        return 0;
}

_public_ long varlink_connection_new(VarlinkConnection **connectionp, const char *address) {
        _cleanup_(varlink_connection_freep) VarlinkConnection *connection = NULL;
        long r;

        connection_new(&connection);
        connection->address = strdup(address);

        r = varlink_socket_connect(&connection->socket, address);
        if (r < 0)
                return r; /* CannotConnect or InvalidAddress */

        *connectionp = connection;
        connection = NULL;

        return 0;
}

_public_ VarlinkConnection *varlink_connection_free(VarlinkConnection *connection) {
        if (connection->socket.fd >= 0)
                varlink_connection_close(connection);

        while (!STAILQ_EMPTY(&connection->pending)) {
                _cleanup_(freep) ReplyCallback *cb = NULL;

                cb = STAILQ_FIRST(&connection->pending);
                STAILQ_REMOVE_HEAD(&connection->pending, entry);
        }

        free(connection->address);
        free(connection);

        return NULL;
}

_public_ void varlink_connection_freep(VarlinkConnection **connectionp) {
        if (*connectionp)
                varlink_connection_free(*connectionp);
}

_public_ long varlink_connection_process_events(VarlinkConnection *connection, int events) {
        long r;

        if (connection->socket.fd < 0)
                return -VARLINK_ERROR_CONNECTION_CLOSED;

        r = varlink_socket_dispatch(&connection->socket, events);
        if (r < 0)
                return r;

        /* Check if the socket is valid, because a callback might have closed the connection */
        while (connection->socket.fd >= 0) {
                _cleanup_(varlink_object_unrefp) VarlinkObject *message = NULL;
                _cleanup_(freep) char *error = NULL;
                _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
                uint64_t flags = 0;
                ReplyCallback *callback;

                r = varlink_socket_read(&connection->socket, &message);
                if (r < 0)
                        return r;

                if (!message)
                        return 0;

                callback = STAILQ_FIRST(&connection->pending);
                if (!callback)
                        return -VARLINK_ERROR_INVALID_MESSAGE;

                r = varlink_protocol_unpack_reply(message, &error, &parameters, &flags);
                if (r < 0)
                        return -VARLINK_ERROR_INVALID_MESSAGE;

                if ((flags & VARLINK_REPLY_CONTINUES) && !(callback->call_flags & VARLINK_CALL_MORE))
                        return VARLINK_ERROR_INVALID_MESSAGE;

                callback->func(connection, error, parameters, flags, callback->userdata);

                if (!(flags & VARLINK_REPLY_CONTINUES)) {
                        STAILQ_REMOVE_HEAD(&connection->pending, entry);
                        free(callback);
                }
        }

        return 0;
}

_public_ int varlink_connection_get_events(VarlinkConnection *connection) {
        return varlink_socket_get_events(&connection->socket);
}

_public_ long varlink_connection_close(VarlinkConnection *connection) {
        varlink_socket_deinit(&connection->socket);

        while (connection->closed_callback)
                connection->closed_callback(connection, connection->closed_userdata);

        return 0;
}

_public_ bool varlink_connection_is_closed(VarlinkConnection *connection) {
        return connection->socket.fd < 0;
}

_public_ int varlink_connection_get_fd(VarlinkConnection *connection) {
        return connection->socket.fd;
}

_public_ long varlink_connection_call(VarlinkConnection *connection,
                                      const char *qualified_method,
                                      VarlinkObject *parameters,
                                      uint64_t flags,
                                      VarlinkReplyFunc func,
                                      void *userdata) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *call = NULL;
        ReplyCallback *callback;
        long r;

        if (connection->socket.fd < 0)
                return -VARLINK_ERROR_CONNECTION_CLOSED;

        if (flags & VARLINK_CALL_MORE && flags & VARLINK_CALL_ONEWAY)
                return -VARLINK_ERROR_INVALID_CALL;

        r = varlink_protocol_pack_call(qualified_method, parameters, flags, &call);
        if (r < 0)
                return r;

        if (!(flags & VARLINK_CALL_ONEWAY)) {
                callback = calloc(1, sizeof(ReplyCallback));
                callback->call_flags = flags;
                callback->func = func;
                callback->userdata = userdata;
                STAILQ_INSERT_TAIL(&connection->pending, callback, entry);
        }

        return varlink_socket_write(&connection->socket, call);
}

_public_ void varlink_connection_set_close_callback(VarlinkConnection *connection,
                                                    VarlinkConnectionClosedFunc closed,
                                                    void *userdata) {
        connection->closed_callback = closed;
        connection->closed_userdata = userdata;
}
